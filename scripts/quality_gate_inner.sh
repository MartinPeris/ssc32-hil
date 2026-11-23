#!/usr/bin/env bash
set -euo pipefail

readonly workspace=/workspace
readonly source_dir="${workspace}/src/ssc32-hil"
readonly coverage_lines_minimum=40
readonly coverage_branches_minimum=30

section() {
  printf '\n==> %s\n' "$1"
}

configure() {
  local build_dir="$1"
  shift
  cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
    -DSSC32_HIL_WARNINGS_AS_ERRORS=ON \
    -DSSC32_HIL_SSC32_SOURCE_DIR="${workspace}/deps/ssc32" \
    -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS="${workspace}/deps/tomlplusplus" \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    "$@"
}

validate_scenarios() {
  local executable="$1/ssc32-hil"
  "${executable}" validate \
    "${source_dir}/profiles/al5c-conservative.toml" \
    "${source_dir}/scenarios/controller-smoke.toml"
  "${executable}" validate \
    "${source_dir}/profiles/al5c-conservative.toml" \
    "${source_dir}/scenarios/single-channel-smoke.toml"
}

section 'Formatting'
mapfile -d '' formatted_sources < <(
  find "${source_dir}/include" "${source_dir}/src" "${source_dir}/tests" \
    -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0
)
clang-format --dry-run --Werror "${formatted_sources[@]}"

section 'Shell lint'
shellcheck \
  "${source_dir}/.githooks/pre-commit" \
  "${source_dir}/scripts/install_git_hooks.sh" \
  "${source_dir}/scripts/quality_gate.sh" \
  "${source_dir}/scripts/quality_gate_inner.sh"

section 'GCC warnings-as-errors build, tests, scenarios, and installation'
export CC=gcc CXX=g++
configure "${workspace}/build-gcc" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "${workspace}/build-gcc" --parallel 2
ctest --test-dir "${workspace}/build-gcc" --output-on-failure
validate_scenarios "${workspace}/build-gcc"
cmake --install "${workspace}/build-gcc" --prefix "${workspace}/install-gcc"

section 'Clang static analysis'
while IFS= read -r source; do
  clang-tidy --quiet -p "${workspace}/build-gcc" "${source}"
done < <(find "${source_dir}/src" -type f -name '*.cpp' | sort)

section 'Clang warnings-as-errors build and tests'
export CC=clang CXX=clang++
configure "${workspace}/build-clang"
cmake --build "${workspace}/build-clang" --parallel 2
ctest --test-dir "${workspace}/build-clang" --output-on-failure

section 'AddressSanitizer and UndefinedBehaviorSanitizer'
export CC=gcc CXX=g++
configure "${workspace}/build-sanitizers" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build "${workspace}/build-sanitizers" --parallel 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest --test-dir "${workspace}/build-sanitizers" --output-on-failure

section 'Coverage build, tests, and gate'
configure "${workspace}/build-coverage" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='--coverage -O0 -g' \
  -DCMAKE_EXE_LINKER_FLAGS=--coverage \
  -DCMAKE_SHARED_LINKER_FLAGS=--coverage
cmake --build "${workspace}/build-coverage" --parallel 2
ctest --test-dir "${workspace}/build-coverage" --output-on-failure
gcovr \
  --root "${source_dir}" \
  --object-directory "${workspace}/build-coverage" \
  --gcov-exclude-directories '.*/CMakeFiles/.*/CompilerIdCXX$' \
  --filter "${source_dir}/src/" \
  --filter "${source_dir}/include/ssc32_hil/" \
  --exclude-unreachable-branches \
  --exclude-throw-branches \
  --print-summary \
  --fail-under-line "${coverage_lines_minimum}" \
  --fail-under-branch "${coverage_branches_minimum}"

section 'Quality gate passed'
