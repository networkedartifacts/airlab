# Data

Recorded sensor data is stored in binary files under `/int/data/`, named by a persistent counter (`file-0001.bin`, `file-0002.bin`, ...). Each file is a fixed header (`dat_head_t`) followed by a flat array of samples (`al_sample_t`). All values are little-endian and the structs are packed (no padding).

## Header

The header is 412 bytes:

| Offset | Size | Field     | Description                                          |
|--------|------|-----------|------------------------------------------------------|
| 0      | 4    | `magic`   | `0x42414C41`, the bytes `"ALAB"`                     |
| 4      | 2    | `version` | Format version, currently `1`                        |
| 6      | 2    | `num`     | File number (persistent counter, also the filename)  |
| 8      | 8    | `start`   | Recording start, int64 ms since the Unix epoch       |
| 16     | 396  | `marks`   | 99 x int32 mark offsets in ms since start            |

The `marks` array holds user-set markers and is zero-terminated: entries are filled in order, and the first zero entry ends the list.

## Samples

Each sample is 18 bytes:

| Offset | Size | Field | Encoding                                             |
|--------|------|-------|------------------------------------------------------|
| 0      | 4    | `off` | int32 ms since `start` (max ~24 days)                |
| 4      | 2    | `co2` | int16 ppm                                            |
| 6      | 2    | `tmp` | int16 °C x 100                                       |
| 8      | 2    | `hum` | int16 % rH x 100                                     |
| 10     | 2    | `voc` | int16 gas index + flags (see below)                  |
| 12     | 2    | `nox` | int16 gas index + flags (see below)                  |
| 14     | 2    | `prs` | int16 hPa (as measured; see below)                   |
| 16     | 2    | `pm`  | int16 PM2.5 µg/m³ x 10 + flags (see below)           |

### Gas Fields

The `voc` and `nox` fields store the gas index in the lower bits and flags in the upper bits:

| Mask     | Meaning                                                        |
|----------|----------------------------------------------------------------|
| `0x03FF` | Index value: 1-500, `0` = no reading available                 |
| `0x0400` | Sampled while the SGP41 was duty-cycled                        |
| `0x0800` | Gas index algorithm still in its initial learning phase        |

### PM Field

A negative `pm` value (`-1`) means no reading is available (no sensor, or none taken yet). Otherwise, the field stores the value in the lower bits and flags in the upper bits:

| Mask     | Meaning                                                        |
|----------|----------------------------------------------------------------|
| `0x3FFF` | Value x 10: 0-1638.3 µg/m³                                     |
| `0x4000` | Sensor obstructed: reading recorded, but its value unreliable  |

### Pressure

The `prs` field stores the pressure as measured. Consumers (on-device display and CSV export) divide it by a barometric factor derived from the configured altitude to obtain sea-level equivalent pressure.

## Versioning

The magic and version fields were introduced together with the PM2.5 field; earlier files start directly with the file number and have 16-byte samples. On startup, the firmware skips files whose magic or version do not match as "foreign", leaving them untouched on flash: recordings from other firmware versions survive but are not listed.

## CSV Export

Files export to `/ext/export/file-XXXX.csv` with the header `time,co2,tmp,hum,voc,nox,prs,pm` and one row per sample:

- `time` — `start + off`, in ms since the Unix epoch.
- `tmp`, `hum` — two decimals; `pm` — one decimal; others — whole numbers.
- `voc`, `nox`, `pm` — flags are masked out; the column is left empty when no reading is available.
