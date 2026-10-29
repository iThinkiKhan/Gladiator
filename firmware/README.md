# Firmware

The networking is largely overbuilt: multiple processors working in concert. The ESP32-S3 on the back is the primary coordinator. The ESP32-C6 next to it handles networking, and the ESP32-CAM board controls the camera and the display in the mast.

All of the current features are working, although the Dupont connections still cause some reliability issues. Autonomous features are next. I want the bot to become something like a Roomba, mapping and moving around independently.

[Project](../README.md) · [S3 controller](src) · [C6 gateway](gateway) · [Camera and display](cam) · [Shared protocol](shared) · [Operator interface](tools/booth)

## Build reference

The projects retain the canonical source layout. On Windows, use a checkout on a local drive: the ESP-IDF compiler cannot resolve UNC network-share source paths. Install PlatformIO, then run these commands from this `firmware` directory:

```sh
python tools/configure.py
pio run -e gladiator_s3
pio run -d gateway -e gladiator_c6
pio run -d cam -e gladiator_cam
```

`configure.py` creates ignored local configuration files from examples without overwriting existing files. Set your own network credentials, OTA password and matching S3/C6 link key before use. Set the camera credentials and gateway URL in `cam/src/LocalConfig.h`. Gateway browser assets are generated from `tools/booth/` and `tools/hardware-test/` during its build. No configured binaries are included.

## Offline checks

```sh
python ../tools/run_host_tests.py
node tools/booth/test.cjs
node tools/booth/test_settings.cjs
```

The C++ checks need a C++17 compiler (`g++` by default; set `CXX` to override). These checks do not connect to or control a robot.

## Hardware setup

<!-- WRITE: Your hardware setup, calibration and operating instructions. -->

## Operation

<!-- WRITE: Your controls and operating guidance. -->
