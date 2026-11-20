#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${AXPKG:-}" ]]; then
  echo "AXPKG must point to the axpkg executable" >&2
  exit 2
fi

test_dir=$(mktemp -d /tmp/axpkg-lock.XXXXXX)
trap 'rm -rf "$test_dir"; rm -f "$test_dir.first.lock"' EXIT
mkdir -p "$test_dir/src"
cat > "$test_dir/axpkg.toml" <<'TOML'
[package]
name = "lock-test"
version = "0.1.0"
TOML
printf 'fn main() -> i32 { 42 }\n' > "$test_dir/src/main.ax"

cd "$test_dir"
"$AXPKG" lock
"$AXPKG" verify
cp axpkg.lock "$test_dir.first.lock"
"$AXPKG" lock
cmp "$test_dir.first.lock" axpkg.lock
printf 'fn main() -> i32 { 43 }\n' > src/main.ax
if "$AXPKG" verify; then
  echo "axpkg verify accepted a modified package file" >&2
  exit 1
fi
