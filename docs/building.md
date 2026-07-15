# Building from source

## Requirements

- CMake 3.13 or newer
- Ninja or Make
- Arm GNU embedded compiler
- Raspberry Pi Pico SDK 2.2.0
- Git, for obtaining the SDK and its submodules

## Build

```sh
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git checkout 2.2.0
git submodule update --init
cd ..

export PICO_SDK_PATH="$PWD/pico-sdk"
git clone https://github.com/toonhuysmans/pocket-scion-synth.git
cmake -S pocket-scion-synth -B pocket-scion-synth/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build pocket-scion-synth/build
```

The flashable result is:

```text
pocket-scion-synth/build/pocket_scion_synth.uf2
```

## Host-side sensor test

The recovered sensor mathematics can be tested without the Pico SDK:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -Isrc src/sensor_math.c tests/test_sensor_math.c \
  -lm -o /tmp/test_sensor_math
/tmp/test_sensor_math
```

## Firmware metadata

When `picotool` is available:

```sh
picotool info -a build/pocket_scion_synth.uf2
```

The release should report version `2.3.0`, board `pocket_scion`, and RP2040
family metadata.

## Flashing

Enter the RP2040 USB bootloader, then copy `pocket_scion_synth.uf2` to the
`RPI-RP2` volume. The device automatically reboots after a successful copy.
