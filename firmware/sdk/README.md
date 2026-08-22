# SDK

This component hosts third party sensor SDKs. Openly redistributable code is committed directly, while license-gated SDKs are kept out of this repository: a committed stub stands in for the real SDK, and the payload is provided via a git-ignored directory.

## BMV080

The `bmv080/` directory is git-ignored and holds the precompiled ESP32-S3 SDK (`bmv080.h`, `bmv080_defs.h`, `lib_bmv080.a`, `lib_postProcessor.a`). Provide it either by:

- Cloning the private `airlab-bmv080` repository directly to `bmv080/`, or symlinking it there from a checkout elsewhere.
- Downloading the SDK from [BOSCH](https://www.bosch-sensortec.com/en/products/environmental-sensors/particulate-matter-sensor/bmv080) (requires license agreement) and unpacking the ESP32-S3 precompiled binaries to `bmv080/`.

Without the SDK, the build falls back to the stub in `stub/`, which disables PM support (the sensor is reported as absent). Release builds must use the real SDK.

Note: the stub/SDK switch happens at CMake configure time, and adding or removing the SDK does not trigger a reconfigure by itself. Force a reconfigure (or delete the build directory) after changing it.
