#include <string.h>

#include "configuration/app_config.h"
#include "context/app_context.h"
#include "core/network/console_init.h"
#include "core/network/time_init.h"
#include "core/network/wifi_init.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/rtc.h"
#include "ui/screens/home_page.h"

static const char *TAG = "console";

static const char *AP_SSID = AP_CONSOLE_SSID;
static const char *AP_PASS = AP_CONSOLE_PASSWORD;

static bool consoleStarted = false;
char consoleIp[16] = "0.0.0.0";
static esp_netif_t *ap_netif = NULL;
static httpd_handle_t server = NULL;

// HTML content (same as before, stored as const char*)
static const char html_dashboard[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <title>ESP32-S3 Console</title>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", system-ui, sans-serif;
            background: #000;
            color: #fff;
            min-height: 100vh;
            padding: 20px;
            overflow-x: hidden;
        }
        .container { max-width: 1400px; margin: 0 auto; }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 30px;
            flex-wrap: wrap;
            gap: 20px;
        }
        .logo { display: flex; align-items: center; gap: 12px; }
        .logo-icon {
            width: 48px;
            height: 48px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            border-radius: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 24px;
        }
        .logo-text h1 { font-size: 24px; font-weight: 700; letter-spacing: -0.5px; }
        .logo-text p { font-size: 13px; color: #888; margin-top: 2px; }
        .status-badge {
            display: flex;
            align-items: center;
            gap: 8px;
            padding: 8px 16px;
            background: rgba(255,255,255,0.05);
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 20px;
            font-size: 13px;
        }
        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: #10b981;
            animation: pulse 2s infinite;
        }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        .card {
            background: rgba(255,255,255,0.03);
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 16px;
            padding: 24px;
            backdrop-filter: blur(10px);
            transition: all 0.3s ease;
        }
        .card:hover {
            background: rgba(255,255,255,0.05);
            border-color: rgba(255,255,255,0.15);
            transform: translateY(-2px);
        }
        .card-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 20px;
        }
        .card-title { font-size: 16px; font-weight: 600; letter-spacing: -0.3px; }
        .card-icon { font-size: 24px; opacity: 0.6; }
        .stat-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 16px; }
        .stat {
            background: rgba(255,255,255,0.03);
            padding: 16px;
            border-radius: 12px;
            border: 1px solid rgba(255,255,255,0.05);
        }
        .stat-label {
            font-size: 12px;
            color: #888;
            margin-bottom: 6px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .stat-value { font-size: 20px; font-weight: 700; color: #fff; }
        .stat-unit { font-size: 14px; color: #888; margin-left: 4px; }
        .progress-bar {
            width: 100%;
            height: 8px;
            background: rgba(255,255,255,0.05);
            border-radius: 4px;
            overflow: hidden;
            margin-top: 8px;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #667eea 0%, #764ba2 100%);
            border-radius: 4px;
            transition: width 0.3s ease;
        }
        .input-group { margin-bottom: 16px; }
        .input-group label {
            display: block;
            font-size: 13px;
            color: #888;
            margin-bottom: 8px;
            font-weight: 500;
        }
        .input-group input, .input-group select {
            width: 100%;
            padding: 12px 16px;
            background: rgba(255,255,255,0.05);
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 8px;
            color: #fff;
            font-size: 14px;
            transition: all 0.2s ease;
        }
        .input-group input:focus, .input-group select:focus {
            outline: none;
            border-color: #667eea;
            background: rgba(255,255,255,0.08);
        }
        .btn {
            width: 100%;
            padding: 14px 20px;
            border: none;
            border-radius: 10px;
            font-size: 14px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s ease;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
        }
        .btn-primary {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: #fff;
        }
        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 20px rgba(102, 126, 234, 0.4);
        }
        .btn-secondary {
            background: rgba(255,255,255,0.05);
            color: #fff;
            border: 1px solid rgba(255,255,255,0.1);
        }
        .btn-secondary:hover { background: rgba(255,255,255,0.08); }
        .btn-danger {
            background: rgba(239, 68, 68, 0.15);
            color: #ef4444;
            border: 1px solid rgba(239, 68, 68, 0.3);
        }
        .btn-danger:hover { background: rgba(239, 68, 68, 0.25); }
        .btn:active { transform: translateY(0) scale(0.98); }
        .btn-group {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 12px;
            margin-top: 16px;
        }
        .console-output {
            background: rgba(0,0,0,0.4);
            border: 1px solid rgba(255,255,255,0.05);
            border-radius: 8px;
            padding: 16px;
            font-family: 'SF Mono', 'Monaco', monospace;
            font-size: 12px;
            color: #10b981;
            max-height: 200px;
            overflow-y: auto;
            line-height: 1.6;
        }
        .log-entry {
            margin-bottom: 4px;
            opacity: 0;
            animation: fadeIn 0.3s forwards;
        }
        @keyframes fadeIn { to { opacity: 1; } }
        .timestamp { color: #888; margin-right: 8px; }
        .alert {
            padding: 12px 16px;
            border-radius: 8px;
            margin-bottom: 16px;
            display: flex;
            align-items: center;
            gap: 12px;
            font-size: 13px;
        }
        .alert-info {
            background: rgba(59, 130, 246, 0.1);
            border: 1px solid rgba(59, 130, 246, 0.3);
            color: #60a5fa;
        }
        .device-list {
            display: grid;
            gap: 12px;
            margin-top: 16px;
        }
        .device-card {
            background: rgba(255,255,255,0.03);
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 12px;
            padding: 16px;
            transition: all 0.3s ease;
        }
        .device-card:hover {
            background: rgba(255,255,255,0.05);
            border-color: rgba(255,255,255,0.15);
        }
        .device-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 12px;
        }
        .device-title {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .device-icon {
            font-size: 24px;
        }
        .device-icon.ap { color: #4CAF50; }
        .device-icon.client { color: #2196F3; }
        .device-name {
            font-size: 16px;
            font-weight: 600;
        }
        .device-meta {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
            gap: 12px;
            margin-bottom: 12px;
            font-size: 12px;
            color: #888;
        }
        .meta-item {
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .meta-label {
            text-transform: uppercase;
            letter-spacing: 0.5px;
            font-size: 10px;
        }
        .meta-value {
            color: #fff;
            font-size: 14px;
            font-weight: 600;
        }
        .device-actions {
            display: flex;
            gap: 8px;
            margin-top: 12px;
        }
        .device-actions button {
            flex: 1;
            padding: 10px;
            font-size: 12px;
        }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 16px;
            margin-bottom: 24px;
        }
        .stat-card {
            background: rgba(255,255,255,0.03);
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 12px;
            padding: 16px;
        }
        .stat-label {
            font-size: 12px;
            color: #888;
            margin-bottom: 6px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .stat-value {
            font-size: 24px;
            font-weight: 700;
            color: #fff;
        }
        .empty-state {
            text-align: center;
            padding: 60px 20px;
            color: #888;
        }
        .empty-state-icon {
            font-size: 48px;
            margin-bottom: 16px;
            opacity: 0.5;
        }
        @media (max-width: 768px) {
            .grid { grid-template-columns: 1fr; }
            .btn-group { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
<div class="container">
    <div class="header">
        <div class="logo">
            <div class="logo-icon">⌚</div>
            <div class="logo-text">
                <h1>ESP32-S3 Console</h1>
                <p>Advanced Management Dashboard</p>
            </div>
        </div>
        <div class="status-badge">
            <div class="status-dot"></div>
            <span id="connectionStatus">Connected</span>
        </div>
    </div>
    <div class="grid">
        <div class="card">
            <div class="card-header">
                <div class="card-title">System Status</div>
                <div class="card-icon">📊</div>
            </div>
            <div class="stat-grid">
                <div class="stat">
                    <div class="stat-label">Uptime</div>
                    <div class="stat-value" id="uptime">--</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Free Heap</div>
                    <div class="stat-value" id="freeHeap">--<span class="stat-unit">KB</span></div>
                    <div class="progress-bar">
                        <div class="progress-fill" id="heapProgress" style="width: 0%"></div>
                    </div>
                </div>
                <div class="stat">
                    <div class="stat-label">CPU Freq</div>
                    <div class="stat-value" id="cpuFreq">--<span class="stat-unit">MHz</span></div>
                </div>
                <div class="stat">
                    <div class="stat-label">Flash Size</div>
                    <div class="stat-value" id="flashSize">--<span class="stat-unit">MB</span></div>
                </div>
            </div>
        </div>
        <div class="card">
            <div class="card-header">
                <div class="card-title">Network Info</div>
                <div class="card-icon">📡</div>
            </div>
            <div class="stat-grid">
                <div class="stat">
                    <div class="stat-label">AP SSID</div>
                    <div class="stat-value" style="font-size: 16px" id="apSsid">--</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Connected</div>
                    <div class="stat-value" id="clientCount">0</div>
                </div>
                <div class="stat">
                    <div class="stat-label">IP Address</div>
                    <div class="stat-value" style="font-size: 16px" id="ipAddr">--</div>
                </div>
                <div class="stat">
                    <div class="stat-label">MAC Address</div>
                    <div class="stat-value" style="font-size: 14px" id="macAddr">--</div>
                </div>
            </div>
        </div>
        <div class="card">
            <div class="card-header">
                <div class="card-title">WiFi Configuration</div>
                <div class="card-icon">🔌</div>
            </div>
            <div class="alert alert-info">
                <span>💡</span>
                <span>Configure WiFi credentials for normal operation mode</span>
            </div>
            <div class="input-group">
                <label>Network SSID</label>
                <input type="text" id="wifiSsid" placeholder="Enter WiFi network name">
            </div>
            <div class="input-group">
                <label>Password</label>
                <input type="password" id="wifiPassword" placeholder="Enter password">
            </div>
            <button class="btn btn-primary" onclick="saveWifi()">
                <span>💾</span>
                <span>Save WiFi Settings</span>
            </button>
        </div>
        <div class="card">
            <div class="card-header">
                <div class="card-title">Time Settings</div>
                <div class="card-icon">⏰</div>
            </div>
            <div class="input-group">
                <label>GMT Offset (seconds)</label>
                <input type="number" id="gmtOffset" value="-18000" placeholder="-18000">
            </div>
            <div class="input-group">
                <label>Daylight Offset (seconds)</label>
                <input type="number" id="dstOffset" value="3600" placeholder="3600">
            </div>
            <div class="input-group">
                <label>Time Sync Mode</label>
                <select id="timeMode">
                    <option value="0">Manual</option>
                    <option value="1" selected>Network (NTP)</option>
                </select>
            </div>
            <button class="btn btn-primary" onclick="saveTimeSettings()">
                <span>⏰</span>
                <span>Save Time Settings</span>
            </button>
        </div>
        <div class="card">
            <div class="card-header">
                <div class="card-title">System Actions</div>
                <div class="card-icon">⚙️</div>
            </div>
            <button class="btn btn-primary" onclick="syncTime()">
                <span>🔄</span>
                <span>Sync Time (NTP)</span>
            </button>
            <button class="btn btn-primary" onclick="syncTimeFromBrowser()" style="margin-top: 12px">
                <span>📱</span>
                <span>Sync Time from This Device</span>
            </button>
            <div class="btn-group">
                <button class="btn btn-secondary" onclick="reboot()">
                    <span>🔄</span>
                    <span>Reboot</span>
                </button>
                <button class="btn btn-secondary" onclick="clearPrefs()">
                    <span>🗑️</span>
                    <span>Clear Prefs</span>
                </button>
            </div>
            <button class="btn btn-danger" onclick="exitConsole()">
                <span>⬅️</span>
                <span>Exit Console Mode</span>
            </button>
        </div>
        <div class="card">
            <div class="card-header">
                <div class="card-title">System Log</div>
                <div class="card-icon">📝</div>
            </div>
            <div class="console-output" id="consoleLog">
                <div class="log-entry"><span class="timestamp">[00:00]</span> Console initialized</div>
            </div>
            <button class="btn btn-secondary" onclick="clearLog()" style="margin-top: 12px">
                <span>🗑️</span>
                <span>Clear Log</span>
            </button>
        </div>
    </div>
</div>
<script>
let logCount = 0;
function addLog(message) {
    const log = document.getElementById('consoleLog');
    const time = new Date().toLocaleTimeString('en-US', { hour12: false });
    const entry = document.createElement('div');
    entry.className = 'log-entry';
    entry.innerHTML = `<span class="timestamp">[${time}]</span> ${message}`;
    log.appendChild(entry);
    log.scrollTop = log.scrollHeight;
    logCount++;
    if (logCount > 50) { log.removeChild(log.firstChild); logCount--; }
}
function clearLog() {
    document.getElementById('consoleLog').innerHTML = '';
    logCount = 0;
    addLog('Log cleared');
}
function formatUptime(ms) {
    const seconds = Math.floor(ms / 1000);
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    if (hours > 0) return `${hours}h ${minutes}m`;
    if (minutes > 0) return `${minutes}m ${secs}s`;
    return `${secs}s`;
}
async function refreshStats() {
    try {
        const res = await fetch('/api/stats');
        const data = await res.json();
        document.getElementById('uptime').textContent = formatUptime(data.uptime);
        document.getElementById('freeHeap').textContent = Math.round(data.freeHeap / 1024);
        document.getElementById('cpuFreq').textContent = data.cpuFreq;
        document.getElementById('flashSize').textContent = Math.round(data.flashSize / (1024 * 1024));
        document.getElementById('apSsid').textContent = data.apSsid;
        document.getElementById('clientCount').textContent = data.clients;
        document.getElementById('ipAddr').textContent = data.ipAddr;
        document.getElementById('macAddr').textContent = data.macAddr;
        const heapPercent = (data.freeHeap / data.totalHeap) * 100;
        document.getElementById('heapProgress').style.width = heapPercent + '%';
    } catch (err) { addLog('⚠️ Failed to fetch stats'); }
}
async function saveWifi() {
    const ssid = document.getElementById('wifiSsid').value;
    const password = document.getElementById('wifiPassword').value;
    if (!ssid) { addLog('❌ SSID cannot be empty'); return; }
    try {
        const res = await fetch('/api/wifi', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid, password })
        });
        if (res.ok) { addLog('✅ WiFi settings saved'); }
        else { addLog('❌ Failed to save WiFi settings'); }
    } catch (err) { addLog('❌ Network error'); }
}
async function saveTimeSettings() {
    const gmtOffset = parseInt(document.getElementById('gmtOffset').value);
    const dstOffset = parseInt(document.getElementById('dstOffset').value);
    const timeMode = parseInt(document.getElementById('timeMode').value);
    try {
        const res = await fetch('/api/time-config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gmtOffset, dstOffset, timeMode })
        });
        if (res.ok) { addLog('✅ Time settings saved'); }
        else { addLog('❌ Failed to save time settings'); }
    } catch (err) { addLog('❌ Network error'); }
}
async function syncTime() {
    addLog('🔄 Syncing time from NTP...');
    try {
        const res = await fetch('/api/sync-time', { method: 'POST' });
        if (res.ok) { addLog('✅ Time synchronized from NTP'); }
        else { addLog('❌ Time sync failed'); }
    } catch (err) { addLog('❌ Network error'); }
}
async function syncTimeFromBrowser() {
    const now = new Date();
    const timeData = {
        year: now.getFullYear(),
        month: now.getMonth() + 1,
        day: now.getDate(),
        hour: now.getHours(),
        minute: now.getMinutes(),
        second: now.getSeconds()
    };
    const timeString = now.toLocaleString();
    addLog(`📱 Syncing time from browser: ${timeString}`);
    try {
        const res = await fetch('/api/sync-time-manual', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(timeData)
        });
        if (res.ok) { addLog('✅ Time synchronized from your device'); }
        else { addLog('❌ Failed to sync time'); }
    } catch (err) { addLog('❌ Network error'); }
}
async function reboot() {
    if (confirm('Reboot the device? The console will disconnect.')) {
        addLog('🔄 Rebooting device...');
        await fetch('/api/reboot', { method: 'POST' });
        setTimeout(() => { addLog('⚠️ Connection lost - device rebooting'); }, 1000);
    }
}
async function clearPrefs() {
    if (confirm('Clear all preferences? This cannot be undone.')) {
        addLog('🗑️ Clearing preferences...');
        await fetch('/api/clear-prefs', { method: 'POST' });
        addLog('✅ Preferences cleared');
    }
}
function exitConsole() {
    if (confirm('Exit console mode and return to WiFi mode?')) {
        addLog('⬅️ Exiting console mode...');
        window.location.href = '/api/exit-console';
    }
}
refreshStats();
setInterval(refreshStats, 3000);
addLog('🚀 Dashboard loaded');
</script>
</body>
</html>
)HTML";

// HTTP Handler for main dashboard
static esp_err_t main_dashboard_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_dashboard, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP Handler for /api/stats
static esp_err_t api_stats_handler(httpd_req_t *req)
{
    uint32_t uptime = esp_timer_get_time() / 1000; // milliseconds
    size_t freeHeap = esp_get_free_heap_size();
    size_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_get_config(&conf);
    uint32_t cpuFreq = conf.freq_mhz;
    uint32_t flashSize = 0;
    esp_flash_get_size(NULL, &flashSize);

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ap_netif, &ip_info);

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);

    wifi_sta_list_t sta_list;
    esp_wifi_ap_get_sta_list(&sta_list);

    char json[512];
    snprintf(json, sizeof(json),
             "{\"uptime\":%lu,\"freeHeap\":%u,\"totalHeap\":%u,\"cpuFreq\":%lu,"
             "\"flashSize\":%lu,\"apSsid\":\"%s\",\"clients\":%d,"
             "\"ipAddr\":\"" IPSTR "\",\"macAddr\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}",
             uptime, freeHeap, totalHeap, cpuFreq, flashSize, AP_SSID, sta_list.num,
             IP2STR(&ip_info.ip), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Helper to parse JSON (simple string search)
static bool parse_json_string(const char *json, const char *key, char *out, size_t out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *start = strstr(json, search);
    if (!start)
        return false;

    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end)
        return false;

    size_t len = end - start;
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static int parse_json_int(const char *json, const char *key)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start)
        return 0;

    start += strlen(search);
    return atoi(start);
}

// HTTP Handler for /api/wifi
static esp_err_t api_wifi_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    char ssid[33], password[65];
    if (!parse_json_string(content, "ssid", ssid, sizeof(ssid)))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID");
        return ESP_FAIL;
    }
    parse_json_string(content, "password", password, sizeof(password));

    save_network_prefs(ssid, password);

    ESP_LOGI(TAG, "WiFi configured: %s", ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP Handler for /api/time-config
static esp_err_t api_time_config_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    int gmtOffset = parse_json_int(content, "gmtOffset");
    int dstOffset = parse_json_int(content, "dstOffset");
    int timeMode = parse_json_int(content, "timeMode");

    g_settings.set_gmt_offset(gmtOffset, true);
    g_settings.set_daylight_offset(dstOffset, true);
    g_settings.set_network_time_mode(timeMode, true);

    ESP_LOGI(TAG, "Time settings updated");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP Handler for /api/sync-time
static esp_err_t api_sync_time_handler(httpd_req_t *req)
{
    // Get timezone offsets from settings
    long gmt_offset = g_settings.get_gmt_offset_sec();
    int daylight_offset = g_settings.get_daylight_offset_sec();
    initialize_network_time(gmt_offset, daylight_offset);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP Handler for /api/sync-time-manual
static esp_err_t api_sync_time_manual_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    int year = parse_json_int(content, "year");
    int month = parse_json_int(content, "month");
    int day = parse_json_int(content, "day");
    int hour = parse_json_int(content, "hour");
    int minute = parse_json_int(content, "minute");
    int second = parse_json_int(content, "second");

    set_manual_time(year, month, day, hour, minute, second);

    ESP_LOGI(TAG, "Time set manually: %d-%d-%d %d:%d:%d",
             year, month, day, hour, minute, second);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP Handler for /api/reboot
static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"rebooting\"}", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
}

// HTTP Handler for /api/clear-prefs
static esp_err_t api_clear_prefs_handler(httpd_req_t *req)
{
    nvs_handle_t handle;

    nvs_open("settings", NVS_READWRITE, &handle);
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    nvs_open("time", NVS_READWRITE, &handle);
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Preferences cleared");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP Handler for /api/exit-console
static esp_err_t api_exit_console_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "Exiting console mode...", HTTPD_RESP_USE_STRLEN);
    g_settings.set_network_mode(0, true);
    return ESP_OK;
}


// Initialize console
void initialize_console(void)
{
    // Start AP mode
    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.ap.ssid, AP_SSID, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, AP_PASS, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen(AP_SSID);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Get IP
    esp_netif_ip_info_t ip_info;
    ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_get_ip_info(ap_netif, &ip_info);

    snprintf(consoleIp, 16, IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Access Point Started. IP: %s", consoleIp);

    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.stack_size = 8192;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Register URI handlers
        httpd_uri_t uri_root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = main_dashboard_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_root);

        httpd_uri_t uri_stats = {
            .uri = "/api/stats",
            .method = HTTP_GET,
            .handler = api_stats_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_stats);

        httpd_uri_t uri_wifi = {
            .uri = "/api/wifi",
            .method = HTTP_POST,
            .handler = api_wifi_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_wifi);

        httpd_uri_t uri_time_config = {
            .uri = "/api/time-config",
            .method = HTTP_POST,
            .handler = api_time_config_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_time_config);

        httpd_uri_t uri_sync_time = {
            .uri = "/api/sync-time",
            .method = HTTP_POST,
            .handler = api_sync_time_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_sync_time);

        httpd_uri_t uri_sync_time_manual = {
            .uri = "/api/sync-time-manual",
            .method = HTTP_POST,
            .handler = api_sync_time_manual_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_sync_time_manual);

        httpd_uri_t uri_reboot = {
            .uri = "/api/reboot",
            .method = HTTP_POST,
            .handler = api_reboot_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_reboot);

        httpd_uri_t uri_clear_prefs = {
            .uri = "/api/clear-prefs",
            .method = HTTP_POST,
            .handler = api_clear_prefs_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_clear_prefs);

        httpd_uri_t uri_exit = {
            .uri = "/api/exit-console",
            .method = HTTP_GET,
            .handler = api_exit_console_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_exit);

        consoleStarted = true;
        ESP_LOGI(TAG, "Enhanced Web Console Ready");
    }
}

void handle_wifi_console(void)
{
    if (g_settings.get_network_mode() == 1)
    {
        if (!consoleStarted)
        {
            ESP_LOGI(TAG, "Initializing Enhanced WiFi Console...");
            initialize_console();
        }
        // ESP-IDF HTTP server handles requests automatically
    }
}

void stop_console(void)
{
    if (server)
    {
        httpd_stop(server);
        server = NULL;
    }
}

void start_console(void)
{
    if (consoleStarted && !server)
    {
        initialize_console();
    }
    else if (!consoleStarted)
    {
        initialize_console();
    }
}