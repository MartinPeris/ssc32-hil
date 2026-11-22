# Configuration reference

## Profile

Required top-level fields:

- `name`: human-readable profile name.
- `[controller].device`: serial device path; may be overridden by `--device`.
- `[controller].baud`: positive baud rate supported by the SSC-32.
- `[controller].timeout_ms`: positive read timeout.
- `[[channels]]`: one or more named physical channels.

Each channel requires:

- `name`: unique semantic identifier.
- `number`: unique SSC-32 channel from 0 through 31.
- `minimum_us`, `home_us`, `maximum_us`: ordered pulse envelope within the
  SSC-32's 500–2500 microsecond protocol range.
- `speed_us_per_second`: positive speed limit used for test movements.

Channels 0–15 use VS1 and channels 16–31 use VS2. The application derives the
bank from `number`; it is not separately configurable.

## Scenario actions

### `version`

Sends `VER` and optionally validates `expect_prefix`.

### `status`

Sends `Q` and requires `expect_moving = true` or `false`.

### `pulse_query`

Requires `channels`, an array of declared channel names. It reads controller
commanded pulse widths; it does not measure shaft position.

### `move`

Requires positive `duration_ms` and a `targets` inline table. Each target must
specify exactly one of:

- `pulse_us`: absolute pulse width.
- `offset_us`: signed offset from that channel's configured home pulse.

Every resolved target must be inside the channel envelope. The runner waits for
the controller to report idle and then verifies the commanded pulse width with
a 10 microsecond tolerance, matching SSC-32 query quantization.

An optional `operator_confirmation` pauses for a physical observation after the
automated assertions.

### `pause`

Requires positive `pause_ms` and performs no serial I/O.
