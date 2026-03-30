# Plugins

Air Lab supports WASM-based plugins that run on-device via [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime) (WebAssembly Micro Runtime). Plugins are written in C, compiled to `wasm32-wasi` with Zig, and bundled into `.alp` files that can be uploaded and launched over USB.

> **Note:** The plugins in this repo are primarily intended to test the plugin system. If you want to write your own plugins, check out the [Script Editor](https://studio.networkedartifacts.com/airlab/editor) first — it provides its own Go API and a faster feedback loop.

## Adding a Plugin

1. Create a new directory (e.g. `examples/myplugin/`) with a `main.c` source file and an `alp.yml` manifest.
2. Add the directory name to `.PHONY`, the `foreach` list, and the `all:` target in the [`Makefile`](examples/Makefile).

After that, you can build the plugin using `make myplugin`.

## Prerequisites

- [Zig](https://ziglang.org) compiler
- `airlab` CLI — requires [Go](https://go.dev) 1.23+, install with `make install` in the [`tools`](../tools/) directory
- Air Lab device connected via USB (for upload/launch)

## Writing a Plugin

A minimal plugin consists of a C source file and a manifest. Here is the [`hello`](examples/hello/) example:

**`examples/hello/main.c`:**

```c
#include "../../al.h"

int main() {
  al_clear(0);
  al_write(AL_W / 2, (AL_H - 16) / 2, 0, 16, 1, "Hello, World!", AL_WRITE_ALIGN_CENTER);
  al_yield(0, 0);
  return 0;
}
```

**`examples/hello/alp.yml`:**

```yaml
name: example.hello
title: "Example: Hello"
version: v0.1.0
binary:
  main: ./main.wasm
```

The plugin includes [`al.h`](al.h), which provides the full device API. The `alp.yml` manifest defines the plugin metadata. The `name` field is the plugin identifier, `title` is the display name, `version` must be valid semver, and `binary` is an object whose `main` key points to the compiled WASM file. Plugins that use sprites list them under `sprites:` (see the [`sprite`](examples/sprite/) example).

## Building

The [`Makefile`](examples/Makefile) provides targets for each plugin:

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

### Info

| Function | Description |
|----------|-------------|
| `al_info(i)` | Read device info: battery, sensors, storage, accelerometer, input, and settings. |
| `al_config(c, a, b, d)` | Set engine config: button repeat, screen rotation. |

### Flow Control

| Function | Description |
|----------|-------------|
| `al_yield(timeout, flags)` | Yield to the runtime, wait for input. Returns events (buttons, touch, scroll, motion, sensor, power). Flags control frame behavior and event subscriptions. |
| `al_delay(ms)` | Sleep for the given number of milliseconds. |
| `al_millis()` | Get the current time in milliseconds. |
| `al_clock(epoch, field)` | Get clock fields (year, month, day, hour, minute, second, epoch). Pass `0` for current time. |

### Graphics

| Function | Description |
|----------|-------------|
| `al_clear(c)` | Clear the screen (0 = white, 1 = black). |
| `al_line(x1, y1, x2, y2, c, b)` | Draw a line. |
| `al_rect(x, y, w, h, c, b)` | Draw a rectangle. |
| `al_arc(x, y, r, sa, ea, c, w)` | Draw an arc. `r` = radius, `sa`/`ea` = start/end angle, `w` = stroke width. |
| `al_write(x, y, s, f, c, str, flags)` | Draw text. `s` = style, `f` = font size, `c` = color. Supports center/right alignment. |
| `al_draw(x, y, w, h, s, a, img, mask)` | Draw a raw bitmap with optional mask. |
| `al_beep(freq, duration, flags)` | Play a tone. Use `AL_BEEP_WAIT` to block. |

### I/O

| Function | Description |
|----------|-------------|
| `al_gpio(cmd, flags, arg)` | Configure, read, or write GPIO pins (digital, PWM, analog). |
| `al_i2c(addr, w, wl, r, rl, timeout)` | Perform an I2C transaction. |

### Store

| Function | Description |
|----------|-------------|
| `al_store_info(field)` | Get store info: start time, length, and record counts (all, short, long). |
| `al_store_query(field, values, count, start, resolution)` | Query stored sensor data (CO2, temperature, humidity, VOC, NOx, pressure). With `resolution=0`, access raw samples by index (`start` can be negative to read from the end). With `resolution>0`, access time-based interpolated values where `start` is in milliseconds. |

### Sprites

| Function | Description |
|----------|-------------|
| `al_sprite_resolve(name)` | Resolve a bundled sprite by name. |
| `al_sprite_width(sprite)` | Get sprite width. |
| `al_sprite_height(sprite)` | Get sprite height. |
| `al_sprite_draw(sprite, x, y, s, a)` | Draw a sprite. |

### Data

| Function | Description |
|----------|-------------|
| `al_data_set(name, buf, len)` | Write persistent key-value data. |
| `al_data_get(name, buf, len)` | Read persistent key-value data. |

### HTTP

| Function | Description |
|----------|-------------|
| `al_http_new()` | Create a new HTTP request. |
| `al_http_set(field, num, str1, str2)` | Set request fields (URL, method, headers, auth, timeout). |
| `al_http_run(req, req_len, res, res_len)` | Execute the request with optional body, receive response. |
| `al_http_get(field)` | Get response fields (status, length, errno). |

### Config

| Function | Description |
|----------|-------------|
| `al_config_get_s(key, value, value_len)` | Read a string config value. |
| `al_config_get_b(key)` | Read a boolean config value. |
| `al_config_get_i(key)` | Read an integer config value. |
| `al_config_get_f(key)` | Read a float config value. |

### Utils

| Function | Description |
|----------|-------------|
| `al_log(msg)` | Log a string message. |
| `al_logf(fmt, ...)` | Log a formatted message (printf-style, max 256 chars). |

## Constraints

- **Memory:** 256 KB total (initial and max), 65 KB stack.
- **Target:** `wasm32-wasi`.
- **Compilation flags:** `-Os -Wl,-z,stack-size=65536 -Wl,--initial-memory=262144 -Wl,--max-memory=262144`.
