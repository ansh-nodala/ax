#!/usr/bin/env bash
set -euo pipefail
AXC=${AXC:-./build/axc}
echo "== AX examples =="
for f in examples/*.ax; do
  case "$f" in *bad_shape*) continue;; esac
  echo "--- $f"
  "$AXC" build "$f" -o /tmp/ax_ex && /tmp/ax_ex
done
echo "== shape checker must fail =="
if "$AXC" check examples/bad_shape.ax; then echo "ERROR: bad_shape should fail"; exit 1; else echo "OK: bad_shape rejected"; fi
echo "== emitters =="
"$AXC" emit-asm examples/matmul.ax > /tmp/ax.s && grep -q "ax_add" /tmp/ax.s && echo "asm OK"
"$AXC" emit-ptx examples/cuda_kernel.ax > /tmp/ax.ptx && grep -q ".version 8" /tmp/ax.ptx && echo "ptx OK"
"$AXC" emit-spirv examples/vulkan_shader.ax -o /tmp/ax.spv
spirv_magic=$(od -An -tx1 -N4 /tmp/ax.spv | tr -s ' ' | sed 's/^ //;s/ $//')
if [[ "$spirv_magic" != "03 02 23 07" ]]; then
  echo "ERROR: invalid SPIR-V magic: $spirv_magic"
  exit 1
fi
echo "spirv OK"
"$AXC" emit-stablehlo examples/tpu_kernel.ax > /tmp/ax.mlir && grep -q "stablehlo" /tmp/ax.mlir && echo "stablehlo OK"
echo "ALL EXAMPLE TESTS DONE"
