# Milestone 2 Report — Features 24–48

**Gate 2 result: NOT PASSED.** This is an evidence report for the work completed at the time it was written. Features that remained incomplete are marked as such; Milestone 3 had not started at that point.

## Environment

- Project: `/home/ansh-raj/ax-lang-native-v2`
- Date: 2026-11-20
- Host: Linux `x86_64`
- CMake: 4.2.3
- C/C++ compiler: GNU 15.2.0
- An AArch64 C++ cross compiler, QEMU runner, and ARM64 machine were not available. AArch64 assembly was syntax-assembled with Clang, but the full runtime was not built or executed on ARM64.

## Build and test evidence

Clean host build command:

```text
cmake --fresh -S . -B /tmp/ax-lang-native-v2-m2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS='-Wall -Wextra' \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra'
cmake --build /tmp/ax-lang-native-v2-m2 --parallel 2
Result: [75/75] built successfully; no compiler warnings or errors.
```

Test command and result:

```text
ctest --test-dir /tmp/ax-lang-native-v2-m2 --output-on-failure
100% tests passed, 0 tests failed out of 3

/tmp/ax-lang-native-v2-m2/ax_tests
RESULT: 14 passed, 0 failed

/tmp/ax-lang-native-v2-m2/ax_m2_ai_tests
RESULT: 27 passed, 0 failed

scripts/run_tests.sh
51 positive AX examples, negative shape rejection, and the existing asm/PTX/SPIR-V/StableHLO emitter smoke checks passed.
```

AArch64 assembly syntax checks also passed:

```text
clang --target=aarch64-linux-gnu -c compiler/backends/arm64/neon_dot.S \
  -o /tmp/ax_neon_dot_aarch64.o

axc emit-asm examples/m2_runtime.ax > /tmp/ax_m2_arm64.s
clang --target=aarch64-linux-gnu -c /tmp/ax_m2_arm64.s \
  -o /tmp/ax_m2_arm64.o
```

These checks produce valid AArch64 object files. They do not verify linking or execution of the complete ARM64 toolchain.

## Source constraint checks

| Check | Result |
|---|---|
| `find compiler runtime tools -name '*.py'` | **PASS** — zero files |
| `rg -n 'TODO|FIXME|not implemented|placeholder|stub' compiler runtime tools stdlib` | **PASS** — zero matches |
| Generic `stdlib/*_fn_NNN.ax` template files | **PASS** — zero remain |
| Generic backend `*_ext_NNN.hpp` template headers | **PASS** — zero remain |

The unreferenced generated template files were removed. Twenty unused NEON sample kernels that repeated the same incorrect loop body were replaced by the callable `compiler/backends/arm64/neon_dot.S` implementation. Metadata-only optimizer hooks for autodiff, tiling, inlining, and maxpower were removed from the compiler pipeline; they did not perform those transformations.

## Feature status

| # | Feature | Status | Implemented paths and remaining work |
|---:|---|---|---|
| 24 | Low-bit types and packed conversions | **PARTIAL** | `runtime/core/quantized.hpp`, `runtime/core/quantized.cpp`: i1/i2/i4, trit (five base-3 values per byte), FP4, FP8 E4M3/E5M2, BF16 pack/unpack, and scalar trit matmul. The compiler type system does not yet lower these names to packed storage, and trit matmul is not an ARM SIMD kernel. |
| 25 | FlashAttention-3 and PagedAttention | **PARTIAL** | `runtime/core/ai_kernels.hpp`, `runtime/core/ai_kernels.cpp`: tiled online-softmax attention on CPU. GPU IR lowering and a fused ARM64 attention kernel are absent. |
| 26 | RoPE and ALiBi | **PARTIAL** | `runtime/core/ai_kernels.cpp`, `runtime/core/axrt.cpp`, `runtime/core/axrt.hpp`: position-offset RoPE and ALiBi; AX runtime supports unary, offset, and explicit-shape RoPE calls. Dedicated NEON RoPE/ALiBi kernels are absent. |
| 27 | SwiGLU, GeGLU, RMSNorm, GQA | **PARTIAL** | `runtime/core/ai_kernels.cpp`, `runtime/core/axrt.cpp`, `compiler/backends/x86_64/x86_64_backend.cpp`, `compiler/backends/arm64/arm64_backend.cpp`: native CPU kernels and AX ABI bindings. The compiler does not yet lower these as fused IR operations. |
| 28 | MoE routing and load balancing | **PARTIAL** | `runtime/core/ai_kernels.hpp`, `runtime/core/ai_kernels.cpp`: top-K routing, normalized weights, and auxiliary load-balance loss. No compiler IR routing pass is wired. |
| 29 | Higher-order automatic differentiation | **PARTIAL** | `runtime/core/autodiff.hpp`, `runtime/core/autodiff.cpp`: scalar graph VJP/JVP/Jacobian and exact second-order Hessian. VJP/JVP and Hessian are checked against finite differences in `tests/test_ai_m2.cpp`. These operations are not compiler IR passes and do not provide tensor-wide activation checkpointing. |
| 30 | `@recompute` activation checkpointing | **NOT IMPLEMENTED** | No compiler rematerialization pass. |
| 31 | Zero-copy KV cache and radix prefix cache | **PARTIAL** | `runtime/core/kv_cache.hpp`, `runtime/core/kv_cache.cpp`: paged KV storage, shared prefix pages, longest-prefix lookup, and copy-on-write for partial pages. It is not wired into a model-serving scheduler under `stdlib/ai/infer/`. |
| 32 | Speculative decoding and continuous batching | **NOT IMPLEMENTED** | No serving scheduler or draft/target decoder. |
| 33 | Bit-packed SIMD micro-kernels | **PARTIAL** | `runtime/core/quantized.cpp` implements base-3 packed trits and numerically checked scalar trit matmul. `compiler/backends/arm64/neon_dot.S` is a real NEON float dot-product kernel used by ARM64 attention. Requested NEON/SVE2 sdot/udot/mmla BitNet and 4-bit matmul kernels are absent. |
| 34 | BPE, Unigram, and byte-level tokenizer | **PARTIAL** | `runtime/core/tokenizer.hpp`, `runtime/core/tokenizer.cpp`: ranked byte BPE and Unigram Viterbi encoding return token spans backed by the input buffer. There is no `.ax` standard-library wrapper or full normalization/pre-tokenization pipeline. |
| 35 | LoRA, QLoRA, and DoRA adapters | **NOT IMPLEMENTED** | No adapter IR or weight-freeze lowering. |
| 36 | Static compute graph capture | **NOT IMPLEMENTED** | No graph record/replay engine. |
| 37 | Automatic mixed precision | **NOT IMPLEMENTED** | No loss-scaling or master-weight compiler pass. |
| 38 | SafeTensors, GGUF, and ONNX I/O | **PARTIAL** | `runtime/core/model_io.hpp`, `runtime/core/model_io.cpp`: SafeTensors writing and mmap-backed reading with bounds and layout checks. GGUF and ONNX parsing/export are absent. |
| 39 | Heterogeneous CPU/GPU/NPU/TPU scheduler | **NOT IMPLEMENTED** | Existing HAL registry does not contain a device cost model or subgraph splitter. |
| 40 | Polyhedral loop optimizer and auto-tiler | **NOT IMPLEMENTED** | No loop interchange/skewing/vectorization pass. |
| 41 | Compile-time roofline analyzer | **NOT IMPLEMENTED** | No FLOPs/bytes estimator or bound diagnostics. |
| 42 | Async double-buffered DMA | **NOT IMPLEMENTED** | No host-device ping-pong transfer runtime. |
| 43 | ARM64 SVE2 and SME codegen | **PARTIAL** | `compiler/backends/arm64/neon_dot.S` adds a real NEON dot kernel and is wired into the ARM64 runtime build. SVE2/SME codegen and dispatch are absent; only the existing baseline ARM64 emitter was syntax-assembled here. |
| 44 | Apple Metal, AMX, and ANE | **NOT IMPLEMENTED** | No MSL emitter or Apple command-buffer backend. |
| 45 | x86 AVX-512, AMX, and Intel Xe/SYCL | **PARTIAL** | Existing `compiler/backends/x86_64/` native host backend builds AX programs. AVX-512/AMX and SYCL/Xe lowering are absent. |
| 46 | WebGPU WGSL and WebAssembly SIMD128 | **NOT IMPLEMENTED** | No WGSL or WASM emitter/CLI output. |
| 47 | RISC-V Vector 1.0 | **NOT IMPLEMENTED** | No RVV emitter or `vsetvli` output path. |
| 48 | FPGA/HLS pipeline export | **NOT IMPLEMENTED** | No SystemVerilog/HLS exporter. |

## Required benchmarks

| Benchmark | Result |
|---|---|
| ARM64 NEON BitNet/trit matmul | **NOT RUN** — no ARM64 runtime available; current trit matmul is scalar. |
| ARM64 NEON RoPE | **NOT RUN** — no ARM64 runtime available; dedicated NEON RoPE kernel is absent. |
| ARM64 NEON GEMM | **NOT RUN** — no ARM64 runtime available; the new NEON dot kernel is used by attention, not a GEMM benchmark. |

No ARM64 performance numbers are reported because the code could not be executed on an ARM64 machine. Host x86_64 measurements would not satisfy this benchmark requirement.

## Gate 2 decision

The host build, existing M1 unit tests, M2 runtime tests, AX example suite, source scans, and AArch64 assembly syntax checks passed. Gate 2 remains **NOT PASSED** because a full ARM64 runtime build/run, the listed ARM64 benchmarks, GGUF/ONNX, the missing model/inference features, and the requested accelerator emitters have not been implemented and verified. Do not treat this report as Milestone 2 completion or begin Milestone 3.
