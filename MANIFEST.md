# AX v2 Source Manifest

- Source/documentation files included in `ax-lang-v3-ultra.zip`: 446 after adding the interactive project guide.
- The working directory contained 807 files at the latest audit, including build/output/work artifacts and the ZIP; these are excluded from the source archive.
- Language/toolchain: C++20, C11, AArch64 assembly, AX, CMake, shell, JSON, TOML, Markdown.
- Python files under `compiler/`, `runtime/`, and `tools/`: 0.
- Empty files in the repository at audit: 0.
- Required unfinished-marker scan (`TODO|FIXME|stub|not implemented`) under `compiler/`, `runtime/`, and `tools/`: 0 matches.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel 2`.
- Tests: `ctest --test-dir build --output-on-failure`.
- ARM64 verification: run `bash scripts/build_arm64.sh` on a native Linux ARM64 host. It rejects other architectures.
- Verification status: M1–M5 and the full 110-feature request are **not fully passed**; see `docs/PHASE3_MASTER_VERIFICATION_MATRIX.md` and milestone reports.
