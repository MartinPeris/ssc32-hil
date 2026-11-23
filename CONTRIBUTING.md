# Contributing

Keep changes narrow and testable. New scenario actions must define validation,
wire preview, execution, failure behavior, and automated coverage together.

Install the versioned pre-commit hook once per clone:

```sh
./scripts/install_git_hooks.sh
```

The hook runs the complete Dockerized quality gate against the exact staged Git
tree. Run the same gate explicitly against working-tree content with
`./scripts/quality_gate.sh`. It enforces formatting, shell lint, Clang static
analysis, GCC and Clang warnings-as-errors, tests, bundled-scenario validation,
installation, ASan/UBSan, and an exact committed production-coverage ratchet,
currently above 91% lines and 83% branches. Every commit must equal or improve
both covered/total ratios. After a stable improvement, run
`./scripts/quality_gate.sh --update-coverage-baseline` to adopt it; the command
refuses to lower either metric.

Hardware reports should include the controller model and firmware, host OS,
serial adapter, baud rate, profile, scenario, and generated JSON evidence. Do
not include unrelated serial-device or system information.
