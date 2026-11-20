# Install AX

AX release packages target Linux x86_64 and ARM64. Each package includes the `ax` and `axc` compilers, `axedit`, formatting/linting/debugging tools, static runtime/compiler archives, public headers, AX library sources, examples, and local documentation.

## User-local install

Download and run the installer:

```bash
curl -fsSL https://raw.githubusercontent.com/ansh-nodala/ax/main/scripts/install.sh -o /tmp/ax-install.sh
bash /tmp/ax-install.sh
```

It installs under `~/.local` by default, verifies the release archive against `SHA256SUMS`, checks its shared-library requirements, and falls back to a source build if the archive is incompatible with the host. It adds `~/.local/bin` to common Bash, Zsh, or Fish startup configuration. Open a new terminal after installation. To use a custom prefix:

```bash
AX_INSTALL_PREFIX="$HOME/tools/ax" bash /tmp/ax-install.sh
```

If there is no release archive for the current architecture, the installer downloads the project source and builds it with CMake and a C++20 compiler. Set `AX_JOBS=4` to change the source-build parallelism. Set `AX_VERSION=v2.0.0` to install from a specific release tag.

## Debian or Ubuntu package

Download the appropriate `.deb` asset from [GitHub Releases](https://github.com/ansh-nodala/ax/releases) and install it with the system package manager:

```bash
sudo apt install ./ax-linux-x86_64.deb
```

Use `ax-linux-aarch64.deb` on ARM64. Debian records the shared-library dependencies for the packaged compiler.

## Compile and edit from any directory

```bash
ax build /path/to/program.ax -o /tmp/program
/tmp/program
ax edit /path/to/program.ax
```

`ax` compiles for the machine's Linux host architecture. `axedit` is beside the compiler, so `ax edit` can launch it after either package installation or a source build.

## GPU requirements

The compiler works without a physical GPU for ordinary CPU programs. An `@gpu` program additionally requires a physical Vulkan compute device visible at build and run time, a Vulkan loader/driver, and `glslc` or `glslangValidator` when compiling the source. The Debian package declares the Vulkan loader dependency when built with Vulkan support. CUDA, ROCm, and TPU execution are not included in the current package; see the [GPU guide](GPU_GUIDE.md).

## Build and install from a checkout

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
sudo cmake --install build
```

To create a Debian package from the checkout, configure with `-DCMAKE_INSTALL_PREFIX=/usr`, build, then run `cpack --config build/CPackConfig.cmake -G DEB`.
