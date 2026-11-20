#!/usr/bin/env bash
set -euo pipefail

machine=$(uname -m)
case "$machine" in
  aarch64|arm64) ;;
  *)
    echo "error: a native Linux ARM64 host is required (detected $machine)" >&2
    echo "run this script on an ARM64 Linux machine; x86_64 is not an ARM64 verification" >&2
    exit 2
    ;;
esac

build_dir=${AX_ARM64_BUILD_DIR:-"${TMPDIR:-/tmp}/ax-lang-native-v2-arm64"}
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS='-Wall -Wextra' \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra'
cmake --build "$build_dir" --clean-first --parallel 2
ctest --test-dir "$build_dir" --output-on-failure
"$build_dir/axc" run hello.ax
echo "Linux ARM64 build and regression suite passed: $build_dir"
