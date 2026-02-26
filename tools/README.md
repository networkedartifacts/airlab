# Tools

The `airlab` CLI provides plugin management and screen capture utilities for Air Lab devices.

## Installation

Requires [Go](https://go.dev) 1.23+.

```bash
make install # go install ./airlab
```

## Commands

### `airlab plugin bundle <dir> <output>`

Bundle a plugin directory into an `.alp` file. Reads the `alp.yml` manifest, includes the compiled WASM binary, and packages any declared sprites.

```bash
airlab plugin bundle ./hello ./hello/hello.alp
```

Use `-v` / `--verbose` for detailed section output.

### `airlab plugin upload <input> [device]`

Upload a bundled `.alp` file to a device over USB serial. If `device` is omitted, the first available device is used.

```bash
airlab plugin upload ./hello/hello.alp
airlab plugin upload ./hello/hello.alp /dev/ttyUSB0
```

### `airlab plugin launch <name> [device]`

Launch an uploaded plugin on a device and stream its log output. The `name` is the `.alp` filename as stored on-device.

```bash
airlab plugin launch hello.alp
```

### `airlab capture convert <glob>`

Convert screen capture binary files (`.bin`) to images.

```bash
airlab capture convert "screen-*.bin"
```

Options:
- `--format png|bmp` — Output format (default: `png`).
- `--scale N` — Pixel scale factor (default: `1`).
- `--grey` — Use a grey color palette instead of black and white.

### `airlab capture animate <glob>`

Create a GIF animation from a sequence of screen capture binary files.

```bash
airlab capture animate "screen-*.bin"
```

Options:
- `--scale N` — Pixel scale factor (default: `1`).
- `--grey` — Use a grey color palette.
- `--fast` — Use a fixed short frame delay for faster playback.

The output is written to `animation.gif` in the current directory.

## `alp` Package

The [`alp`](alp) Go package implements the Air Lab Plugin bundle format. It can be used programmatically to create, parse, and inspect `.alp` files.

### Bundle

The `Bundle` type represents an `.alp` file as a list of typed sections:

```go
bundle, err := alp.DecodeBundle(data) // parse an .alp file
raw := bundle.Encode()                // serialize to bytes

bundle.AddAttr("name", []byte("my-plugin")) // add a metadata attribute
val := bundle.GetAttr("name")               // read an attribute
```

Each `BundleSection` has a `Type` (`BundleTypeAttr`, `BundleTypeBinary`, or `BundleTypeSprite`), a `Name`, and `Data`. Sections are integrity-checked with CRC32 on decode.

### Sprite

The `Sprite` type handles conversion between PNG images and the 1-bit bitmap format used by the device:

```go
sprite := alp.SpriteFromPNG(pngData, scale) // convert PNG to 1-bit image + mask
encoded := sprite.Encode()                  // serialize for bundling

sprite = alp.DecodeSprite(encoded)          // parse back from binary
```

Sprites are stored as a width/height header followed by two 1-bit bitmaps (image and mask). Transparent PNG pixels are excluded from the mask; non-black opaque pixels become white.
