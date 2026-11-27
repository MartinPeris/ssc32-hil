# ssc32-hil

[![CI](https://github.com/MartinPeris/ssc32-hil/actions/workflows/ci.yml/badge.svg)](https://github.com/MartinPeris/ssc32-hil/actions/workflows/ci.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE)

A declarative hardware-in-the-loop test application for the
[ssc32](https://github.com/MartinPeris/ssc32) C++ library and Lynxmotion
SSC-32/SSC-32U servo controllers.

The application separates physical wiring from test intent, validates every
motion against a configured pulse envelope, previews the exact protocol bytes,
and records the bytes actually exchanged with the controller.

## Status

The parser, plan compiler, protocol preview, recording transport, and POSIX
serial path are exercised in automated tests. Physical SSC-32 validation is
still pending. The bundled AL5C profiles intentionally use narrow pulse limits.
The channels 16–22 profile records the currently reported seven-servo wiring,
including the mechanically mirrored shoulder-link pair.

## Safety boundary

SSC-32 controllers command open-loop hobby servos. A pulse-width query confirms
the controller command, not the physical shaft position. This application is
not a safety system.

- Inspect the generated plan before execution.
- Support the arm, remove payloads, and keep an accessible power cutoff nearby.
- Verify channel assignments and mechanical limits on the physical arm.
- Start with the controller-only scenario before enabling servo power.
- Do not widen the sample pulse envelopes until each joint is calibrated.

Execution is opt-in: `run` refuses to open the serial device without
`--execute`, and asks for a physical preflight confirmation unless `--yes` is
explicitly supplied.

## Build

Requirements: a C++17 compiler, CMake 3.20+, and Git.

```sh
cmake -S . -B build -G Ninja \
  -DSSC32_HIL_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The build first looks for an installed `ssc32` CMake package. If none is found,
it fetches the audited library commit pinned in `CMakeLists.txt`. Developers can
use an existing checkout explicitly:

```sh
cmake -S . -B build -G Ninja \
  -DSSC32_HIL_SSC32_SOURCE_DIR=/path/to/ssc32
```

## Use

Validate configuration without touching hardware:

```sh
./build/ssc32-hil validate \
  profiles/al5c-conservative.toml \
  scenarios/controller-smoke.toml
```

Render the resolved channel map and exact wire commands:

```sh
./build/ssc32-hil plan \
  profiles/al5c-conservative.toml \
  scenarios/single-channel-smoke.toml \
  --device /dev/ttyUSB0
```

Execute only after reviewing that plan:

```sh
./build/ssc32-hil run \
  profiles/al5c-conservative.toml \
  scenarios/controller-smoke.toml \
  --device /dev/ttyUSB0 \
  --execute
```

Each run writes `ssc32-hil-report.json` and `ssc32-hil-report.xml`. The JSON
report includes the result of every step and a timestamped hexadecimal TX/RX
transcript. The JUnit report can be ingested by test-reporting tools.

### Channels 16–22 AL5C checkout

First validate communication with servo power off:

```sh
./build/ssc32-hil run \
  profiles/al5c-ch16-22-conservative.toml \
  scenarios/al5c-ch16-22-controller-check.toml \
  --device /dev/ttyUSB0 \
  --execute
```

Then support the arm, position it near the provisional 1500 µs neutral pose,
keep the servo-power cutoff within reach, and preview the motion test:

```sh
./build/ssc32-hil plan \
  profiles/al5c-ch16-22-conservative.toml \
  scenarios/al5c-ch16-22-motion-smoke.toml \
  --device /dev/ttyUSB0
```

The motion scenario uses only 15–30 µs excursions and returns each joint to
neutral before continuing. Channels 17 and 18 are always commanded atomically
with opposite offsets; if they oppose each other instead of sharing the load,
cut servo power and reverse that pairing before proceeding.

For the dedicated paired-shoulder sweep, preview and run:

```sh
./build/ssc32-hil plan \
  profiles/al5c-shoulder-pair-45deg-provisional.toml \
  scenarios/al5c-shoulder-pair-45deg.toml \
  --device /dev/ttyUSB0

./build/ssc32-hil run \
  profiles/al5c-shoulder-pair-45deg-provisional.toml \
  scenarios/al5c-shoulder-pair-45deg.toml \
  --device /dev/ttyUSB0 \
  --execute
```

This scenario commands only channels 17 and 18. It assumes the common
500–2500 µs ≈ 180° mapping, so 1500/1500 is neutral, 2000/1000 is provisional
+45°, and 1000/2000 is provisional −45°. Those angles remain estimates until
the physical arm is measured and calibrated.

## Declarative model

A hardware profile gives semantic names to physical channels and defines the
only permitted pulse range:

```toml
[[channels]]
name = "shoulder"
number = 1
minimum_us = 1450
home_us = 1500
maximum_us = 1550
speed_us_per_second = 75
```

A scenario refers to the semantic name rather than the SSC-32 channel number:

```toml
[[steps]]
name = "small-shoulder-motion"
action = "move"
duration_ms = 1000
targets = { shoulder = { offset_us = 50 } }
operator_confirmation = "Confirm smooth motion in the expected direction."
```

The compiler resolves this into a concrete `ssc32::ServoCommand`, rejects any
target outside the profile envelope, and prints the exact command generated by
the `ssc32` protocol implementation. See [ARCHITECTURE.md](ARCHITECTURE.md) for
the complete data flow and [docs/configuration.md](docs/configuration.md) for the
configuration reference.

## License

BSD 3-Clause. See [LICENSE](LICENSE).
