# Config

Plugin configuration is defined in `alp.yml` and encoded into the plugin bundle as a nested bundle with type `BundleTypeConfig` (0x03) and name `main`.

## Schema Bundle

The schema bundle encodes config sections and items as `BundleTypeBinary` entries. Each entry starts with a common header:

```
[section: 1 byte]              - section index
[type: 1 byte]                 - 0=section, 1=string, 2=bool, 3=int, 4=float
[flags: 1 byte]                - bit0=default, bit1=min, bit2=max,
                                 bit3=step, bit4=options, bit5=hint
```

### Sections

Named by index (`"0"`, `"1"`, ...) with type `0`:

```
[section] [type=0] [flags]
[title: null-terminated string]
[hint: null-terminated string]  - if flag set
```

### Items

Named by key (e.g. `"name"`) with type `1`-`4`:

```
[section] [type] [flags]
[title: null-terminated string]
[hint: null-terminated string]  - if flag set
[default value]                 - if flag set
[min value]                     - if flag set
[max value]                     - if flag set
[step value]                    - if flag set
[option count: uint16]          - if flag set
  [value] [label: null-terminated string] ...
```

## Stored Values

User-modified config values are stored as a separate bundle (loaded from `{plugin}.alc`). Each entry is a `BundleTypeBinary` section named by key:

```
[type: 1 byte]  - 1=string, 2=bool, 3=int, 4=float
[value]
```

## Value Encoding

| Type   | Byte | Encoding                       |
|--------|------|--------------------------------|
| string | 1    | null-terminated string         |
| bool   | 2    | 1 byte (0 or 1)                |
| int    | 3    | int32 little-endian            |
| float  | 4    | float32 little-endian          |

## Resolution

When reading a config value, stored values are checked first. If no stored value exists, the default from the schema bundle is used.
