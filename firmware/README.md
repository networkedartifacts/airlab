# Firmware

The Air Lab firmware runs on the [NAOS](https://github.com/256dpi/naos) framework, which provides the `naos` CLI tool for managing the firmware project and the NAOS ESP-IDF component to ease development.

> **Note:** The firmware is best modified directly in this tree. Creating a standalone firmware project and linking the board support library separately is not supported at the moment.

**Target hardware:** ESP32-S3 with 16 MB flash, octal SPIRAM, 296x128 e-paper display, SCD41 (CO2), SGP41 (VOC/NOx), LPS22 (pressure) sensors, capacitive touch (CY8CMBR3108), accelerometer (FXLS8974CF), RTC (BQ32000), 1500 mAh LiPo with charger (BQ25601), USB-C.

## Prerequisites

- **NAOS** — Install the [NAOS CLI](https://github.com/256dpi/naos) by downloading the latest binary from the [releases page](https://github.com/256dpi/naos/releases) and placing it in your `$PATH`. It manages ESP-IDF and the Xtensa/RISC-V toolchains. The SDK version is pinned in [`naos.json`](naos.json).

## Build & Flash

All commands below assume you are in the `firmware/` directory.

First, install the SDK and toolchain (only needed once):

```bash
naos install
```

Then build the firmware:

```bash
naos build
```

Connect a device via USB-C and flash:

```bash
naos flash
```

To attach to the device after flashing:

```bash
naos attach
```

Build, flash, and attach can be combined:

```bash
naos run
```

## Source Structure

All application source lives in [`src/`](src/):

| Module | Description |
|--------|-------------|
| `main` | Entry point — initializes NAOS, defines parameters, runs the setup task. |
| `sig`  | Signal/event system — dispatches and awaits button presses, sensor readings, touch, motion, and plugin events. |
| `hmi`  | Human-machine interface — manages button input, repeat behavior, and input flags. |
| `gfx`  | Graphics — initializes the LVGL display driver, manages begin/end render cycles. |
| `scr`  | Screen — top-level screen state machine that drives all UI flows (measurement, analysis, settings, plugins, etc.). |
| `gui`  | GUI primitives — modal dialogs, confirmations, lists, progress bars, and wheel pickers built on LVGL. |
| `lvx`  | LVGL extensions — custom widgets (bar, chart, bubble, wheel, status), sprite decoder, and helpers. |
| `dat`  | Data — manages recorded measurement files on internal/external storage (create, append, load, import/export). |
| `rec`  | Recorder — background recording task that periodically samples sensors and appends to the active data file. |
| `com`  | Communication — handles serial protocol commands for plugin management and log streaming. |
| `eng`  | Engine — plugin manager that enumerates, loads, and runs WASM plugins via WAMR. |
| `stm`  | Statements — rule-based air quality feedback system with localized messages and moods. |
| `fnt`  | Fonts — embedded bitmap font data. |
| `img`  | Images — embedded image/sprite data for the UI. |

## Board Support Library

The hardware abstraction layer lives in [`lib/al/`](lib/al/) and exposes a clean C API for all board peripherals:

| Header | Abstraction |
|--------|-------------|
| `core.h` | System initialization, reset trigger detection, memory allocation. |
| `power.h` | Battery level/voltage, USB power, charging state. |
| `sensor.h` | CO2, temperature, humidity, VOC, NOx, pressure readings and sample rates. |
| `storage.h` | Internal (LittleFS) and external (SD/FAT) file storage, USB mass storage. |
| `store.h` | Long-term measurement store on external storage. |
| `epd.h` | E-ink display driver. |
| `buttons.h` | Five-button input with debounce. |
| `touch.h` | Capacitive touch slider. |
| `accel.h` | Accelerometer for orientation and motion detection. |
| `buzzer.h` | Piezo buzzer for tones and beeps. |
| `led.h` | Status LED control. |
| `clock.h` | RTC and time synchronization. |
| `sample.h` | Measurement sample type definitions. |

## Hardware Notes

- **Board Revision R3:** Disable `CONFIG_SPIRAM_MODE_OCT` in `naos.json` overrides. R3 boards use quad-mode SPIRAM instead of octal.
