# Screens

Idle screens are defined in `config/screens.alb`, an ALP bundle that maps numeric indices to screen plugins. The firmware loads this bundle on idle and cycles through the screens in order.

## Bundle Layout

Each screen is represented by one or two sections sharing the same numeric name (`"0"`, `"1"`, `"2"`, ...):

| Section Type  | Name  | Data                          | Required |
|---------------|-------|-------------------------------|----------|
| ATTR (0x00)   | `"N"` | Null-terminated plugin name   | yes      |
| CONFIG (0x03) | `"N"` | Nested ALP bundle with config | no       |

The ATTR section contains the plugin file name (e.g. `"clock"`). The optional CONFIG section contains a nested ALP bundle with ATTR entries that are passed as arguments to the screen plugin.

## Example

A bundle with two screens:

```
Section 0: ATTR  "0"  -> "clock"
Section 1: CONFIG "0" -> nested bundle { ATTR "timezone" -> "UTC" }
Section 2: ATTR  "1"  -> "weather"
```

## Display Cycle

1. Firmware loads `config/screens.alb` and counts ATTR sections to determine the number of screens.
2. The persistent index `scr_screen_index` selects the current screen (wraps to 0 at count).
3. The plugin name is read from the ATTR section and optional config from the CONFIG section.
4. If a CONFIG section is present, its data is parsed as a nested ALP bundle and passed as arguments to the screen plugin via `eng_run_config`.
5. After display, the device sleeps until woken. On wake it advances to the next screen.
6. If no `config/screens.alb` is present or it contains no screens, a default idle screen is shown.
