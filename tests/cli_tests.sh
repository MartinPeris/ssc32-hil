#!/usr/bin/env bash
set -euo pipefail

readonly executable="$1"
readonly source_dir="$2"
readonly profile="${source_dir}/profiles/al5c-conservative.toml"
readonly scenario="${source_dir}/scenarios/controller-smoke.toml"
readonly arm_profile="${source_dir}/profiles/al5c-ch16-22-conservative.toml"
readonly arm_scenario="${source_dir}/scenarios/al5c-ch16-22-motion-smoke.toml"
readonly shoulder_profile="${source_dir}/profiles/al5c-shoulder-pair-45deg-provisional.toml"
readonly shoulder_scenario="${source_dir}/scenarios/al5c-shoulder-pair-45deg.toml"

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
arm_plan="$("${executable}" plan "${arm_profile}" "${arm_scenario}" \
  --device /dev/ttyTEST)"
for expected in \
  'base_rotation -> 16 (VS2' \
  'shoulder_link_right -> 17 (VS2' \
  'shoulder_link_left -> 18 (VS2' \
  'shoulder -> 19 (VS2' \
  'wrist -> 20 (VS2' \
  'wrist_rotation -> 21 (VS2' \
  'gripper -> 22 (VS2' \
  'TX "#18 P1480 S25 #17 P1520 S25 T2000 \r"'; do
  if ! grep -Fq "${expected}" <<<"${arm_plan}"; then
    printf 'AL5C plan is missing expected text: %s\n' "${expected}" >&2
    exit 1
  fi
done
shoulder_plan="$(${executable} plan "${shoulder_profile}" "${shoulder_scenario}" \
  --device /dev/ttyTEST)"
for expected in \
  'shoulder_link_right -> 17 (VS2, 1000..2000 us, home 1500 us)' \
  'shoulder_link_left -> 18 (VS2, 1000..2000 us, home 1500 us)' \
  'TX "#18 P1500 S100 #17 P1500 S100 T5000 \r"' \
  'TX "#18 P1000 S100 #17 P2000 S100 T5000 \r"' \
  'TX "#18 P2000 S100 #17 P1000 S100 T5000 \r"'; do
  if ! grep -Fq "${expected}" <<<"${shoulder_plan}"; then
    printf 'Shoulder plan is missing expected text: %s\n' "${expected}" >&2
    exit 1
  fi
done
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
