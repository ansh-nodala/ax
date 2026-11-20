# AX Language — Learn, Build, and Explore

AX is an experimental, indentation-sensitive language with a native compiler and a C++ runtime. This guide is written for people who are opening the repository for the first time. It shows how to build and run `.ax` programs, explore the tensor and AI runtime, generate backend output, and understand which parts are ready to use.

> **Project status:** this repository is not a completed 110-feature production language. A small Vulkan `@gpu` kernel path is now executable, but many requested language features, TPU support, and the CUDA/ROCm execution paths remain partial or absent. Read the [feature verification matrix](docs/PHASE3_MASTER_VERIFICATION_MATRIX.md) before relying on a capability.

The same material is available as an [offline interactive guide](index.html). It uses local HTML, CSS, and JavaScript only.

## Install AX for your terminal

Linux x86_64 and ARM64 release packages are published on the [GitHub Releases page](https://github.com/ansh-nodala/ax/releases). The installer uses a verified release archive when available, otherwise it builds the latest source for the machine:

```bash
curl -fsSL https://raw.githubusercontent.com/ansh-nodala/ax/main/scripts/install.sh -o /tmp/ax-install.sh
bash /tmp/ax-install.sh
```

The default install goes under `~/.local`, adds its `bin` directory to common shell startup files, and needs no `sudo`. Open a new terminal, then compile from any working directory:

```bash
ax build /path/to/hello.ax -o /tmp/hello
/tmp/hello
ax edit /path/to/hello.ax
```

On Debian or Ubuntu, install the architecture-specific `.deb` from Releases with `sudo apt install ./ax-linux-x86_64.deb` or `ax-linux-aarch64.deb`. The native editor is installed with the compiler. AX packages target Linux; optional `@gpu` compilation still needs Vulkan shader tools and a compatible driver. See [install details](docs/INSTALL.md).

## 1. Build AX

### Requirements

- Linux x86_64 or Linux ARM64
- CMake 3.22 or newer
- C and C++ compilers with C++20 support (GCC or Clang)
- GNU Make or Ninja
- Optional for `@gpu`: Vulkan development files at CMake configure time, plus a Vulkan compute driver and `glslc` or `glslangValidator`

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

CMake builds the compiler (`build/ax` and `build/axc`), runtime archive, helper tools, and native test programs. The build uses `-Wall -Wextra`. To run the existing regression suite:

```bash
ctest --test-dir build --output-on-failure
```

The strict ARM64 build script must run on a **native Linux ARM64 host**:

```bash
bash scripts/build_arm64.sh
```

It performs a clean build, runs CTest, and runs `hello.ax`. On x86_64 it stops with a clear architecture message; that result is not an ARM64 build.

## 2. Make `ax` available in your terminal

After building, source the helper from the repository root:

```bash
source scripts/ax-env.sh
```

This adds the project's `build/` directory to the current shell's `PATH`. If an older build contains `build/axc` but not `build/ax`, the helper creates a small compatibility link. The setup lasts for that terminal session.

Check the command:

```bash
ax --help
```

You can use `axc` instead; it is kept for existing scripts.

## 3. Compile and run your first program

Create `hello.ax`:

```ax
fn main() -> i32:
    print("Hello from AX")
    return 0
```

Compile to a native executable and run it:

```bash
ax build hello.ax -o /tmp/hello
/tmp/hello
```

Or use the one-step command:

```bash
ax run hello.ax
```

`ax build` selects the host backend: x86_64 on x86_64 Linux, ARM64 on ARM64 Linux. `ax run` compiles and then runs the generated executable.

### The AX shape of a program

- `fn main() -> i32:` declares the entry function and its return type.
- A colon starts an indented block. Use four spaces per indentation level.
- `name: f32 = ...` declares a typed local value. `name = ...` can infer a local's type in supported cases.
- `print(...)` writes a value to standard output.
- `return 0` ends `main` successfully.

Here is a slightly larger program from [`examples/compiled_demo.ax`](examples/compiled_demo.ax):

```ax
fn main() -> i32:
    first: f32 = 19.0
    second: f32 = 23.0
    total = first + second
    print("This program was compiled by axc:")
    print(total)
    return 0
```

Build it with `ax build examples/compiled_demo.ax -o /tmp/ax-demo`, then run `/tmp/ax-demo`; it prints `42`.

## 4. Learn the language in a useful order

Start with these checked-in examples:

| Learn | File | What to look for |
|---|---|---|
| Functions, types, printing | [`examples/hello.ax`](examples/hello.ax) | `main`, typed locals, arithmetic |
| Scalar math and tensors | [`examples/matmul.ax`](examples/matmul.ax) | Tensor literals, elementwise math, loops |
| Tensor activation functions | [`examples/softmax_demo.ax`](examples/softmax_demo.ax), [`examples/layernorm_demo.ax`](examples/layernorm_demo.ax) | Runtime tensor operations |
| AX-to-runtime AI bindings | [`examples/m2_runtime.ax`](examples/m2_runtime.ax) | RoPE, RMSNorm, SwiGLU, GeGLU calls |
| Language/backend smoke coverage | [`examples/all_features.ax`](examples/all_features.ax) | A broad example suite; some blocks are syntax/codegen smoke checks only |
| Shared C ABI | [`examples/shared_math.ax`](examples/shared_math.ax) | Exporting a callable function |

The grammar currently used by examples includes functions, local variables, arithmetic/comparisons, `if`/`else`, `for` with `range`, `while`, `parallel for`, tensor literals, and calls to built-in runtime operations. Struct, enum, trait, and macro-shaped declarations appear in examples, but do not assume that every related language feature has complete semantics. For exact status, use the matrix.

### A tensor example

```ax
fn main() -> i32:
    values: Tensor[f32, 4] = tensor([1.0, 2.0, 3.0, 4.0])
    print(softmax(values))
    return 0
```

Save it as `softmax.ax`, then run `ax check softmax.ax` and `ax run softmax.ax`. Shapes are checked for supported tensor expressions; a deliberate mismatch is in [`examples/bad_shape.ax`](examples/bad_shape.ax):

```bash
ax check examples/bad_shape.ax   # expected to report a shape error
```

## 5. AI and LLM work: what you can use today

AX currently gives you two related layers:

1. **AX source programs** can create small tensors and call supported runtime operations. The runtime bindings include operations such as `matmul`, `gemm`, `relu`, `softmax`, `layernorm`, `rmsnorm`, `rope`, `swiglu`, `geglu`, `gelu`, `silu`, `sigmoid`, `tanh`, `mse_loss`, `cross_entropy`, and `adamw`.
2. **Native C++ runtime APIs** expose additional research/engine components. These are not all available as importable `.ax` modules.

Run the AX-side inference primitive example:

```bash
ax run examples/m2_runtime.ax
```

This is the fastest way to see AI operations called from `.ax`: it exercises RMSNorm, RoPE (including a position offset), SwiGLU, and GeGLU through native runtime bindings. It is a kernel/binding demo, not a trained language model.

The C++ runtime headers include:

| Area | Header | Available API examples |
|---|---|---|
| Attention and model kernels | `runtime/core/ai_kernels.hpp` | RoPE with position offsets, ALiBi bias, RMSNorm, SwiGLU, GeGLU, tiled attention, GQA, MoE routing |
| Quantized values | `runtime/core/quantized.hpp` | Pack/unpack helpers for i1, ternary, i2, i4, FP4, FP8, and BF16; scalar packed-trit matmul |
| Automatic differentiation | `runtime/core/autodiff.hpp` | C++ `DiffGraph` evaluate, VJP, JVP, Jacobian, and Hessian |
| Tokenization | `runtime/core/tokenizer.hpp` | Byte BPE and Unigram tokenization |
| KV cache | `runtime/core/kv_cache.hpp` | Paged cache and radix prefix sharing |
| Model files | `runtime/core/model_io.hpp` | SafeTensors writer and mmap-backed reader |

### Game and systems runtime

Game-oriented components currently live in C++ headers: [`runtime/core/ecs.hpp`](runtime/core/ecs.hpp) has dense SoA storage for one fixed component signature; [`runtime/core/physics.hpp`](runtime/core/physics.hpp) has spring gradients and triangle BVH build/raycast; [`runtime/core/fixed.hpp`](runtime/core/fixed.hpp) has Q16.16/Q32.32 arithmetic and CORDIC sine/cosine; [`runtime/core/rendergraph.hpp`](runtime/core/rendergraph.hpp) plans pass dependencies, barriers, and resource aliasing.

Concurrency APIs include `TaskScope` in `runtime/coro/coro.hpp`, bounded queues in `runtime/coro/channel.hpp`, and a bounded actor mailbox in `runtime/coro/actor.hpp`. Localhost all-reduce and checkpoint APIs are under `runtime/dist/`. These are runtime libraries, not fully wired `.ax` language features: for example, `examples/game_loop.ax`, `examples/coro.ax`, and `examples/distribute_demo.ax` are smoke examples, not complete ECS, async-network, or distributed-training programs.

For example, the C++ differentiation graph can compute the derivative of `x*x`:

```cpp
#include "runtime/core/autodiff.hpp"

int main() {
    ax::ai::DiffGraph graph(1);
    const auto x = graph.input(0);
    const auto y = graph.multiply(x, x);
    const auto gradient = graph.vjp({3.0}, y);
    // gradient[0] is 6.0
    return gradient[0] == 6.0 ? 0 : 1;
}
```

Use these APIs from a C++ target linked to the CMake `axrt` target. The current repository does **not** provide a finished LLM trainer, model server, Python/PyTorch bridge, or complete model-loading stack. SafeTensors is implemented; GGUF and ONNX are not. The directories under `stdlib/` are currently category folders, not a populated standard library of importable AX packages.

A native kernel can also be called directly from C++:

```cpp
#include "runtime/core/ai_kernels.hpp"
#include <array>

int main() {
    const std::array<float, 4> input{1.0f, 2.0f, 3.0f, 4.0f};
    const auto output = ax::ai::rms_norm(input, 1, input.size());
    return output.size() == input.size() ? 0 : 1;
}
```

Put this in a CMake executable and link `axrt`; the runtime returns a new `std::vector<float>` here, so the result is owned by the caller.

### Use a coding AI with this repository

An AI assistant can explain examples or draft a small change. First give it the project's real boundaries. For example, paste this prompt into your coding assistant from the project root:

```text
You are helping me use this AX language repository. First read README.md and
docs/PHASE3_MASTER_VERIFICATION_MATRIX.md. Use only commands, syntax, and APIs
that exist in this checkout. Do not assume roadmap features are implemented.
For an AX change, make the smallest edit, then tell me to run:
  source scripts/ax-env.sh
  ax check <file.ax>
  ax run <file.ax>
If the task needs C++ runtime APIs, show the exact header and CMake target.
Explain each step in beginner-friendly language and call out unsupported parts.
```

Then ask one focused question at a time, such as “Explain `examples/m2_runtime.ax` line by line” or “Help me make one RMSNorm example.” Run `ax check`/`ax run` yourself and compare new APIs with the feature matrix. A passing small example does not mean a whole roadmap feature is complete.

### Compile a real Vulkan GPU kernel

`@gpu` is a strict Vulkan compute path. It detects physical Vulkan compute devices, compiles supported kernels to SPIR-V, and embeds them in the native executable. Intel Arc was verified here; other GPUs require compatible Vulkan compute drivers. The vendor driver compiles SPIR-V for the device.

```bash
ax devices
ax check examples/gpu_ai_kernels.ax
ax build examples/gpu_ai_kernels.ax -o /tmp/ax-gpu-ai.axg
/tmp/ax-gpu-ai.axg
```

The example runs multiple kernels and prints vector arithmetic, a tiled GEMM result, softmax, RMSNorm, layernorm, SwiGLU, ReLU, sigmoid, SiLU, and GELU outputs. `.axg` is AX's suggested executable suffix; the output is a native executable with SPIR-V embedded. A physical GPU, Vulkan loader/driver, and `glslc` or `glslangValidator` are required. If no GPU is detected, `ax check` and `ax build` fail; an `@gpu` kernel never falls back to CPU.

If `ax devices` lists multiple GPUs, set `AX_GPU_DEVICE=1` (or another listed index) before compiling and running to select one. Index `0` is the default.

Supported kernels use fixed positive dimensions and `f32` tensors. AX currently lowers elementwise expressions and ReLU/sigmoid/SiLU/GELU/tanh/sqrt/exp/log/SwiGLU/GeGLU; rank-2 `matmul(A[M,K], B[K,N])` to a shared-memory 16×16 tiled kernel; and row-wise softmax, RMSNorm, and layernorm to 128-lane reductions. The runtime reuses one Vulkan device and caches compiled pipelines within each process; it still allocates host-visible buffers and dispatch resources per call. CUDA, ROCm, and TPU still need their own real runtime integration and are not launched by AX. See [GPU guide](docs/GPU_GUIDE.md) for exact limits and implementation details.

### Edit AX source in the terminal

AX includes a native terminal editor with colored AX syntax, keyword/type/function highlighting, file-local symbol completions, signature suggestions, and live type hints. Build it with the project and launch it with either command:

```bash
ax edit examples/hello.ax
# or
axedit examples/hello.ax
```

Use `Ctrl-S` or `Ctrl-O` to save, `Ctrl-Q` or `Ctrl-X` to quit, `Tab` to accept a suggestion, `Ctrl-Space` to show completions, and arrow keys or `Ctrl-N`/`Ctrl-P` to select suggestions. See [the editor guide](docs/EDITOR_GUIDE.md) for the full key list.

### AI and accelerator workflow

Use `ax check` and `ax run` to verify CPU behavior first. The emitter commands generate backend output for inspection or a separate toolchain:

```bash
ax emit-ir examples/m2_runtime.ax > /tmp/model.mir
ax emit-asm examples/matmul.ax > /tmp/matmul-arm64.s
ax emit-ptx examples/cuda_kernel.ax > /tmp/kernel.ptx
ax emit-rocm examples/rocm_kernel.ax > /tmp/kernel.hip
ax emit-stablehlo examples/tpu_kernel.ax > /tmp/kernel.mlir
ax emit-spirv examples/gpu_ai_kernels.ax -o /tmp/gpu.spv
```

Emitter output is **not the same as running on a GPU/NPU**. The executable `@gpu` Vulkan subset is documented above. `@cuda`, `@rocm`, and `@tpu` still generate PTX, HIP, or StableHLO output for external toolchains and are not executed by `ax build`. TPU execution needs an XLA/PJRT deployment. `ax emit-spirv` compiles `@gpu` functions to real SPIR-V; without one, its legacy default output remains a minimal module. There is no automatic CPU/GPU scheduler.

## 6. Command reference

Run `ax --help` for the list from the built compiler. The common commands are:

| Command | Use |
|---|---|
| `ax devices` | List physical Vulkan GPUs with compute queues |
| `ax build file.ax -o program` | Compile a `.ax` source file to a native host executable |
| `ax build file.ax --shared -o libname.so` | Build a shared library and generate a neighboring C header |
| `ax run file.ax` | Build and run a source file |
| `ax check file.ax` | Parse, type-check, and shape-check without linking |
| `ax emit-ir file.ax` | Print the optimized MIR representation |
| `ax emit-asm file.ax` | Print AArch64 assembly, even when the host is x86_64 |
| `ax emit-ptx file.ax` | Print PTX and a host launcher stub/output |
| `ax emit-rocm file.ax` | Print HIP/ROCm emitter output |
| `ax emit-stablehlo file.ax` | Print StableHLO/MLIR output |
| `ax emit-spirv file.ax -o file.spv` | Compile a supported `@gpu` kernel to SPIR-V; without one, write the legacy minimal module |
| `ax cook asset.obj -o asset.axb` | Cook supported OBJ, PCM WAV, or PNG assets to an AXB bundle |
| `ax bench` | Run a small coroutine and vector-dot smoke benchmark; not a statistics suite |
| `ctest --test-dir build --output-on-failure` | Run all registered repository test suites |

Commands such as `ax repl`, `ax migrate`, `ax inspect`, `ax doc`, `ax profile`, `ax watch`, and `axpkg new` are not implemented in this checkout. The compiler also does not have a general `--target`, `--sanitize`, `--lto`, or `--pgo` driver option.

`ax test` is a shortcut for `./build/ax_tests` and should be run from the repository root. For package operations, the separate binary is `axpkg`:

```bash
axpkg init
axpkg list
axpkg add package_name
axpkg remove package_name
axpkg lock
axpkg verify
```

The current `axpkg` supports basic TOML edits and a SHA-256 source-tree lock/verify workflow; it does not resolve or download dependency graphs. Run `axpkg init` only in a new project directory: it truncates and rewrites an existing `axpkg.toml` in the current directory.

## 7. C embedding and shared-library use

### Export an AX function to a C ABI shared object

```bash
ax build examples/shared_math.ax --shared -o /tmp/libshared_math.so
```

This creates `/tmp/libshared_math.so` and `/tmp/libshared_math.h`. The generated C ABI passes values as `AxVal*`, not as arbitrary C scalar types. A small C caller can include both generated files:

```c
#include "ax_runtime.h"
#include "libshared_math.h"

int main(void) {
    AxVal* answer = ax_scale(ax_scalar(21.0), ax_scalar(2.0));
    ax_print(answer);
    return 0;
}
```

Compile it with the C compiler and the generated library (replace `/path/to/project` with this checkout):

```bash
cc -I/path/to/project/include -I/tmp caller.c \
  -L/tmp -lshared_math -Wl,-rpath,/tmp -o /tmp/caller
/tmp/caller
```

The shared build currently supports `.so` output and up to six `AxVal*` parameters per export. It does not generate general typed C marshalling or a static `.a` export library. The current `AxVal` ABI has no public value-release function, so repeated allocations from a long-lived C host should be treated as experimental.

### Embed the compiler API

`include/ax_embed.h` declares the C API. The CMake target `ax_embed` builds `libax_embed.a`. The API can check source and emit MIR or host/ARM64 assembly. For operations returning an `AxEmbedBuffer`, release it with `ax_embed_buffer_release`:

```c
#include "ax_embed.h"
#include <stdio.h>

int main(void) {
    AxEmbedBuffer output = {0};
    const char* source = "fn main() -> i32:\n    return 0\n";
    const int result = ax_embed_emit_ir(source, "embedded.ax", &output);
    if (result == 0) puts(output.data);
    ax_embed_buffer_release(&output);
    return result;
}
```

Return codes are `0` for success, `1` for AX source diagnostics, and `2` for an API/internal error. This API does not provide in-process JIT execution.

## 8. The other developer tools

- `axedit file.ax` opens AX's native terminal editor. It provides colored syntax, file-local completions, signatures, and declaration-based type hints; use `ax edit file.ax` for the same editor through the main CLI.
- `axfmt file.ax [-o formatted.ax]` normalizes indentation and whitespace. Review its output; it is a lightweight formatter, not a full syntax-aware rewrite engine.
- `axlint file.ax` reports simple style/unused-variable warnings. Its checks are heuristic.
- `axlsp` speaks a limited JSON-RPC-over-stdio subset with static completion and hover responses. It is not the requested production LSP with semantic diagnostics and navigation.
- `axdbg` is a basic command-loop prototype, not an executing step debugger or record/replay tool.
- `editors/vscode/` registers AX file syntax metadata. The included editor files should be treated as starter/prototype integrations.
- There is no built-in JIT REPL, notebook runner, Python-to-AX migrator, auto-documentation generator, flamegraph profiler, or full cross-compilation matrix yet.

## 9. How the repository is organized

| Path | Contents |
|---|---|
| `compiler/lexer/` | Indentation-aware lexer and token handling |
| `compiler/parser/` | AST and declaration/expression parser |
| `compiler/sema/` | Name/type and tensor-shape checks |
| `compiler/ir/`, `compiler/opt/` | MIR construction and basic optimization passes |
| `compiler/backends/` | x86_64 and ARM64 native emitters plus PTX, ROCm, StableHLO, and SPIR-V paths |
| `compiler/embed/`, `include/` | C embedding API and runtime ABI headers |
| `runtime/core/` | Tensor/runtime, AI kernels, tokenizer, KV cache, model I/O, game and system utilities |
| `runtime/core/vulkan_gpu.*` | Vulkan device detection, GPU dispatch, and buffer/pipeline management |
| `runtime/coro/` | Coroutine/task, channel, actor, and io_uring support |
| `runtime/dist/` | Localhost collectives and checkpoint support |
| `tools/` | `ax`/`axc`, `axedit`, `axpkg`, formatter, linter, LSP prototype, debugger prototype |
| `examples/` | Small AX programs, backend samples, and smoke demonstrations |
| `tests/` | C++ runtime/compiler tests, AX examples, and CLI integration scripts |
| `stdlib/` | Planned library categories; currently no complete importable AX standard library |
| `docs/` | GPU and editor how-to guides, milestone reports, and feature-by-feature verification matrix |

## 10. Troubleshooting

### `ax: command not found`

From the repository root, run `source scripts/ax-env.sh`. If neither `build/ax` nor `build/axc` exists, build first with the CMake commands above.

### `ax: compiler not found`

Check that you built this checkout's `build/` directory. If you used a custom CMake directory, set it before sourcing:

```bash
export AX_BUILD_DIR=/path/to/your/build
source scripts/ax-env.sh
```

### AArch64 assembly fails on x86_64

`ax emit-asm` deliberately emits ARM64 assembly. Do not feed that output to an x86 assembler. Use `ax build file.ax -o program` for native x86_64 compilation on an x86_64 host.

### A tensor shape is rejected

Use `ax check file.ax` before compiling. Compare the declared dimensions on both sides of each tensor operation. [`examples/bad_shape.ax`](examples/bad_shape.ax) is an intentional negative example.

### `@gpu` says no physical Vulkan GPU was found

Run `ax devices`. Check that the GPU driver exposes Vulkan compute. Software-only Vulkan devices such as `llvmpipe` are intentionally excluded. An `@gpu` function requires a physical supported GPU at compile time; it does not fall back to CPU. Kernel compilation also needs `glslc` or `glslangValidator` on `PATH`.

## 11. Current verification status

The prior full regression record used a fresh CMake/Ninja build with `-Wall -Wextra`: 87 build steps succeeded and 9/9 registered CTest suites passed. After the Vulkan GPU kernel expansion, the compiler rebuilt and `examples/gpu_ai_kernels.ax` compiled and ran on Intel Arc; the outputs include GEMM `[58, 64, 139, 154]`, normalized rows, activations, and elementwise arithmetic. The 9-suite CTest run was not repeated after these GPU changes. See [the verification report](VERIFICATION_REPORT.md) for exact commands and limits.

Linux ARM64 was not available in the authoring environment. The master audit currently marks **0/110 features as fully verified against the complete requested specifications**; some subcomponents are implemented and tested, but many features remain partial or absent. See [`docs/PHASE3_MASTER_VERIFICATION_MATRIX.md`](docs/PHASE3_MASTER_VERIFICATION_MATRIX.md) and the five milestone reports for exact evidence.
