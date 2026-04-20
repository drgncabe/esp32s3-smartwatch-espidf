# ESP32-S3 Smartwatch Firmware (ESP-IDF)
> ESP32-S3 1.83", ESP32-S3 1.69" (V1 & V2) Touch LCD

| | |
|---|---|
| **Written by** | Noah Clark |
| **Date Created** | 2024-07-01 |
| **Last Updated** | 2025-01-23 |
| **Language** | C/C++ (ESP-IDF framework, no Arduino) |
| **Target Platform** | ESP32-S3 (1.83" and 1.69" Touch LCD boards) |
| **Build System** | PlatformIO CLI |
| **Development Environment** | macOS Sequoia 15.2, MacBook M2 Pro 2023 14-inch |
| **External Hardware** | ESP32-S3 1.83" Touch LCD, ESP32-S3 1.69" Touch LCD (V1 and V2) |

Custom firmware for ESP32-S3 based smartwatch hardware, built on the ESP-IDF framework with LVGL for the user interface. This project targets small touchscreen LCD boards (1.69" and 1.83" variants) and turns them into functional, WiFi-capable wristwatches with weather, time sync, hotspot sharing, a built-in web console, and network scanning tools. Built and flashed with PlatformIO and requires no Arduino dependencies.

I will note that some of this documentation is spotty, as this repo is a bit older. I had other plans for this project, but have decided to make it open source and add it to the books. Another thing to keep in mind, this is built on the ESP-IDF framework (rather than Arduino), so some of this code may feel a little confusing if you're used to Arduino. Nonetheless, this was an aboslute blast to build out and the following consolidation of my notes should make things easier.

A few of my favorite features:
- OpenTime Sync 
  - This was an idea I had to work around not having an RTC battery to keep time while the device is off. These boards do have a spot for the RTC battery, but I wanted to keep things compact as I was 3D printing a sleek case/band combo for it.
- NAPT Hotspot
  - This is one of my favorites because of how challenging it was to get working. It doesn't look like a lot when you check the code out, but getting it into a stable and ready-to-use position was a pain.

I did end up trimming a lot of pieces out of this codebase, I had initially implemented a TON of working pentesting tools for my own research purposes, but I figured making this public with those tools still fully implemented was not a realm I wanted to enter. Anyways, enjoy!

> If you are planning to build and flash this project, I highly recommend reading and following this documentation.

---

## Table of Contents

- [Supported Hardware](#supported-hardware)
- [Getting Started](#getting-started)
- [PlatformIO CLI Setup](#platformio-cli-setup)
  - [macOS](#macos)
  - [Windows](#windows)
- [Build and Flash](#build-and-flash)
- [Project Structure](#project-structure)
- [Configuration](#configuration)
  - [Board Selection](#board-selection)
  - [Network Settings](#network-settings)
  - [Time and Timezone](#time-and-timezone)
  - [Touch Calibration](#touch-calibration)
  - [Battery Thresholds](#battery-thresholds)
- [Features](#features)
  - [Open Network Sync](#open-network-sync)
  - [Open Time Sync](#open-time-sync)
  - [Time Caching (No RTC Battery Needed)](#time-caching-no-rtc-battery-needed)
  - [WiFi Console](#wifi-console)
  - [Hotspot with Internet Sharing (NAT)](#hotspot-with-internet-sharing-nat)
  - [Weather](#weather)
  - [Bluetooth Scanner](#bluetooth-scanner)
  - [Heartbeat Sensor (Proximity Radar)](#heartbeat-sensor-proximity-radar)
  - [Power Management](#power-management)
- [Crash Decoding](#crash-decoding)
- [Custom Fonts and Icons](#custom-fonts-and-icons)
- [Partition Table](#partition-table)
- [Notes and Gotchas](#notes-and-gotchas)

---

## Supported Hardware

The firmware currently supports three board variants, all based on the ESP32-S3:

| Board ID | Display | Status |
|----------|---------|--------|
| `BOARD_ESP32_S3_183` | 1.83" 240x284 | Fully supported. Uses AXP2101 PMIC. |
| `BOARD_ESP32_S3_169_V2` | 1.69" 240x280 | Fully supported. Uses ADC for battery. |
| `BOARD_ESP32_S3_169_V1` | 1.69" 240x280 | Partially supported. Touch driver and PMIC adjustments still needed. |

All boards use an SPI-connected LCD, an I2C CST816 capacitive touch controller, and a LiPo battery. The 1.83" board has an AXP2101 power management IC which gives more reliable battery readings and hardware charging detection. The 1.69" boards rely on the ESP32's ADC with a voltage divider, which works but is a bit noisier.

You select which board you are building for in `app_config.h`. More on that below.

---

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or the VS Code / Cursor extension)
- An ESP32-S3 board from the supported list above
- A USB-C cable for flashing

### Clone and Open

```bash
git clone <repo-url>
cd esp32s3-smartwatch-espidf
```

Open the folder in VS Code or Cursor. PlatformIO should detect the project automatically from `platformio.ini`.

### Editor

I use and recommend VS Code (or Cursor, which is what I actually use day to day) for working on this project. It handles the ESP-IDF include paths and PlatformIO integration well once things are set up correctly.

If IntelliSense is lighting up with a ton of errors after you first open the project, do not panic. It almost always means the editor has not picked up the right include paths yet. Run these two commands to regenerate the compile database and VS Code project files:

```bash
pio run -t compiledb
pio project init --ide vscode
```

If that does not fix it, try resetting the IntelliSense database: open the command palette (`Cmd+Shift+P` on Mac, `Ctrl+Shift+P` on Windows) and run `C/C++: Reset IntelliSense Database`. Then reload the window. Between those steps, the false errors should clear up.

If you still see issues, make sure `c_cpp_properties.json` in your `.vscode` folder is pointing to the right toolchain paths for your system. PlatformIO generates this file for you with `pio project init --ide vscode`, so running that again is usually the fix.

If you do not have PlatformIO set up yet, skip ahead to the next section first.

---

## PlatformIO CLI Setup

This project is meant to be built and flashed entirely from the command line using PlatformIO Core (the CLI). You do not need the VS Code PlatformIO extension or any IDE plugin. Everything runs through `pio` commands in a terminal.

I develop on macOS and would always recommend doing the same if you have the option. The setup is cleaner, the toolchain just works, and you will run into fewer path and driver issues. That said, Windows instructions are included below if that is what you have.

For a more detailed walkthrough covering ESP-IDF CLI setup alongside PlatformIO, see `cli_setup.md` in this repo.

### macOS

Install Homebrew if you do not already have it, then install the prerequisites and PlatformIO itself:

```bash
brew update
brew install git cmake ninja python
brew install platformio
```

That is it. Verify the install:

```bash
pio --version
pio system info
```

If you prefer to keep PlatformIO isolated from your system Python, you can use `pipx` instead of the Homebrew formula:

```bash
brew install pipx
pipx ensurepath
pipx install platformio
```

Restart your terminal after `pipx ensurepath` so the PATH update takes effect.

One thing to watch out for on macOS: if your board does not show up when you try to upload, make sure your USB cable actually supports data (not just charging). You can check what the system sees with `pio device list` or `ls /dev/tty.*`.

### Windows

I do not develop on Windows, so your mileage may vary, but the general steps are:

1. Install Git, Python 3, and optionally CMake via `winget` or manually:

```powershell
winget install Git.Git
winget install Python.Python.3
```

2. Install PlatformIO Core via `pipx` (cleanest approach):

```powershell
python -m pip install --upgrade pip
python -m pip install --user pipx
python -m pipx ensurepath
pipx install platformio
```

Close and reopen PowerShell, then verify with `pio --version`.

If `pio` is not recognized after install, make sure your Python Scripts folder is on your PATH. It is usually something like `%APPDATA%\Python\Python3x\Scripts`.

For USB driver issues on Windows, check Device Manager under "Ports (COM & LPT)" and install the appropriate USB-to-UART driver for your board if needed.

---

## Build and Flash

If you're on mac you can use the "autorun.sh" script in the project root to build, upload and monitor by executing ```./autorun.sh```. I prefer to do things that way, but it's all the same.

### Standard Build

```bash
pio run -e esp-smartwatch
```

### Build and Upload

```bash
pio run -e esp-smartwatch && pio run -e esp-smartwatch -t upload
```

### Monitoring Serial Output

```bash
pio device monitor
```

The monitor runs at 115200 baud and includes the ESP32 exception decoder filter, so backtraces are automatically symbolized when possible.

### Full Erase (Required After Partition Changes)

If you change `partitions/partitions.csv`, you need to erase the flash completely before uploading again. Otherwise the old partition table will conflict with the new one.

```bash
pio run -t erase
pio run -t upload
```

For a hard erase when things are really stuck:

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbmodem101 erase_flash
```

Replace the port with whatever your board enumerates as.

---

## Project Structure

```
src/
  main.cpp                     Entry point (app_main)
  core/
    boot/boot.cpp              Power latch, button handling
    display/
      lcd_169_drv.cpp          SPI LCD driver, LVGL initialization
      cst_816_drv.cpp          CST816 touch driver (I2C)
    network/
      wifi_init.cpp            WiFi STA connection, startup scan, mode switching
      time_init.cpp            SNTP sync, time caching to NVS
      console_init.cpp         Web console (AP mode + HTTP server)
      napt_interface.cpp       Hotspot with NAT internet sharing
    power/power_mgmt.cpp       Battery voltage, charging detection, power off
    store/nvs_fs.cpp           NVS initialization
    memory/memory_manager.cpp  Heap utilities
  network/
    modules/
      wifi_scanner.cpp         WiFi AP scanning
      open_scanner.cpp         Open network discovery and auto-connect
      bluetooth_scanner.cpp    BLE device scanning
      heartbeat_sensor.cpp     Proximity/device tracking via WiFi
    utility/
      wifi_tools.cpp           Raw 802.11 frame parsing, OUI vendor lookup
  api/
    prefs.cpp                  NVS read/write helpers
    weather_api.cpp            Open-Meteo weather fetching
    geocode.cpp                Zip code to lat/lon resolution
    gyroscope.cpp              Gyroscope interface
  context/
    app_settings.cpp           Settings management (load, save, apply)
    app_context.cpp            Global app state
    display_controller.cpp     Backlight PWM control
  ui/
    screens/
      home_page.cpp            Main watch face (time, date, weather, battery)
      settings_page.cpp        Settings menu shell
      heartbeat_sensor_page.cpp  Proximity radar UI
      components/settings/     Individual settings groups (network, time, display, etc.)
    dialogs/
      scan_dialog.cpp          WiFi network list and connect dialog
      opentime_dialog.cpp      Open time sync UI
      bluetooth_dialog.cpp     BLE device list
    components/                Toasts, buttons, keyboard, time formatting
  assets/
    fa_weather_24.c            Font Awesome weather icons
  util/
    util.cpp                   Auto-dim, CPU frequency helpers

include/                       Headers mirroring the src/ layout
  configuration/
    app_config.h               Main user-facing configuration
    pin_config.h               GPIO pin mappings per board
    config_types.h             Board type definitions

partitions/partitions.csv      Flash partition table
platformio.ini                 PlatformIO build configuration
lv_conf.h                      LVGL 8 configuration
sdkconfig.defaults             ESP-IDF SDK configuration
decode_crash.sh                Backtrace decoder script
```

---

## Configuration

Almost everything you would want to change lives in `include/configuration/app_config.h`. This is the one file you should look at before building.

### Board Selection

At the top of `app_config.h`:

```c
#define BOARD_TYPE BOARD_ESP32_S3_183
```

Change this to match your hardware:

- `BOARD_ESP32_S3_183` for the 1.83" board (with AXP2101 PMIC)
- `BOARD_ESP32_S3_169_V2` for the 1.69" V2 board
- `BOARD_ESP32_S3_169_V1` for the 1.69" V1 board (supported, but it is likely that you have the V2 board)

This single define controls which pin mapping is used (from `pin_config.h`), whether the AXP2101 PMIC driver is active or the ADC fallback is used, and the LCD resolution.

### Network Settings

```c
#define DEFAULT_HOTSPOT_SSID "ESP32-Hotspot"
#define DEFAULT_HOTSPOT_PASSWORD "12345678"
#define HOTSPOT_CHANNEL 1
#define HOTSPOT_MAX_CONNECTIONS 4

#define AP_CONSOLE_SSID "ESP32-Console"
#define AP_CONSOLE_PASSWORD "12345678"
```

The hotspot settings control the network name and password when the watch acts as a WiFi hotspot (with NAT internet sharing). The console settings control the access point that the web management dashboard runs on.

You will probably want to change the passwords before flashing, especially if you plan on using this around other people.

### Time and Timezone

```c
#define NTP_SERVER "pool.ntp.org"
#define DEFAULT_GMT_OFFSET_SEC -18000
#define DEFAULT_DAYLIGHT_OFFSET_SEC 3600
```

The GMT offset is in seconds. `-18000` is US Eastern time (UTC-5). The daylight offset of `3600` adds one hour during DST. These defaults are used on first boot and can be changed later through the settings UI or the web console.

Some common GMT offsets:

| Timezone | Offset (seconds) |
|----------|-------------------|
| US Eastern (UTC-5) | -18000 |
| US Central (UTC-6) | -21600 |
| US Mountain (UTC-7) | -25200 |
| US Pacific (UTC-8) | -28800 |
| UTC | 0 |
| Central Europe (UTC+1) | 3600 |

### Touch Calibration

```c
#define TOUCH_OFFSET_X 0
#define TOUCH_OFFSET_Y -30
#define TOUCH_SCALE_X 1.0f
#define TOUCH_SCALE_Y 1.0f
```

These are already set per board type with recommended values. If taps are registering in the wrong spot on your particular unit, you can adjust:

- If taps register too high, increase `TOUCH_OFFSET_Y`
- If taps register too far right, increase `TOUCH_OFFSET_X`
- If taps near the edges of the screen do not register, increase the scale values

### Battery Thresholds

```c
#define BATTERY_FULL_VOLTAGE 4.2f
#define BATTERY_HIGH_VOLTAGE 4.1f
#define BATTERY_LOW_VOLTAGE 3.6f
```

These only matter for the 1.69" boards that use the ADC fallback for battery monitoring (the 1.83" board reads percentage directly from the AXP2101 fuel gauge). Standard LiPo batteries use 4.2V full and 3.0V cutoff. If you are using a high-voltage LiPo, set `BATTERY_FULL_VOLTAGE` to `4.35f` and `BATTERY_HIGH_VOLTAGE` to `4.25f`.

---

## Features

### Open Network Sync

This is one of the more interesting features. When enabled, the watch will periodically scan for open (no password) WiFi networks in range, connect to the strongest one it can find, and use that connection to sync time via NTP and optionally stay connected for weather and other network tasks.

It works by:
1. Scanning all nearby WiFi networks
2. Filtering down to only open (unencrypted) networks
3. Sorting them by signal strength, strongest first
4. Attempting to connect to each one in order
5. If a connection succeeds and the network actually provides internet access (not just a captive portal), it syncs time and optionally stays connected

This runs as a background task and will not interfere with a normal WiFi connection if you already have one. If you are already connected to a saved network, the periodic scanner skips its cycle.

You can enable periodic open network scanning in the watch settings. When enabled, it checks once per minute whether it should attempt a scan. It will only start if WiFi is not already connected and WiFi mode is not set to "off".

### Open Time Sync

Open Time Sync is a focused version of Open Network Sync. Instead of staying connected to the open network, it connects just long enough to pull the current time from an NTP server, caches it, and then disconnects and restores whatever WiFi state you had before.

This is specifically designed to solve the problem of not having an RTC battery. Most of these ESP32 watch boards do include connection for a battery-backed real-time clock, which is what prevents the time from resetting after every reboot. However, I removed those connectors from my board because they were getting in the way of the slim case i was building. Open Time Sync gets around this by opportunistically grabbing the real time from whatever open WiFi happens to be nearby, even if you do not have your home network credentials saved, even if you are out and about.

The flow:
1. The current WiFi state is saved (were you connected to something? which network?)
2. WiFi is reset to STA mode
3. A scan for open networks runs
4. It connects to the strongest open network and syncs time via NTP
5. The synced time is immediately cached to NVS (flash storage)
6. WiFi is restored to whatever state it was in before

You can trigger this manually from the UI, or it can happen automatically as part of the periodic scanner.

### Time Caching (No RTC Battery Needed)

Since these boards lack an RTC battery, the firmware maintains a time cache in NVS (non-volatile storage on the ESP32's flash). Here is how it works:

- Every 30 seconds, the current unix timestamp and the ESP32's internal uptime counter are saved to NVS
- A boot counter is also saved so the firmware can detect fresh reboots vs. same-session updates
- On startup, if no network is available, the firmware loads the last cached time and applies the uptime delta to estimate the current time

If the watch was off for a while (powered down completely), the cached time will be stale by however long it was off. But it is a much better starting point than midnight January 1st, and the next time it sees an open network or connects to your home WiFi, it will correct itself.

The time is also force-cached any time the battery gets low or the watch is about to power off, so you lose as little accuracy as possible.

### WiFi Console

Switching the network mode to "Console" in the settings turns the watch into a WiFi access point running a web-based management dashboard. Connect to the AP from your phone or laptop (default SSID: `ESP32-Console`, password: `12345678`) and navigate to `192.168.4.1` in a browser.

The console lets you:
- View system stats (uptime, free heap, CPU frequency, flash size)
- See connected clients and network info
- Save WiFi credentials so the watch can connect to your home network
- Configure timezone and NTP settings
- Sync time from NTP or directly from your phone/browser
- Reboot the watch
- Clear all saved preferences
- Exit console mode and return to normal WiFi mode

This is the easiest way to do initial setup if you do not want to hard-code your WiFi credentials. Flash the firmware, switch to console mode, connect from your phone, enter your WiFi details, and switch back.

### Hotspot with Internet Sharing (NAT)

The watch can act as a WiFi hotspot that shares its internet connection with other devices. This uses lwIP's NAPT (Network Address Port Translation) to route traffic from hotspot clients through the ESP32's existing WiFi connection to the internet.

When enabled:
- The ESP32 switches to APSTA mode (simultaneous access point and station)
- A separate AP network is created on 192.168.4.0/24
- NAT is enabled to translate packets between the AP and STA interfaces
- A DNS forwarder runs on port 53 so clients get automatic DNS resolution

Devices connected to the hotspot get full internet access routed through the watch. This is useful if you need to share a WiFi connection with a device that cannot connect directly, or if you want a quick portable hotspot.

The hotspot SSID and password are configurable in `app_config.h` or through the settings UI.

### Weather

Weather data is fetched from the Open-Meteo API using the geocode (latitude/longitude) derived from your zip code. The zip code is configured in settings, and the firmware uses the Zippopotam.us API to resolve it to coordinates, which are then cached in NVS so subsequent fetches do not need the geocode step.

Weather is displayed on the home screen and refreshes when the watch first connects to WiFi. The weather API response is parsed manually (no JSON library) to keep memory usage low.

### Bluetooth Scanner

The BLE scanner uses the ESP32's Bluetooth radio to detect nearby BLE devices. It classifies devices by type (phone, watch, headphones, etc.) based on their advertised service UUIDs and device class. The device list is stored in PSRAM to avoid eating into the main heap.

### Heartbeat Sensor (Proximity Radar)

Despite the name, this is not a medical heart rate sensor. It is a proximity and device tracking feature that uses WiFi scanning to detect nearby devices, estimate their distance based on RSSI, and track how frequently they appear. Devices that show up consistently are marked as "familiar."

It uses the same underlying WiFi scanning infrastructure as the network tools, parsing management frames (beacons, probe requests) to identify devices and their manufacturers via OUI lookup.

### Power Management

The firmware has two layers of power saving:

**Direct Power Save Mode** -- Manually toggled from settings. Aggressively reduces power consumption by:
- Dropping CPU frequency to 80 MHz
- Disabling the gyroscope and haptics
- Dimming the display to near-minimum
- Disabling WiFi (unless the hotspot is active, in which case it uses max modem sleep instead)
- All previous settings are snapshotted in RAM and restored when you turn power save off

**Auto-Lock** -- Triggered automatically after a period of inactivity. Turns off the display, reduces CPU speed, and enables WiFi power save. Waking up (by tapping the screen) restores everything to its previous state.

On the 1.83" board with the AXP2101 PMIC, the firmware can read the hardware fuel gauge for accurate battery percentage and detect charging state directly from the PMIC status register. On the 1.69" boards, battery percentage is estimated from voltage using a LiPo discharge curve approximation, and charging is detected by watching the voltage trend over time (rising voltage indicates charging) plus checking for an active USB serial connection.

When the battery drops below 15%, the firmware force-caches the current time to NVS. When a power-off is triggered (long press on the power button), it caches time, pauses LVGL timers, and powers down WiFi before releasing the power latch or sending the PMIC shutdown command.

---

## Crash Decoding

If the watch crashes and you see a backtrace in the serial output like:

```
0x420d715e:0x3fcebdd0 0x4200bbff:0x3fcebdf0 ...
```

You can decode it to source file and line numbers using the included script:

```bash
./decode_crash.sh 0x420d715e:0x3fcebdd0 0x4200bbff:0x3fcebdf0 0x420086dd:0x3fcebe10
```

The script uses `addr2line` from the Xtensa toolchain against the compiled ELF file.

---

## Custom Fonts and Icons

Weather icons on the home screen use Font Awesome 6 glyphs. If you need to update or add icons:

1. Download the Font Awesome v6 web fonts from [fontawesome.com](https://fontawesome.com/v6/download)
2. Locate `fa-solid-900.ttf` in the web fonts package
3. Use the [LVGL Online Font Converter](https://lvgl.io/tools/fontconverter) with these settings:
   - Range: `0xF000-0xF8FF`
   - BPP: 4
   - Size: 24 (or whatever size you need)
   - Format: LVGL
4. In the generated `.c` file, remove the line `.static_bitmap = 0,` at the bottom (it breaks LVGL 8)
5. Declare the font in your source: `LV_FONT_DECLARE(fa_weather_24);`

---

## Partition Table

The flash is divided as follows (16 MB total):

| Partition | Type | Offset | Size |
|-----------|------|--------|------|
| nvs | NVS data | 0x9000 | 24 KB |
| otadata | OTA data | 0xF000 | 8 KB |
| app0 | OTA slot 0 | 0x20000 | ~3.75 MB |
| app1 | OTA slot 1 | 0x3E0000 | ~3.75 MB |
| spiffs | SPIFFS data | 0x7A0000 | 384 KB |

Two OTA app slots are included for future over-the-air update support. The NVS partition stores all user preferences, saved WiFi credentials, cached time, and geocode data.

If you modify `partitions.csv`, you must do a full flash erase before uploading. See the [Build and Flash](#build-and-flash) section.

---

## Notes and Gotchas

- **First boot with no WiFi**: The watch will start with the time set to January 1, 2026 00:00:00. It will correct itself once it gets any network connection (saved WiFi, open network, or manual set via console).

- **PSRAM is expected**: The build flags include `BOARD_HAS_PSRAM`. All supported boards have PSRAM. Large buffers (BLE device lists, scan results) are allocated there to keep the main heap free for LVGL and WiFi.

- **The `--allow-multiple-definition` linker flag**: This is intentional. The firmware overrides `ieee80211_raw_frame_sanity_check` from the WiFi stack to allow raw 802.11 frame handling for the network scanning and device tracking features.

- **Haptics are placeholder**: The haptics toggle exists in settings and the code calls `play_haptic_notification()` in a few places, but the actual motor driver is not yet wired up. It will not crash, it just will not buzz.

- **Console mode does not persist across reboots**: If you set the network mode to "Console" or "WiFi Off", it reverts to WiFi mode on the next boot. This is intentional so you cannot accidentally lock yourself into console-only mode.

- **Weather needs a zip code**: If no zip code is set, weather defaults to a hardcoded location (Mount Pleasant, SC). Set your zip code in settings or through the web console.

- **DNS in hotspot mode**: The hotspot runs its own DNS forwarder that proxies queries to the upstream DNS server (your router's DNS, or 8.8.8.8 as a fallback). This means clients connected to the hotspot do not need any special DNS configuration.
