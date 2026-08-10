# BMV080 SDK

Download the SDK from [BOSCH](https://www.bosch-sensortec.com/en/products/environmental-sensors/particulate-matter-sensor/bmv080), requires license agreement. Then unpack the ESP32S3 precompiled binaries to this directory.

Without the SDK, the build falls back to the stub in `stub/`, which disables PM support (the sensor is reported as absent). Release builds must use the real SDK.
