# Tools

The `airlab` CLI provides plugin, bundle, file, screen, and capture utilities for Air Lab devices.

## Installation

Requires [Go](https://go.dev) 1.23+.

```bash
make install # go install ./airlab
```

## Commands

Commands that talk to a device take an optional `device` argument or `-d`/`--device` flag with the serial device path. If omitted, the first available device is used.

### Plugins

#### `airlab plugin bundle <dir> <output>`

Bundle a plugin directory into an `.alp` file. Reads the `alp.yml` manifest, includes the compiled WASM binary, and packages any declared sprites and config schemas.

```bash
airlab plugin bundle ./hello ./hello/hello.alp
```

#### `airlab plugin upload <input> [device]`

Upload a bundled `.alp` file to a device over USB serial.

```bash
airlab plugin upload ./hello/hello.alp
airlab plugin upload ./hello/hello.alp /dev/ttyUSB0
```

#### `airlab plugin launch <name> [device]`

Launch an uploaded plugin on a device and stream its log output. The `name` is the `.alp` filename as stored on-device.

```bash
airlab plugin launch hello.alp
```

Options:
- `-b`, `--mode` — Mode to launch the plugin in.

#### `airlab plugin config [key=value...]`

Show, set, or delete stored config values for a plugin on a device. Keys are validated against the config schema in the plugin's `alp.yml`. Without arguments, the current config is shown.

```bash
airlab plugin config -C ./hello           # show current values
airlab plugin config -C ./hello mode=fast # set a value
airlab plugin config -C ./hello --delete  # delete the config file
```

Options:
- `-C`, `--dir` — Plugin directory containing `alp.yml` (default: `.`).
- `-d`, `--device` — Serial device path.
- `--delete` — Delete the config file from the device.

#### `airlab plugin monitor [device]`

Stream the engine log from a device without launching a plugin.

```bash
airlab plugin monitor
```

### Bundles

#### `airlab bundle analyze <file>`

Decode a bundle file and print its sections, including attribute contents and nested config schemas.

```bash
airlab bundle analyze ./hello/hello.alp
```

### Files

The `files` commands manage the device file system. Remote paths are relative to the internal storage mount (`/int`); pass `-e`/`--external` to target the external storage mount (`/ext`) instead.

```bash
airlab files upload ./screens.alb /config/screens.alb  # upload a file
airlab files download /data/file-0001.bin ./data.bin   # download a file
airlab files list /engine                              # list a directory
airlab files tree                                      # show the full tree
airlab files remove /engine/hello.alp                  # remove a file
```

### Screens

The `screens` commands manage the idle screen configuration (`/int/config/screens.alb`, see [`docs/screens.md`](../docs/screens.md)). All take `-d`/`--device`.

```bash
airlab screens show                       # show configured screens
airlab screens set 0 clock timezone=UTC   # set entry "0" with config values
airlab screens clear 0                    # clear entry "0"
```

Config value types for `set` are inferred from the given values (bool, int, float, then string).

### Capture

The `capture` commands work with screen capture files and share these options:

- `--scale N` — Pixel scale factor (default: `10`).
- `--grey` — Use a grey color palette instead of black and white.

#### `airlab capture record [device]`

Record screen captures live from a device. Enables the `gfx-record` parameter, continuously downloads captured frames from `/ext/dump/`, and writes them as PNG images until interrupted.

```bash
airlab capture record
```

Options:
- `--once` — Stop after capturing one frame.
- `--raw` — Save raw `.bin` files instead of `.png` images.

#### `airlab capture convert <glob>`

Convert screen capture binary files (`.bin`) to images.

```bash
airlab capture convert "screen-*.bin"
```

Options:
- `--format png|bmp` — Output format (default: `png`).

#### `airlab capture animate <glob>`

Create a GIF animation from a sequence of screen capture binary files. The output is written to `animation.gif` in the current directory.

```bash
airlab capture animate "screen-*.bin"
```

Options:
- `--fast` — Use a fixed short frame delay for faster playback.

## Packages

### `alb`

The [`alb`](alb) Go package implements the generic Air Lab Bundle format used for plugin bundles (`.alp`), config bundles (`.alc`), and the screens configuration (`.alb`).

#### Bundle

The `Bundle` type represents a bundle file as a list of typed sections:

```go
bundle, err := alb.DecodeBundle(data) // parse a bundle file
raw := bundle.Encode()                // serialize to bytes

bundle.AddAttr("name", []byte("my-plugin")) // add a metadata attribute
val := bundle.GetAttr("name")               // read an attribute
```

Each `BundleSection` has a `Type` (`BundleTypeAttr`, `BundleTypeBinary`, `BundleTypeSprite`, or `BundleTypeConfig`), a `Name`, and `Data`. Sections are integrity-checked with CRC32 on decode.

#### Config

The `Config` type models a plugin config schema (sections with typed items, defaults, ranges, and options) as declared in `alp.yml`. It can be encoded to and decoded from a nested bundle (see [`docs/config.md`](../docs/config.md)):

```go
bundle, err := config.Encode()      // encode schema to a bundle
config, err := alb.DecodeConfig(b)  // parse schema back from a bundle
```

Individual values are handled with `EncodeConfigValue` and `DecodeConfigValue`.

#### Sprite

The `Sprite` type handles conversion between PNG images and the 1-bit bitmap format used by the device:

```go
sprite := alb.SpriteFromPNG(pngData, scale) // convert PNG to 1-bit image + mask
encoded := sprite.Encode()                  // serialize for bundling

sprite = alb.DecodeSprite(encoded)          // parse back from binary
```

Sprites are stored as a width/height header followed by two 1-bit bitmaps (image and mask). Transparent PNG pixels are excluded from the mask; non-black opaque pixels become white.

### `alp`

The [`alp`](alp) Go package implements the plugin layer on top of `alb`: it loads and validates `alp.yml` manifests and builds complete plugin bundles.

```go
manifest, err := alp.LoadManifest("./hello")       // load and validate alp.yml
bundle, err := alp.BuildBundle(manifest, "./hello") // build the plugin bundle
```
