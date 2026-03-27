/***************************************************************************************
 *  File        : napt_interface.cpp
 *  Description : ESP32 internet sharing with NAT and DNS forwarding
 *  Author      : Noah Clark
 *  Created     : 2026-01-29
 *--------------------------------------------------------------------------------------
 *  Part of the ESP-32 Smartwatch Firmware
 *--------------------------------------------------------------------------------------
 *  Notes:
 *   - Relevant build flags and sdkconfig settings are needed for this to work properly.
 ***************************************************************************************/


#include <string.h>
#include "configuration/app_config.h"
#include "core/network/napt_interface.h"
#include "core/network/wifi_init.h"
#include "dhcpserver/dhcpserver.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

static const char *TAG = "napt_interface";


static bool hotspot_enabled = false;
static esp_netif_t *ap_netif = NULL;
static bool napt_enabled = false;
static uint32_t napt_address = 0;

static int dns_forwarder_socket = -1;
static TaskHandle_t dns_forwarder_task_handle = NULL;
static ip_addr_t upstream_dns;

extern "C" {
    void ip_napt_enable(uint32_t addr, int enable);
}

// Transparent DNS proxy: forwards queries from AP clients to upstream DNS
static void dns_forwarder_task(void *pvParameters)
{
    static char rx_buffer[512];
    static char tx_buffer[512];
    struct sockaddr_in dest_addr;
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);
    
    ESP_LOGI(TAG, "DNS Forwarder: Starting on port 53");
    
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS Forwarder: Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    
    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(53);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int err = bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "DNS Forwarder: Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "DNS Forwarder: Listening on 0.0.0.0:53");
    ESP_LOGI(TAG, "DNS Forwarder: Forwarding to " IPSTR, IP2STR(&upstream_dns.u_addr.ip4));
    
    dns_forwarder_socket = sock;
    
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    
    while (hotspot_enabled) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, 
                          (struct sockaddr *)&source_addr, &socklen);
        
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            ESP_LOGE(TAG, "DNS Forwarder: recvfrom failed: errno %d", errno);
            break;
        }
        
        if (len > 0) {
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(53);
            dest_addr.sin_addr.s_addr = upstream_dns.u_addr.ip4.addr;
            
            int upstream_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            if (upstream_sock >= 0) {
                struct timeval upstream_timeout;
                upstream_timeout.tv_sec = 2;
                upstream_timeout.tv_usec = 0;
                setsockopt(upstream_sock, SOL_SOCKET, SO_RCVTIMEO, &upstream_timeout, sizeof upstream_timeout);
                
                sendto(upstream_sock, rx_buffer, len, 0, 
                      (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                
                int response_len = recvfrom(upstream_sock, tx_buffer, sizeof(tx_buffer) - 1, 0, NULL, NULL);
                
                if (response_len > 0) {
                    sendto(sock, tx_buffer, response_len, 0, 
                          (struct sockaddr *)&source_addr, socklen);
                }
                
                close(upstream_sock);
            }
        }
    }
    
    close(sock);
    dns_forwarder_socket = -1;
    ESP_LOGI(TAG, "DNS Forwarder: Stopped");
    vTaskDelete(NULL);
}

// Sets up AP in APSTA mode with NAT for internet sharing and a DNS forwarder.
void enable_hotspot(const char *ssid, const char *password)
{
    if (hotspot_enabled)
    {
        ESP_LOGI(TAG, "Hotspot already enabled");
        return;
    }

    if (!wifi_is_connected())
    {
        ESP_LOGE(TAG, "Must be connected to WiFi before enabling hotspot");
        return;
    }

    ESP_LOGI(TAG, "Enabling hotspot: %s", ssid ? ssid : DEFAULT_HOTSPOT_SSID);

    if (ap_netif == NULL)
    {
        ap_netif = esp_netif_create_default_wifi_ap();
        if (ap_netif == NULL)
        {
            ESP_LOGE(TAG, "Failed to create AP network interface");
            return;
        }
        
        esp_netif_dns_info_t dns_info;
        esp_netif_t *sta_netif_for_dns = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif_for_dns != NULL && 
            esp_netif_get_dns_info(sta_netif_for_dns, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK &&
            dns_info.ip.u_addr.ip4.addr != 0)
        {
            ESP_LOGI(TAG, "Using STA's DNS: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }
        else
        {
            IP4_ADDR(&dns_info.ip.u_addr.ip4, 8, 8, 8, 8);
            ESP_LOGI(TAG, "Using fallback DNS: 8.8.8.8");
        }
        
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, 
                               &dns_info.ip.u_addr.ip4.addr, sizeof(dns_info.ip.u_addr.ip4.addr));
        
        esp_netif_dhcps_stop(ap_netif);
        
        esp_netif_ip_info_t ap_ip_config;
        IP4_ADDR(&ap_ip_config.ip, 192, 168, 4, 1);
        IP4_ADDR(&ap_ip_config.gw, 192, 168, 4, 1);
        IP4_ADDR(&ap_ip_config.netmask, 255, 255, 255, 0);
        esp_netif_set_ip_info(ap_netif, &ap_ip_config);
        
        esp_netif_dhcps_start(ap_netif);
        ESP_LOGI(TAG, "AP configured: 192.168.4.1");
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(err));
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));

    wifi_config_t ap_config = {};
    const char *ap_ssid = ssid ? ssid : DEFAULT_HOTSPOT_SSID;
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ap_ssid);
    
    if (password && strlen(password) >= 8)
    {
        strncpy((char *)ap_config.ap.password, password, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    else if (strlen(DEFAULT_HOTSPOT_PASSWORD) >= 8)
    {
        strncpy((char *)ap_config.ap.password, DEFAULT_HOTSPOT_PASSWORD, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    else
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ap_config.ap.channel = HOTSPOT_CHANNEL;
    ap_config.ap.max_connection = HOTSPOT_MAX_CONNECTIONS;
    ap_config.ap.beacon_interval = 100;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Hotspot configured, waiting for AP interface...");
    
    int retry = 0;
    uint32_t ap_addr = 0;
    esp_netif_ip_info_t ap_ip_info;
    
    while (retry < 20)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        
        if (esp_netif_get_ip_info(ap_netif, &ap_ip_info) == ESP_OK)
        {
            ap_addr = ap_ip_info.ip.addr;
            if (ap_addr != 0)
            {
                ESP_LOGI(TAG, "AP interface ready: " IPSTR, IP2STR(&ap_ip_info.ip));
                break;
            }
        }
        retry++;
    }
    
    if (ap_addr == 0)
    {
        ESP_LOGE(TAG, "AP interface failed to get IP address");
        return;
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL)
    {
        ESP_LOGE(TAG, "Failed to get STA network interface");
        return;
    }

    esp_netif_ip_info_t sta_ip_info;
    if (esp_netif_get_ip_info(sta_netif, &sta_ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get STA IP info");
        return;
    }

    uint32_t sta_addr = sta_ip_info.ip.addr;
    if (sta_addr == 0)
    {
        ESP_LOGE(TAG, "STA has no IP (not connected to internet)");
        return;
    }

    ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&sta_ip_info.ip));
    ESP_LOGI(TAG, "AP IP: " IPSTR, IP2STR(&ap_ip_info.ip));

    esp_netif_dns_info_t dns_info;
    ip_addr_t dnsserver;
    
    if (esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK && 
        dns_info.ip.u_addr.ip4.addr != 0)
    {
        dnsserver.u_addr.ip4.addr = dns_info.ip.u_addr.ip4.addr;
        ESP_LOGI(TAG, "Using router DNS: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    }
    else
    {
        dnsserver.u_addr.ip4.addr = htonl(0x08080808);
        ESP_LOGI(TAG, "Using fallback DNS: 8.8.8.8");
    }
    dnsserver.type = IPADDR_TYPE_V4;
    upstream_dns.type = IPADDR_TYPE_V4;
    upstream_dns.u_addr.ip4.addr = dnsserver.u_addr.ip4.addr;
    
    // NAT must be enabled on the AP address, not STA
    if (!napt_enabled || napt_address != ap_addr)
    {
        if (napt_enabled && napt_address != 0)
        {
            ESP_LOGI(TAG, "Disabling old NAT on 0x%08lx", (unsigned long)napt_address);
            ip_napt_enable(napt_address, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        ESP_LOGI(TAG, "Enabling NAT on AP address");
        ip_napt_enable(ap_addr, 1);
        
        napt_enabled = true;
        napt_address = ap_addr;
        
        ESP_LOGI(TAG, "NAT enabled");
    }
    else
    {
        ESP_LOGI(TAG, "NAT already enabled");
    }
    
    hotspot_enabled = true;
    
    if (dns_forwarder_task_handle == NULL)
    {
        xTaskCreate(dns_forwarder_task, "dns_forwarder", 3072, NULL, 5, &dns_forwarder_task_handle);
        ESP_LOGI(TAG, "DNS forwarder started");
    }
    
    ESP_LOGI(TAG, "Hotspot enabled successfully");
    ESP_LOGI(TAG, "SSID: %s", ap_ssid);
    ESP_LOGI(TAG, "Password: %s", ap_config.ap.authmode == WIFI_AUTH_OPEN ? "None (Open)" : "********");
    ESP_LOGI(TAG, "IP Address: 192.168.4.1");
    ESP_LOGI(TAG, "DNS: Automatic (forwarded to " IPSTR ")", IP2STR((ip4_addr_t*)&upstream_dns.u_addr.ip4.addr));
    ESP_LOGI(TAG, "NAT: Enabled (full internet sharing)");
}

void disable_hotspot(void)
{
    if (!hotspot_enabled)
    {
        ESP_LOGI(TAG, "Hotspot already disabled");
        return;
    }

    ESP_LOGI(TAG, "Disabling hotspot...");

    hotspot_enabled = false;
    
    if (dns_forwarder_task_handle != NULL)
    {
        ESP_LOGI(TAG, "Stopping DNS forwarder");
        vTaskDelay(pdMS_TO_TICKS(200));
        
        if (dns_forwarder_socket >= 0)
        {
            close(dns_forwarder_socket);
            dns_forwarder_socket = -1;
        }
        
        dns_forwarder_task_handle = NULL;
        ESP_LOGI(TAG, "DNS forwarder stopped");
    }

    if (napt_enabled && napt_address != 0)
    {
        ESP_LOGI(TAG, "Disabling NAT");
        ip_napt_enable(napt_address, 0);
        napt_enabled = false;
        napt_address = 0;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set STA mode: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Hotspot disabled successfully");
}

bool is_hotspot_enabled(void)
{
    return hotspot_enabled;
}

