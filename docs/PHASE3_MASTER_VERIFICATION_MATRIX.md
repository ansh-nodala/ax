# Phase 3 Master Verification Matrix — Features 1–110

**Overall verification: FAIL. Fully verified features: 0/110.** `PARTIAL` means some runtime/compiler support exists but at least one requested capability or its required verification is missing; it does not count as a passing feature. `FAIL` means the requested implementation/test is absent or the evidence does not establish it. No ARM64 run was available on this x86_64 host.

The M1–M4 rows below consolidate the evidence and limitations in [M1](MILESTONE_1_REPORT.md), [M2](MILESTONE_2_REPORT.md), [M3](MILESTONE_3_REPORT.md), and [M4](MILESTONE_4_REPORT.md). M5 details are in [MILESTONE_5_REPORT.md](MILESTONE_5_REPORT.md).

Post-milestone update: the repository now has a tested Vulkan `@gpu` execution slice for fixed-shape `f32` elementwise operations, activations, tiled rank-2 matmul, softmax, RMSNorm, and layernorm. See [GPU_GUIDE.md](GPU_GUIDE.md) and the latest section of [VERIFICATION_REPORT.md](../VERIFICATION_REPORT.md). This does not mark any broader requested feature as complete: CUDA, ROCm, TPU, mixed precision, attention, and multi-device scheduling remain unimplemented or partial.

| # | Feature | Result | Implementation paths | Test file / evidence |
|---:|---|---|---|---|
| 1 | `comptime` evaluator | FAIL | — | — |
| 2 | Hygienic AST/procedural macros | PARTIAL | `compiler/parser/parser.cpp` | — (macro expansion/hygiene untested) |
| 3 | Algebraic effects/handlers | FAIL | — | — |
| 4 | Linear/affine resource types | FAIL | `compiler/sema/` (no exact-once tracking) | — |
| 5 | Refinement constraints | FAIL | `compiler/sema/type_inference.hpp` (no `where` solver) | — |
| 6 | Exhaustive pattern matching | FAIL | — | — |
| 7 | Pipe operator/UFCS | FAIL | `compiler/parser/expr_parser.hpp` (no pipe/UFCS) | — |
| 8 | Custom/Unicode operators | PARTIAL | `compiler/lexer/utf8.hpp` | — (operator declarations/precedence absent) |
| 9 | Named/default arguments | FAIL | `compiler/parser/parser.cpp` (positional calls only) | — |
| 10 | Incremental content-addressed cache | FAIL | — | — |
| 11 | LTO/PGO | FAIL | `tools/axc/cli.cpp` (no `--lto`/`--pgo`) | — |
| 12 | Stage-2 bootstrap | FAIL | — (`bootstrap/` absent) | — |
| 13 | Pluggable allocators | PARTIAL | `runtime/core/arena.hpp`, `runtime/core/arena.cpp` | `tests/test_runner.cpp` (arena not checked against all allocator modes) |
| 14 | Scoped memory regions | FAIL | — | — |
| 15 | Alignment/cache-isolation directives | FAIL | — | — |
| 16 | Source prefetch directives | PARTIAL | `compiler/backends/arm64/*.S` (manual `prfm`, no AX directives) | — |
| 17 | Zero-copy mmap slices | FAIL | `runtime/core/os.cpp` (basic OS helpers only) | — |
| 18 | Packed/C/SoA/AoSoA layouts | PARTIAL | `compiler/parser/parser.cpp` (struct syntax only) | `tests/test_runner.cpp` (no layout verification) |
| 19 | Lock-free atomics/epoch reclamation | FAIL | `runtime/` (atomics only; no epoch subsystem) | — |
| 20 | Constrained inline ARM64 asm | FAIL | — | — |
| 21 | Hardware performance counters | FAIL | — | — |
| 22 | Sanitizer flags | FAIL | `tools/axc/cli.cpp` (no `--sanitize`) | — |
| 23 | Bare-metal/`no_std` mode | FAIL | — | — |
| 24 | Low-bit types and packed conversions | PARTIAL | `runtime/core/quantized.hpp`, `runtime/core/quantized.cpp` | `tests/test_ai_m2.cpp` |
| 25 | FlashAttention/PagedAttention | PARTIAL | `runtime/core/ai_kernels.hpp`, `runtime/core/ai_kernels.cpp` | `tests/test_ai_m2.cpp` (CPU tiled attention only) |
| 26 | RoPE/ALiBi kernels | PARTIAL | `runtime/core/ai_kernels.cpp`, `runtime/core/axrt.cpp` | `tests/test_ai_m2.cpp` (no dedicated NEON kernel) |
| 27 | SwiGLU/GeGLU/RMSNorm/GQA | PARTIAL | `runtime/core/ai_kernels.cpp`, `runtime/core/axrt.cpp`, `compiler/backends/arm64/arm64_backend.cpp`, `compiler/backends/x86_64/x86_64_backend.cpp` | `tests/test_ai_m2.cpp` |
| 28 | MoE routing/load balancing | PARTIAL | `runtime/core/ai_kernels.hpp`, `runtime/core/ai_kernels.cpp` | `tests/test_ai_m2.cpp` (no compiler IR pass) |
| 29 | Higher-order autodiff | PARTIAL | `runtime/core/autodiff.hpp`, `runtime/core/autodiff.cpp` | `tests/test_ai_m2.cpp` (scalar finite-difference checks) |
| 30 | Activation checkpointing | FAIL | — | — |
| 31 | Paged KV/radix prefix cache | PARTIAL | `runtime/core/kv_cache.hpp`, `runtime/core/kv_cache.cpp` | `tests/test_ai_m2.cpp` |
| 32 | Speculative decoding/continuous batching | FAIL | — | — |
| 33 | Bit-packed SIMD microkernels | PARTIAL | `runtime/core/quantized.cpp`, `compiler/backends/arm64/neon_dot.S` | `tests/test_ai_m2.cpp` (trit matmul is scalar) |
| 34 | BPE/Unigram tokenizer | PARTIAL | `runtime/core/tokenizer.hpp`, `runtime/core/tokenizer.cpp` | `tests/test_ai_m2.cpp` |
| 35 | LoRA/QLoRA/DoRA adapters | FAIL | — | — |
| 36 | Static compute graph capture | FAIL | — | — |
| 37 | Automatic mixed precision | FAIL | — | — |
| 38 | SafeTensors/GGUF/ONNX I/O | PARTIAL | `runtime/core/model_io.hpp`, `runtime/core/model_io.cpp` | `tests/test_ai_m2.cpp` (SafeTensors only) |
| 39 | Heterogeneous device scheduler | FAIL | `compiler/backends/hal/hal.cpp` (registry only) | — |
| 40 | Polyhedral loop optimizer | FAIL | `compiler/opt/` (no polyhedral pass) | — |
| 41 | Compile-time roofline analyzer | FAIL | — | — |
| 42 | Double-buffered async DMA | FAIL | — | — |
| 43 | ARM64 SVE2/SME codegen | PARTIAL | `compiler/backends/arm64/arm64_backend.cpp`, `compiler/backends/arm64/neon_dot.S`, `runtime/core/topology.cpp` | `tests/test_runner.cpp` (baseline emitter only) |
| 44 | Metal/AMX/ANE backend | FAIL | — | — |
| 45 | AVX-512/AMX/Xe/SYCL backend | PARTIAL | `compiler/backends/x86_64/x86_64_backend.cpp` (host emitter only) | — (no requested ISA-specific test) |
| 46 | WGSL/WASM SIMD128 backend | FAIL | — | — |
| 47 | RISC-V Vector backend | FAIL | — | — |
| 48 | FPGA/HLS exporter | FAIL | — | — |
| 49 | 5D parallelism planner | FAIL | `runtime/dist/dist.cpp` (coordinator collective only) | `tests/test_m3.cpp` (collective only) |
| 50 | ZeRO/offload stages | FAIL | — | — |
| 51 | Elastic fault-tolerant training | FAIL | `runtime/dist/dist.cpp` (peer timeout fails collective) | `tests/test_m3.cpp` (no kill/recovery test) |
| 52 | Gradient compression | FAIL | — | — |
| 53 | Communication/compute overlap | FAIL | — | — |
| 54 | Decentralized P2P mesh | FAIL | `runtime/dist/dist.cpp` (rank-0 coordinator) | `tests/test_m3.cpp` (no peer mesh test) |
| 55 | Atomic streaming master checkpoint | PARTIAL | `runtime/dist/checkpoint.cpp`, `runtime/core/sha256.hpp` | `tests/test_m3.cpp` (bounded checkpoint, not distributed SafeTensors streaming) |
| 56 | Production model server | FAIL | — | — |
| 57 | Telemetry/dashboard | FAIL | — | — |
| 58 | Cluster deterministic seeding | PARTIAL | `runtime/core/philox.hpp` | `tests/test_m3.cpp` (worker partition check, not cross-device bit identity) |
| 59 | Archetype ECS | PARTIAL | `runtime/core/ecs.hpp` | `tests/test_m3.cpp` (100,000 entity fixed-signature SoA; no dynamic migration) |
| 60 | Ray/path tracing shaders | FAIL | — | — |
| 61 | Mesh/task shaders | FAIL | — | — |
| 62 | Vulkan render graph | PARTIAL | `runtime/core/rendergraph.hpp`, `runtime/core/rendergraph.cpp` | `tests/test_m3.cpp` (DAG/barrier/aliasing, no Vulkan integration) |
| 63 | Differentiable physics/rendering | PARTIAL | `runtime/core/physics.hpp`, `runtime/core/physics.cpp` | `tests/test_m3.cpp` (spring gradient only) |
| 64 | Deterministic fixed-point math | PARTIAL | `runtime/core/fixed.hpp` | `tests/test_m3.cpp` (Q16.16/Q32.32 subset) |
| 65 | SIMD collision/physics | PARTIAL | `runtime/core/physics.hpp`, `runtime/core/physics.cpp` | `tests/test_m3.cpp` (scalar BVH raycast only) |
| 66 | Skeletal animation/IK | FAIL | — | — |
| 67 | Procedural generation kernels | FAIL | — | — |
| 68 | Real-time audio DSP | FAIL | — | — |
| 69 | Live hot reload | FAIL | — | — |
| 70 | Asset cooker | PARTIAL | `runtime/core/asset_cooker.hpp`, `runtime/core/asset_cooker.cpp`, `tools/axc/cli.cpp` | `tests/test_m3.cpp`, `tests/test_cook_cli.sh` (OBJ/WAV/PNG subset) |
| 71 | Structured concurrency | PARTIAL | `runtime/coro/coro.hpp`, `runtime/coro/coro.cpp` | `tests/test_m4.cpp` (cooperative task scope; no compiler lifetime enforcement) |
| 72 | SPSC/MPMC/select channels | PARTIAL | `runtime/coro/channel.hpp`, `runtime/coro/channel.cpp` | `tests/test_m4.cpp` (bounded queues; polling select) |
| 73 | io_uring/network protocols | PARTIAL | `runtime/coro/io_uring.hpp`, `runtime/coro/io_uring.cpp` | — (no TCP/HTTP2/WebSocket/MQTT round-trip test) |
| 74 | Actor model | PARTIAL | `runtime/coro/actor.hpp` | `tests/test_m4.cpp` (mailbox load; no supervision trees) |
| 75 | Software transactional memory | FAIL | — | — |
| 76 | Real-time priority coroutines | FAIL | — | — |
| 77 | Zero-copy wire serialization | FAIL | — | — |
| 78 | gRPC/WebSocket/MQTTv5 | FAIL | — | — |
| 79 | AF_XDP hooks | FAIL | — | — |
| 80 | Capability sandbox | FAIL | — | — |
| 81 | Static bounds-check elimination | FAIL | — | — |
| 82 | Contracts/formal verification | FAIL | — | — |
| 83 | Constant-time secret checking | FAIL | — | — |
| 84 | Hardware crypto suite | PARTIAL | `runtime/core/sha256.hpp` | `tests/test_m4.cpp` (SHA-256 `abc` only; required algorithms absent) |
| 85 | Coverage-guided fuzzer/property tests | FAIL | — | — |
| 86 | Send/Sync race checking | FAIL | — | — |
| 87 | Cryptographic package lockfile | PARTIAL | `tools/axpkg/main.cpp`, `runtime/core/sha256.hpp` | `tests/test_axpkg_lock.sh` |
| 88 | Automatic memory zeroization | PARTIAL | `runtime/core/secure_memory.hpp`, `runtime/core/secure_memory.cpp` | `tests/test_m4.cpp` (runtime wipe only; no compiler insertion/ARM assembly proof) |
| 89 | C header importer | FAIL | — | — |
| 90 | C++20 interop bridge | FAIL | — | — |
| 91 | DLPack tensor bridge | FAIL | — | — |
| 92 | Rust FFI/crate linking | FAIL | `tools/axpkg/main.cpp` (lockfile only) | `tests/test_axpkg_lock.sh` (not Rust FFI) |
| 93 | Android JNI binder | FAIL | — | — |
| 94 | Python-to-AX migration | FAIL | — | — |
| 95 | Embeddable C API | PARTIAL | `compiler/embed/embed.cpp`, `include/ax_embed.h`, `include/ax_runtime.h`, CMake target `ax_embed` | `tests/test_embed_c.c` |
| 96 | C-ABI shared library exports | PARTIAL | `tools/axc/cli.cpp`, `include/ax_runtime.h` | `tests/test_shared_cli.sh` (Linux `.so`/C caller; no `.a` mode) |
| 97 | JIT REPL | FAIL | — | — |
| 98 | `.axnb` notebook engine | FAIL | — | — |
| 99 | Visual diagnostics/auto-fix | FAIL | `compiler/common/diagnostics.cpp` (plain diagnostics only) | — |
| 100 | Flamegraph/memory visualizer | FAIL | — | — |
| 101 | Side-by-side source inspector | FAIL | `tools/axc/cli.cpp` (`emit-*` commands only) | — |
| 102 | Documentation generator/doctests | FAIL | — | — |
| 103 | Cross-compilation target matrix | FAIL | `tools/axc/cli.cpp` (host selection only) | — |
| 104 | Statistical benchmark comparison | PARTIAL | `tools/axc/cli.cpp` (basic coroutine/matmul benchmark) | — (no baseline/statistics test) |
| 105 | Workspace/package manager | PARTIAL | `tools/axpkg/main.cpp`, `axpkg.toml` | `tests/test_axpkg_lock.sh` (lock only; no workspace resolution) |
| 106 | Offline diagnostic handbook | FAIL | — | — |
| 107 | Deterministic replay debugger | FAIL | `tools/axdbg/main.cpp` (interactive placeholder commands only) | — |
| 108 | Binary size/tree-shaker | PARTIAL | `compiler/opt/dce.cpp` (MIR instruction DCE only) | `tests/test_runner.cpp` (no whole-program size test) |
| 109 | Pro IDE/LSP features | PARTIAL | `tools/axedit/main.cpp` adds a native terminal editor with colored syntax, file-local completion, signatures, and basic type hints; `tools/axlsp/main.cpp` remains a limited prototype. | Manual PTY check of completion acceptance and saving; no automated editor/LSP integration test. |
| 110 | Project scaffolder templates | FAIL | `tools/axpkg/main.cpp` (no `new --template`) | — |

## Gate evidence summary

| Check | Result |
|---|---|
| Linux ARM64 clean build and execution | **FAIL / unavailable** — current host is x86_64; `scripts/build_arm64.sh` exits 2 with an explicit architecture message. |
| Host clean `-Wall -Wextra` build | **PASS** — fresh Linux x86_64 build in `/tmp/ax-lang-native-v2-m5`. |
| Current CTest run | **PASS** — 9/9 suites, including M1–M4 suites, embed C API, shared-library caller, examples, cooker, and package lock. |
| Zero Python in `compiler/`, `tools/`, `runtime/` | **PASS** — 0 files. |
| No empty files in repository | **PASS** — 0 files. |
| No `TODO|FIXME|stub|not implemented` in required source directories | **PASS** — 0 matches. |
| Requested 1,500–2,500+ repository files | **FAIL** — the M5 audit returned 691 before the archive; the latest source package has 446 files. |
| Full dedicated 1–110 regression set | **FAIL** — many features have no implementation or feature test; the passing CTest suites do not cover all 110. |
