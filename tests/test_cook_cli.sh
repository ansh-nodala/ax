#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${AXC:-}" ]]; then
  echo "AXC must point to the axc executable" >&2
  exit 2
fi

test_dir=$(mktemp -d /tmp/ax-cook-cli.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
cat > "$test_dir/triangle.obj" <<'OBJ'
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
OBJ

"$AXC" cook "$test_dir/triangle.obj" -o "$test_dir/triangle.axb"
"$AXC" cook "$test_dir/triangle.obj" -o "$test_dir/triangle-again.axb"
cmp "$test_dir/triangle.axb" "$test_dir/triangle-again.axb"
test -s "$test_dir/triangle.axb"
