# Firmware

<!-- WRITE: Your software overview. -->

[Project](../README.md) Â· [S3 controller](src) Â· [C6 gateway](gateway) Â· [Camera and display](cam) Â· [Shared protocol](shared) Â· [Operator interface](tools/booth)

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
