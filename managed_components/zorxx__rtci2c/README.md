# I2C RTC Library
This library provides support for I2C real-time clock devices

This library currently supports the following platforms:
* Linux i2c subsystem
* esp-idf

Source for this project can be found at [https://github.com/zorxx/rtci2c](https://github.com/zorxx/rtci2c).

# Usage

## API

The API for this library can be found in the `include/rtci2c/rtci2c.h` header file.

## Portability

Portability among various host platforms (e.g. Linux i2c device interface vs. the esp-idf i2c driver interface) is accomplished via a platform-specific `i2c_lowlevel_config` structure which is defined at compile-time for the project based on build environment and/or toolchain hints. An example configuration for `i2c_lowlevel_config` for Linux is:

```bash
rtci2c_lowlevel_config config = {0};
config.device = "/dev/i2c-0";
```

An example configuration for `i2c_lowlevel_config` for esp-idf is:

```bash
i2c_lowlevel_config config = {0};
config.port = I2C_NUM_0;
config.pin_sda = GPIO_NUM_21;
config.pin_scl = GPIO_NUM_22;
```

Note that the members of the `i2c_lowlevel_config` change (at compile-time) based on the target platform.

# Example Applications

Example applications are provided for each of the supported platforms and can be found in the `examples` directory.

## Linux

Use the following commands to build the Linux test application (execute from the root of the source directory)

```bash
cmake -B build -S .
cmake --build build
```

## esp-idf

To build the esp-idf test application, execute the following commands after initializing the esp-idf environment (e.g. run `source export.sh`):

```bash
cd esp-idf
idf.py build
```

# License
All files delivered with this library are copyright 2024 Zorxx Software and released under the MIT license. See the `LICENSE` file for details.
