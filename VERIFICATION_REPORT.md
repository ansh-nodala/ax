# AX v2 — Verification Report

## Environment

- Verified host: Linux x86_64, GNU C/C++ 15.2.0, CMake 4.2.3, Ninja.
- A native Linux ARM64 machine, AArch64 cross compiler, and QEMU are unavailable here.
- Build directory: `/tmp/ax-lang-native-v2-m5-clean`.

## Build and test evidence

Fresh host build:

```text
cmake --fresh -S . -B /tmp/ax-lang-native-v2-m5-clean -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS='-Wall -Wextra' -DCMAKE_CXX_FLAGS='-Wall -Wextra'
cmake --build /tmp/ax-lang-native-v2-m5-clean --parallel 2
Result: [87/87] built successfully; no warning or error diagnostics.
```

Regression run:

```text
ctest --test-dir /tmp/ax-lang-native-v2-m5-clean --output-on-failure
Result: 100% tests passed, 0 tests failed out of 9.

/tmp/ax-lang-native-v2-m5-clean/axc run hello.ax
AX compiled: hello.ax -> /tmp/ax_run
Namaste, AX!
```

The nine CTest entries include M1–M4 unit suites, the new C embedding API test, AX examples/emitter checks, cooker and package-lock CLI tests, and a C caller linked against the generated shared library. They do not cover every feature in the 110-feature request.

The source archive was extracted into a new temporary directory and independently rebuilt with the same clean flags: **87/87 build steps**, **9/9 CTest suites**, and `axc run hello.ax` all passed there too.

## Vulkan `@gpu` vertical slice

After the recorded regression run, the CMake build was reconfigured and all targets rebuilt successfully with the configured `-Wall -Wextra` flags. The full CTest suite was **not rerun** after this change.

```text
$ ./build/ax devices
Vulkan compute GPUs detected: 1
  [0] Intel(R) Arc(tm) Graphics (MTL) (vendor 0x8086, device 0x7d55)

$ ./build/ax check examples/gpu_scale.ax
check OK: examples/gpu_scale.ax (Vulkan GPU kernels compiled)

$ ./build/ax build examples/gpu_scale.ax -o /tmp/ax-gpu-scale.axg
AX GPU executable compiled: examples/gpu_scale.ax -> /tmp/ax-gpu-scale.axg

$ /tmp/ax-gpu-scale.axg
[1, 3, 5, 7, 9, 11, 13, 15]

$ VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/ax build examples/gpu_scale.ax -o /tmp/ax-no-gpu.axg
axc: compile-time GPU error: @gpu requires a physical Vulkan compute GPU; none was detected. CPU-only Vulkan drivers do not count. Install a GPU driver or remove @gpu.
exit status: 1
```

This verifies the current x86_64 Intel Arc + Vulkan path and the supported single-input elementwise `f32` kernel, plus rejection when only a software Vulkan driver is visible. `AX_GPU_DEVICE` selection is implemented, but there was only one physical GPU available to exercise it. No ARM64 run, CUDA/ROCm launch, or TPU execution was performed. `.axg` is a native host executable with embedded SPIR-V, not a portable custom container.

## Vulkan kernel expansion

The compiler and runtime were expanded with a two-input GPU ABI, embedded launch metadata, and these low-level shader families: a shared-memory 16×16 tiled GEMM, 128-lane row reductions for softmax/RMSNorm/layernorm, fused elementwise arithmetic and activations, and SwiGLU. The whole project build completed successfully after this expansion; the CTest suite was not rerun. The end-to-end example below ran on the detected Intel Arc GPU.

```text
$ ./build/ax check examples/gpu_ai_kernels.ax
check OK: examples/gpu_ai_kernels.ax (Vulkan GPU kernels compiled)

$ ./build/ax build examples/gpu_ai_kernels.ax -o /tmp/ax-gpu-ai.axg
AX GPU executable compiled: examples/gpu_ai_kernels.ax -> /tmp/ax-gpu-ai.axg

$ /tmp/ax-gpu-ai.axg
[1, 3, 5, 7, 9, 11, 13, 15]
[1, 2, 3, 4, 6, 7, 8, 9]
[58, 64, 139, 154]
[0.0320586, 0.0871443, 0.236883, 0.643914, 0.25, 0.25, 0.25, 0.25]
[0.365148, 0.730296, 1.09544, 1.46059, 0, 0, 0, 0]
[-1.34164, -0.447212, 0.447212, 1.34164, 0, 0, 0, 0]
[0, 0.31123, 0.731059, 1.22636, 3.52319, 4.62071, 5.71545, 6.79481]
[0, 1, 2, 3, 4, 5, 6, 7]
[0.5, 0.731059, 0.880797, 0.952574, 0.982014, 0.993307, 0.997527, 0.999089]
[0, 0.731059, 1.76159, 2.85772, 3.92806, 4.96654, 5.98516, 6.99362]
[0, 0.841192, 1.9546, 2.99636, 3.99993, 5, 6, 7]

$ ./build/ax emit-spirv examples/gpu_ai_kernels.ax -o /tmp/ax-gpu-ai.spv
SPIR-V written to /tmp/ax-gpu-ai.spv (1908 bytes, magic 0x07230203)
$ spirv-val --target-env vulkan1.1 /tmp/ax-gpu-ai.spv
(passed)

$ VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/ax build examples/gpu_ai_kernels.ax -o /tmp/ax-no-gpu.axg
axc: compile-time GPU error: @gpu requires a physical Vulkan compute GPU; none was detected. CPU-only Vulkan drivers do not count. Install a GPU driver or remove @gpu.
exit status: 1
```

These outputs were manually checked for the supplied small inputs. This is not a throughput benchmark and does not establish CUDA, ROCm, TPU, ARM64, large-tensor, or multi-GPU support. At the time of this initial run, the Vulkan runtime recreated the instance/device/pipeline on each call and used host-visible input/output buffers.

## Persistent Vulkan device and pipeline cache

The runtime now initializes a single Vulkan instance/device per process and retains compute pipelines keyed by SPIR-V and input count. Input/output buffers, descriptor sets, and command pools remain per call, and host-visible transfers are still used. Initialization failures are saved and returned consistently instead of leaving a partially initialized Vulkan context behind.

```text
$ cmake --build build --parallel 2
[100%] Built target ax_embed_c_test

$ ./build/ax build examples/gpu_ai_kernels.ax -o /tmp/ax-gpu-ai.axg
AX GPU executable compiled: examples/gpu_ai_kernels.ax -> /tmp/ax-gpu-ai.axg

$ /tmp/ax-gpu-ai.axg
[1, 3, 5, 7, 9, 11, 13, 15]
[1, 2, 3, 4, 6, 7, 8, 9]
[58, 64, 139, 154]
... remaining activation/normalization outputs as listed above ...

$ ./build/ax build /tmp/ax_gpu_cache.ax -o /tmp/ax-gpu-cache.axg
AX GPU executable compiled: /tmp/ax_gpu_cache.ax -> /tmp/ax-gpu-cache.axg
$ /tmp/ax-gpu-cache.axg
[2, 4, 6, 8]
[2, 4, 6, 8]

$ spirv-val --target-env vulkan1.1 /tmp/ax-gpu-ai.spv
(passed)
```

This rerun was on the Intel Arc device listed above. CTest was not rerun after the persistent-context change. This change reduces repeated Vulkan initialization and pipeline compilation; it does not prove cross-vendor compatibility or deliver CUDA, ROCm, TPU, vendor-specific assembly, or tuned device-memory transfers.

## Repository scans

```text
$ find . -type f | wc -l
691

$ find . -type f -empty | wc -l
0

$ find compiler tools runtime -name "*.py"
(no output)

$ grep -rnE "TODO|FIXME|stub|not implemented" compiler/ runtime/ tools/
(no output)
```

The current source-only package contains 446 files; the larger working-tree count is 807 and includes build/output/work artifacts and the ZIP. The requested 1,500–2,500+ file target is not met. The archive contains no empty files or compiled `.o`, `.a`, or `.so` artifacts.

## ARM64 and milestone status

`bash scripts/build_arm64.sh` exits with status 2 on this host and says that Linux ARM64 is required. It does not label an x86_64 build as ARM64 verification. The fresh x86_64 build and tests pass, but this is not a substitute for the requested ARM64 build.

M1–M4 reports document missing or partial features. M5 added tested `libax_embed.a` C APIs and `axc build --shared` output; most M5 features remain absent or partial. The overall result is **NOT PASSED**; there is no 110/110 verification claim. See `docs/PHASE3_MASTER_VERIFICATION_MATRIX.md` and `docs/MILESTONE_1_REPORT.md` through `docs/MILESTONE_5_REPORT.md`.
