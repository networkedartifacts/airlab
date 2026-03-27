# Files

The device uses two storage partitions:

- **Internal** (`/int`): LittleFS partition for firmware data and plugins.
- **External** (`/ext`): FAT32 partition exposed via USB for data export.

## Layout

| Path                  | Purpose                                       |
|-----------------------|-----------------------------------------------|
| `/int/tmp/`           | Staging area for file uploads                 |
| `/int/data/`          | Recorded sensor data files (`.bin`)           |
| `/int/engine/`        | Plugin bundles (`.alp`) and configs (`.alc`)  |
| `/int/config/`        | Device configuration bundles                  |
| `/int/data/{plugin}/` | Per-plugin persistent data storage            |
| `/ext/export/`        | Exported sensor data (`.csv`)                 |
| `/ext/dump/`          | Display frame dumps (`.bin`)                  |

## Staged Uploads

Files are first uploaded to `/int/tmp/` under a random name (12 hex characters), then renamed to their final destination. This ensures that consumers only ever see complete files, even during concurrent access or power loss.

## Sensor Data

Sensor data is recorded to `/int/data/` as binary files named `file-0000.bin` through `file-0127.bin` (up to 128 files). Each file contains a header (`dat_head_t`) followed by an array of samples with CO2, temperature, humidity, VOC, NOX, and pressure values.

Data files can be exported to `/ext/export/` as CSV files (e.g. `file-0001.csv`) with columns: `time,co2,tmp,hum,voc,nox,prs`.

## Plugins

Plugin bundles (`.alp` files) are stored in `/int/engine/`. Each plugin may have an accompanying configuration file (`{name}.alc`) in the same directory. Plugins with storage permission can persist arbitrary data under `/int/data/{plugin}/`.

## Screen Configuration

The idle screen configuration is stored as `/int/config/screens.alb`, a bundle that defines which plugins to run on the idle screen carousel and their parameters.

## Display Dumps

When the `gfx-record` parameter is enabled, each display frame is written to `/ext/dump/` as `screen-{timestamp}.bin` containing raw EPD frame data.
