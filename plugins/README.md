# Plugins

Air Lab supports WASM-based plugins that run on-device via [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime) (WebAssembly Micro Runtime). Plugins are written in C, compiled to `wasm32-wasi` with Zig, and bundled into `.alp` files that can be uploaded and launched over USB.

> **Note:** The plugins in this repo are primarily intended to test the plugin system. If you want to write your own plugins, check out the [Script Editor](https://studio.networkedartifacts.com/airlab/editor) first — it provides its own Go API and a faster feedback loop.

## Adding a Plugin

1. Create a new directory (e.g. `myplugin/`) with a `main.c` source file and an `alp.yml` manifest.
2. Add the directory name to `.PHONY` (line 1) and the `foreach` list (line 15) in the [`Makefile`](Makefile).

After that you can build, upload, and launch the plugin using `make myplugin`.

## Prerequisites

- [Zig](https://ziglang.org) compiler
- [Go](https://go.dev) 1.23+ (for the `airlab` CLI — see [`../tools/`](../tools/))
- Air Lab device connected via USB

## Writing a Plugin

A minimal plugin consists of a C source file and a manifest. Here is the [`hello`](hello/) example:

**`hello/main.c`:**

```c
#include "../al.h"

int main() {
  al_clear(0);
  al_write(AL_W / 2, (AL_H - 16) / 2, 0, 16, 1, "Hello, World!", AL_WRITE_ALIGN_CENTER);
  al_yield(0, 0);
  return 0;
}
```

**`hello/alp.yml`:**

```yaml
name: hello-world
title: Hello World
version: v0.1.0
binary: ./main.wasm
```

The plugin includes [`al.h`](al.h), which provides the full device API. The `alp.yml` manifest defines the plugin metadata. The `name` field is the plugin identifier, `title` is the display name, `version` must be valid semver, and `binary` points to the compiled WASM file. Plugins that use sprites list them under `sprites:` (see the [`sprite`](sprite/) example).

## Building

The [`Makefile`](Makefile) provides targets for each plugin:

```bash
make hello                  # compile and bundle
make hello UPLOAD=1         # compile, bundle, and upload to device
make hello LAUNCH=1         # compile, bundle, and launch on device (streams logs)
```

To build and upload all plugins:

```bash
make upload
```

## API Reference

The [`al.h`](al.h) header defines all functions available to plugins. The display is 296x128 pixels, exposed as `AL_W` and `AL_H`.

**Info:**

| Function | Description |
|----------|-------------|
| `al_info(i)` | Read device info: battery, sensors, storage, accelerometer. |
| `al_config(c, a, b, d)` | Set engine config: button repeat, screen rotation. |

**Flow Control:**

| Function | Description |
|----------|-------------|
| `al_yield(timeout, flags)` | Yield to the runtime, wait for input. Returns button events. Flags control frame skip, wait, invert, and refresh. |
| `al_delay(ms)` | Sleep for the given number of milliseconds. |
| `al_millis()` | Get the current time in milliseconds. |

**Graphics:**

| Function | Description |
|----------|-------------|
| `al_clear(c)` | Clear the screen (0 = white, 1 = black). |
| `al_line(x1, y1, x2, y2, c, b)` | Draw a line. |
| `al_rect(x, y, w, h, c, b)` | Draw a rectangle. |
| `al_write(x, y, s, f, c, str, flags)` | Draw text. `s` = style, `f` = font size, `c` = color. Supports center/right alignment. |
| `al_draw(x, y, w, h, s, a, img, mask)` | Draw a raw bitmap with optional mask. |
| `al_beep(freq, duration, flags)` | Play a tone. Use `AL_BEEP_WAIT` to block. |

**I/O:**

| Function | Description |
|----------|-------------|
| `al_gpio(cmd, flags, arg)` | Configure, read, or write GPIO pins (digital, PWM, analog). |
| `al_i2c(addr, w, wl, r, rl, timeout)` | Perform an I2C transaction. |

**Sprites:**

| Function | Description |
|----------|-------------|
| `al_sprite_resolve(name)` | Resolve a bundled sprite by name. |
| `al_sprite_width(sprite)` | Get sprite width. |
| `al_sprite_height(sprite)` | Get sprite height. |
| `al_sprite_draw(sprite, x, y, s, a)` | Draw a sprite. |

**Data:**

| Function | Description |
|----------|-------------|
| `al_data_set(name, buf, len)` | Write persistent key-value data. |
| `al_data_get(name, buf, len)` | Read persistent key-value data. |

**HTTP:**

| Function | Description |
|----------|-------------|
| `al_http_new()` | Create a new HTTP request. |
| `al_http_set(field, num, str1, str2)` | Set request fields (URL, method, headers, auth, timeout). |
| `al_http_run(req, req_len, res, res_len)` | Execute the request with optional body, receive response. |
| `al_http_get(field)` | Get response fields (status, length, errno). |

**Utils:**

| Function | Description |
|----------|-------------|
| `al_log(msg)` | Log a string message. |
| `al_logf(fmt, ...)` | Log a formatted message (printf-style, max 256 chars). |

## Constraints

- **Memory:** 256 KB total (initial and max), 65 KB stack.
- **Target:** `wasm32-wasi`.
- **Compilation flags:** `-Os -Wl,-z,stack-size=65536 -Wl,--initial-memory=262144 -Wl,--max-memory=262144`.
