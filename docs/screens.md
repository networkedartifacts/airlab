# Screens

Idle screens are defined in `config/screens.alb`, a bundle that maps IDs to screen plugins. The firmware loads this bundle on idle and cycles through the screens in order of their ATTR sections.

## Bundle Layout

Each screen is represented by one or two sections sharing the same ID:

| Section Type  | Name   | Data                             | Required |
|---------------|--------|----------------------------------|----------|
| ATTR (0x00)   | `"id"` | Null-terminated plugin name      | yes      |
| CONFIG (0x03) | `"id"` | Nested bundle with config schema | no       |

The ATTR section contains the plugin file name (e.g. `"clock"`). Section IDs are arbitrary labels used to match ATTR and CONFIG pairs. The display order is determined by the position of ATTR sections in the bundle, not by the ID. By convention, tools should use `"0"`, `"1"`, `"2"`, ... as IDs.

The optional CONFIG section contains a nested bundle with stored config values (see `docs/config.md` "Stored Values" format) that are passed as arguments to the screen plugin.

## Example

A bundle with two screens:

```
Section 0: ATTR   "0" -> "clock"
Section 1: CONFIG "0" -> BUNDLE { BINARY "timezone" -> [type=string] "UTC" }
Section 2: ATTR   "1" -> "weather"
```

## Display Cycle

1. Firmware loads `config/screens.alb` and counts ATTR sections to determine the number of screens.
2. The persistent index `scr_screen_index` selects the current screen (wraps to 0 at count).
3. The nth ATTR section is found by iterating, and its ID is used to look up the matching CONFIG section.
4. If a CONFIG section is present, its data is parsed as a nested bundle and passed as arguments to the screen plugin via `eng_run_config`.
5. After display, the device sleeps until woken. On wake it advances to the next screen.
6. If no `config/screens.alb` is present or it contains no screens, a default idle screen is shown.
