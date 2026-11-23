#!/usr/bin/env bash
set -euo pipefail

readonly executable="$1"
readonly source_dir="$2"
readonly profile="${source_dir}/profiles/al5c-conservative.toml"
readonly scenario="${source_dir}/scenarios/controller-smoke.toml"

expect_failure() {
  if "$@" >/dev/null 2>&1; then
    printf 'Expected command to fail:' >&2
    printf ' %q' "$@" >&2
    printf '\n' >&2
    exit 1
  fi
}

"${executable}" validate "${profile}" "${scenario}" >/dev/null
"${executable}" plan "${profile}" "${scenario}" --device /dev/ttyTEST >/dev/null
expect_failure "${executable}"
expect_failure "${executable}" unknown "${profile}" "${scenario}"
expect_failure "${executable}" validate "${profile}" "${scenario}" --unknown
expect_failure "${executable}" validate "${profile}" "${scenario}" --device
expect_failure "${executable}" run "${profile}" "${scenario}"
expect_failure "${executable}" run "${profile}" "${scenario}" --execute --yes \
  --device /definitely/missing/ssc32 --json /tmp/unused.json --junit /tmp/unused.xml

if printf 'n\n' | "${executable}" run "${profile}" "${scenario}" --execute \
  >/dev/null 2>&1; then
  printf 'Expected operator rejection to fail.\n' >&2
  exit 1
fi
