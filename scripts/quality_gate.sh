#!/usr/bin/env bash
set -euo pipefail

readonly ssc32_commit="d096754e7a47da0b737cd35f9fdd2fa9545e29a0"
readonly tomlplusplus_tag="v3.4.0"
repository_root="$(git rev-parse --show-toplevel)"
readonly repository_root
readonly source_mode="${1:---working-tree}"

case "${source_mode}" in
  --staged | --working-tree) ;;
  *)
    printf 'Usage: %s [--staged|--working-tree]\n' "$0" >&2
    exit 2
    ;;
esac

for command in docker git tar; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'Required command not found: %s\n' "${command}" >&2
    exit 1
  fi
done

if ! docker info >/dev/null 2>&1; then
  printf 'Docker is required and must be running.\n' >&2
  exit 1
fi

if [[ "${source_mode}" == "--staged" ]]; then
  git -C "${repository_root}" diff --cached --check
fi

workspace="$(mktemp -d "${TMPDIR:-/tmp}/ssc32-hil-quality.XXXXXX")"
cleanup() {
  rm -rf "${workspace}"
}
trap cleanup EXIT

quality_source="${workspace}/src/ssc32-hil"
mkdir -p "${quality_source}" "${workspace}/deps/ssc32" \
  "${workspace}/deps/tomlplusplus"
if [[ "${source_mode}" == "--staged" ]]; then
  tree="$(git -C "${repository_root}" write-tree)"
  git -C "${repository_root}" archive "${tree}" |
    tar -xf - -C "${quality_source}"
else
  tar -C "${repository_root}" \
    --exclude=.git --exclude=build --exclude=install --exclude=log \
    -cf - . | tar -xf - -C "${quality_source}"
fi

ssc32_repository="${repository_root}/../ssc32"
if [[ -d "${ssc32_repository}/.git" ]] &&
  git -C "${ssc32_repository}" cat-file -e "${ssc32_commit}^{commit}" 2>/dev/null; then
  git -C "${ssc32_repository}" archive "${ssc32_commit}" |
    tar -xf - -C "${workspace}/deps/ssc32"
else
  ssc32_cache="${XDG_CACHE_HOME:-${HOME}/.cache}/ssc32-hil/ssc32"
  mkdir -p "$(dirname "${ssc32_cache}")"
  if [[ ! -d "${ssc32_cache}/.git" ]]; then
    git clone --quiet https://github.com/MartinPeris/ssc32.git "${ssc32_cache}"
  fi
  if ! git -C "${ssc32_cache}" cat-file -e "${ssc32_commit}^{commit}" 2>/dev/null; then
    git -C "${ssc32_cache}" fetch --quiet origin "${ssc32_commit}"
  fi
  git -C "${ssc32_cache}" archive "${ssc32_commit}" |
    tar -xf - -C "${workspace}/deps/ssc32"
fi

toml_cache="${XDG_CACHE_HOME:-${HOME}/.cache}/ssc32-hil/tomlplusplus"
mkdir -p "$(dirname "${toml_cache}")"
if [[ ! -d "${toml_cache}/.git" ]]; then
  git clone --quiet https://github.com/marzer/tomlplusplus.git "${toml_cache}"
fi
if ! git -C "${toml_cache}" rev-parse --verify --quiet "${tomlplusplus_tag}^{commit}" >/dev/null; then
  git -C "${toml_cache}" fetch --quiet --tags origin
fi
git -C "${toml_cache}" archive "${tomlplusplus_tag}" |
  tar -xf - -C "${workspace}/deps/tomlplusplus"

dockerfile="${quality_source}/docker/Dockerfile.quality"
image_tag="ssc32-hil-quality:$(git hash-object "${dockerfile}" | cut -c1-12)"
if ! docker image inspect "${image_tag}" >/dev/null 2>&1; then
  printf 'Building quality image %s (first run only for this toolchain revision)\n' "${image_tag}"
  docker build --tag "${image_tag}" --file "${dockerfile}" "${quality_source}"
fi

printf 'Running quality gate against the %s snapshot\n' "${source_mode#--}"
docker run --rm \
  --volume "${workspace}:/workspace" \
  --volume ssc32_hil_ccache:/root/.cache/ccache \
  --env CCACHE_DIR=/root/.cache/ccache \
  "${image_tag}" \
  bash /workspace/src/ssc32-hil/scripts/quality_gate_inner.sh
