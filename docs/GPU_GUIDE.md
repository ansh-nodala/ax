# AX GPU kernels (Vulkan)

AX has an executable GPU path for `@gpu` functions. It uses Vulkan compute and SPIR-V, so the same kernel source can target a physical GPU with a compatible Vulkan compute driver. Intel Arc was tested for this change; AMD/NVIDIA use depends on their installed driver exposing the required Vulkan compute features. The driver compiles SPIR-V to the device implementation. TPU execution is separate and is not provided by this feature.

## Check the device and toolchain

Run from the repository root:

```bash
source scripts/ax-env.sh
ax devices
```

`ax devices` lists physical Vulkan devices with a compute queue. Software Vulkan devices such as `llvmpipe` do not qualify. To compile an `@gpu` kernel, the compiler also needs `glslc` or `glslangValidator`; to run the generated executable, the target machine needs a Vulkan loader and compatible GPU driver.

When more than one physical GPU is listed, AX uses index `0` by default. Set `AX_GPU_DEVICE=1` (or another listed index) in the shell before compiling and running to select a device; AX checks that the index exists at compile time.

If there is no physical Vulkan GPU, `ax check` and `ax build` stop with a compile-time error. `@gpu` is strict: it never silently executes its body on the CPU.

## Write and run the first kernel

`examples/gpu_ai_kernels.ax` demonstrates a set of GPU kernels called directly from AX:

Build and execute it:

```bash
ax check examples/gpu_ai_kernels.ax
ax build examples/gpu_ai_kernels.ax -o /tmp/ax-gpu-ai.axg
/tmp/ax-gpu-ai.axg
```

The native executable contains its SPIR-V kernels and prints scale, vector add, matrix multiply, softmax, RMSNorm, layernorm, and SwiGLU results. For a minimal kernel, `examples/gpu_scale.ax` computes `values * 2.0 + 1.0`. The output is a native Linux executable; `.axg` is only AX's suggested filename suffix. It needs a compatible Vulkan GPU at run time too.

## Supported kernel subset

`@gpu` functions currently support these shapes and operations:

- Use fixed positive integer dimensions in `Tensor[f32, ...]` annotations. Symbolic dimensions, dynamic dimensions, and non-`f32` element types are rejected.
- Contain a single `return` expression; host-side AX code can call multiple kernels.
- Elementwise: one or two equal-shaped tensor inputs, same-shaped output, `+`, `-`, `*`, `/`, finite literals, and `relu`, `sigmoid`, `silu`, `gelu`, `tanh`, `sqrt`, `exp`, `log`, `swiglu`, or `geglu` calls.
- `matmul(A, B)`: two rank-2 tensors with `A[M,K]`, `B[K,N]`, and output `[M,N]`.
- `softmax(x)`, `rmsnorm(x)`, and `layernorm(x)`: one tensor, normalized over its final dimension, one workgroup per row.

The matrix multiplication kernel uses 16×16 workgroups, cooperatively loads two matrix tiles into workgroup shared memory, and accumulates with fused multiply-add. Normalization and softmax use 128-lane workgroups with shared-memory tree reductions. Elementwise kernels use 128-way dispatch and can fuse nested expressions such as `x * 2.0 + 1.0` or a SwiGLU gate into one kernel launch. The examples print numerical outputs for each of these paths.

The compiler lowers each supported function to a Vulkan compute shader, compiles it to SPIR-V at AX build time, embeds the shader and launch metadata in the executable, and emits a host wrapper that uploads inputs, dispatches the kernel, and downloads the result. Unsupported signatures and expressions fail during `ax check`/`ax build`. Each process reuses one Vulkan instance/device and caches compiled pipelines; input/output buffers, descriptors, and command pools are still created per call, using host-visible memory. This cuts repeated setup, but it is not yet a tuned high-throughput production runtime. Small tensors are usually faster on CPU because launch and transfer have fixed costs.

## Other backends and current boundaries

`@gpu` is the executable Vulkan path. NVIDIA and AMD GPUs can use it when their installed drivers expose a compatible Vulkan compute device; the driver compiles SPIR-V to that GPU's native instructions. Intel Arc was the only physical GPU available for verification here, so the other vendor/driver combinations remain unverified. This is a portable driver-compiled kernel path, not hand-written CUDA, ROCm, or vendor assembly, and it does not guarantee tensor-core use. `@cuda`/`@nvidia`, `@rocm`, and `@tpu` remain output-generation paths in this checkout; their emitted PTX/HIP/StableHLO is not executed by `ax build`. Direct CUDA execution needs a CUDA runtime path, ROCm needs HIP runtime/toolchain integration, and TPU use needs XLA/PJRT; none of those are implemented here.

The kernel is embedded into the native executable, not packaged as a standalone SPIR-V artifact. `ax emit-spirv` can separately write the compiled kernel for inspection or external tooling.
