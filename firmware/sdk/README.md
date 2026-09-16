# SDK

This component hosts third party code: sensor SDKs and small libraries. Openly redistributable code is committed directly, while license-gated SDKs are kept out of this repository: a committed stub stands in for the real SDK, and the payload is provided via a git-ignored directory.

## BMV080

The `bmv080/` directory is git-ignored and holds the precompiled ESP32-S3 SDK (`bmv080.h`, `bmv080_defs.h`, `lib_bmv080.a`, `lib_postProcessor.a`). Provide it either by:

- Cloning the private `airlab-bmv080` repository directly to `bmv080/`, or symlinking it there from a checkout elsewhere.
- Downloading the SDK from [BOSCH](https://www.bosch-sensortec.com/en/products/environmental-sensors/particulate-matter-sensor/bmv080) (requires license agreement) and unpacking the ESP32-S3 precompiled binaries to `bmv080/`.

Without the SDK, the build falls back to the stub in `stub/`, which disables PM support (the sensor is reported as absent). Release builds must use the real SDK.

Note: the stub/SDK switch happens at CMake configure time, and adding or removing the SDK does not trigger a reconfigure by itself. Force a reconfigure (or delete the build directory) after changing it.

## QR Code Generator

The `qrcodegen/` directory holds [Nayuki's QR code generator](https://www.nayuki.io/page/qr-code-generator-library), vendored unmodified. The firmware draws a QR code when a guided check finishes, so that the result can be carried off the device by a phone.

Vendored rather than pulled from the component registry because it is two files with no dependencies, and because the check payload depends on its segment handling: the URL is encoded as a byte-mode segment for the prefix followed by a numeric-mode segment for the payload digits, which is what makes the result fit a version 9 symbol at error correction level M — about 153 payload bytes at the 2 px module size the 296x128 screen allows.

Its license is MIT, which is not this repository's Apache 2.0. The notice is kept in both source files and in `qrcodegen/LICENSE`, and the library is in its own directory so that the boundary stays visible. Do not edit these files: an upstream update should replace them wholesale.
