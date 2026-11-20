#!/usr/bin/env bash
set -euo pipefail

test_dir=$(mktemp -d /tmp/ax-shared-cli.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
cat > "$test_dir/math.ax" <<'AX'
fn ax_test_sum(left, right) -> f64:
    return left + right
AX

"$AXC" build --shared "$test_dir/math.ax" -o "$test_dir/libax_test_math.so"
test -s "$test_dir/libax_test_math.so"
test -s "$test_dir/libax_test_math.h"
cat > "$test_dir/harness.c" <<'C'
#include "ax_runtime.h"
#include "libax_test_math.h"
int main(void) {
  AxVal* result = ax_test_sum(ax_scalar(2.0), ax_scalar(3.0));
  return ax_to_double(result) == 5.0 ? 0 : 1;
}
C
"$CC" -I "$AX_INCLUDE" -I "$test_dir" "$test_dir/harness.c" \
  -L "$test_dir" -lax_test_math -Wl,-rpath,"$test_dir" -o "$test_dir/harness"
"$test_dir/harness"
