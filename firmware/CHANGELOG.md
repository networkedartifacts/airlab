# Changelog

This changelog lists notable user-facing changes per release. Minor changes,
internal refactoring and development tooling are omitted.

## Unreleased

**Highlights**

- **Particulate matter sensing:** full support for the Bosch BMV080 PM2.5 sensor, from measurement to on-device screens and Home Assistant
- **Longer battery life:** automatic light sleep and extensive power optimizations across sensors, display and radios
- **Over-the-air updates:** a new partition layout paves the way for OTA firmware updates

**Features**

- Support BMV080 particulate matter (PM2.5) sensor with configurable rate
- Show PM2.5 on the idle, history, lab and live view screens
- Announce PM sensor to Home Assistant
- Support LIS2DH12 accelerometer via runtime chip detection
- Run SGP41 continuously by default, duty cycling opt-in via gas-window
- Support disabling the SGP41 via a negative gas-window
- Support freely configurable display and sensor rates
- Support full sensor power off
- Added French translation
- Use the device ID as USB serial number
- Changed partition layout (enables OTA updates)

**Improvements**

- Upgraded to NAOS v0.16.0 (ESP-IDF 5.5.5)
- Enabled automatic light sleep
- Reduced sleep power draw through improved RTC, touch controller, SGP41 and SCD41 power management, and selectable ULP use during deep sleep
- Replay sparse samples to stabilize gas indices after wake
- Draw degraded gas values as dotted bars and unavailable values as "n/a"
- Start radios only when fully awake and use slower, adaptive BLE advertising
- Retry failed I2C transfers
- More robust USB mass storage (PSRAM-backed disk, on-demand allocation, cooperative shutdown)
- Safer RTC handling and RTC verification during self check
- Improved Spanish translations and shortened others

**Bug Fixes**

- Fixed charge detection (falsely showing charging icon despite being fully charged)
- Fixed several data storage, history query and clock stability issues
- Hide connectivity icons when going to sleep

## v0.11.2 (2026-04-25)

**Improvements**

- Added injectable `SIG_REFRESH` signal to immediately apply idle screen changes

**Bug Fixes**

- Fixed stale plugin content remaining on screen when switching from a custom idle screen back to the default

## v0.11.1 (2026-04-24)

**Bug Fixes**

- Fixed crash when sensor reads failed
- Fixed bug when plugins launched on the idle screen

## v0.11.0 (2026-04-24)

**Features**

- Support on-device altitude configuration
- Use Altitude to show air pressure at sea level (QNH)
- Expose altitude to plugins
- Expose raw VOX/NOx values to plugins
- Support SNTP based time synchronization (NAOS)
- Support timezone in configuration

**Improvements**

- Upgraded to NAOS v0.15.0
- Treat RTC time as UTC time
- Reduced timer task blocking
- Improved littleFS performance (MMAP)

**Bug Fixes**

- Update screen every minute even when powered
- Fixed MQTT reconnect deadlock (NAOS)
- Improved WiFi and BLE stability (NAOS)

## v0.10.1 (2026-04-09)

**Bug Fixes**

- Fixed Bundle Parsing Bug
- Fixed Too Short Graphics Wait Timeout

## v0.10.0 (2026-04-01)

**Features**

- Idle Screen: Plugin-based customizable screens with auto and manual-cycling
- Plugins: Screen examples
- API: Power state parameter
- Engine: New plugin API (clock, store, arc, sample offset access)
- Engine: Expose Fahrenheit setting, connectivity status and events to plugins
- Engine: Modern ALB bundle format
- Engine: Plugin settings support
- Engine: Plugin permissions
- CLI: Many more commands

**Improvements**

- UI: Updated BLE pairing screen
- UI: Updated radio icons
- UI: Improved bar and status bar layout
- UX: Re-launch screens on sensor and motion signals
- UX: Improved USB port stability and speed (NAOS)

**Bug Fixes**

- Fixed GFX wait deadlock
- Fixed deep sleep return handling
- Fixed screen index wrapping

## v0.9.0 (2026-03-19)

**Features**

- **BLE Bonding:** support for proper BLE bonding with encryption
- **Configurable sleep & BLE:** configurable sleep deferral and BLE pairing/bonding settings
- **Home Assistant integration:** expose battery and power info to HA
- **CO2 light control:** support disabling the CO2 indicator light
- **Clock calibration:** added RTC clock calibration with a configuration menu for fine-tuning

**Improvements**

- Periodic full display refresh when idling
- Cache measurement samples and show modal during long loads

**Bug Fixes**

- Guard against idle screen crash
- Use reentrant APIs for time/date handling
