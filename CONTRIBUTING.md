# Contributing

Keep changes narrow and testable. New scenario actions must define validation,
wire preview, execution, failure behavior, and automated coverage together.

Before submitting a change:

```sh
cmake -S . -B build -G Ninja -DSSC32_HIL_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Hardware reports should include the controller model and firmware, host OS,
serial adapter, baud rate, profile, scenario, and generated JSON evidence. Do
not include unrelated serial-device or system information.
