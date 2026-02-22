# Bundle

The bundle format is a binary container for storing named, typed sections. It is used for plugin bundles, config bundles, and screen bundles.

## Structure

```
[magic: 3 bytes]               - "ALP"
[version: 1 byte]              - currently 0
[header length: uint32 LE]     - total size of everything except section data
[section count: uint16 LE]     - number of sections

Section headers (repeated for each section):
  [type: 1 byte]               - section type
  [offset: uint32 LE]          - byte offset to section data (from start of bundle)
  [size: uint32 LE]            - section data size in bytes
  [crc32: uint32 LE]           - CRC32/IEEE checksum of section data
  [name: null-terminated string]

Section data (repeated for each section):
  [data: N bytes]              - section data
  [null: 1 byte]               - null terminator
```

## Section Types

| Type   | Byte | Description                            |
|--------|------|----------------------------------------|
| ATTR   | 0x00 | String attribute (e.g. plugin name)    |
| BINARY | 0x01 | Binary data (e.g. code, config values) |
| SPRITE | 0x02 | Bitmap sprite data                     |
| CONFIG | 0x03 | Nested bundle containing config schema |

## Notes

- All integers are little-endian.
- Section names and ATTR data are null-terminated strings.
- The CRC32 checksum covers only the section data bytes (not the null terminator).
- CONFIG sections contain a complete, independently decodable bundle as their data.
