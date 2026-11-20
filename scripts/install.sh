#!/usr/bin/env bash
set -euo pipefail

repo=${AX_REPOSITORY:-ansh-nodala/ax}
version=${AX_VERSION:-latest}
prefix=${AX_INSTALL_PREFIX:-"$HOME/.local"}
machine=$(uname -m)

case "$(uname -s)" in
  Linux) ;;
  *) echo "AX binary packages currently support Linux x86_64 and ARM64." >&2; exit 2 ;;
esac
case "$machine" in
  x86_64|amd64) package_arch=x86_64 ;;
  aarch64|arm64) package_arch=aarch64 ;;
  *) echo "No AX binary package is published for $machine; supported architectures are x86_64 and ARM64." >&2; exit 2 ;;
esac

for tool in curl tar sha256sum; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "AX installer needs '$tool'. Install it and retry." >&2
    exit 2
  fi
done

tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT
asset="ax-linux-${package_arch}.tar.gz"
if [[ "$version" == "latest" ]]; then
  release_url="https://github.com/${repo}/releases/latest/download"
  source_url="https://github.com/${repo}/archive/refs/heads/main.tar.gz"
else
  release_url="https://github.com/${repo}/releases/download/${version}"
  source_url="https://github.com/${repo}/archive/refs/tags/${version}.tar.gz"
fi

installed_prebuilt=false
if curl -fsSL "${release_url}/${asset}" -o "${tmp_dir}/${asset}"; then
  if ! curl -fsSL "${release_url}/SHA256SUMS" -o "${tmp_dir}/SHA256SUMS"; then
    echo "Downloaded package has no checksum manifest; refusing to install it." >&2
    exit 1
  fi
  awk -v wanted="$asset" '$2 == wanted { print }' "${tmp_dir}/SHA256SUMS" > "${tmp_dir}/SHA256SUMS.selected"
  if [[ ! -s "${tmp_dir}/SHA256SUMS.selected" ]]; then
    echo "Checksum manifest does not include ${asset}; refusing to install it." >&2
    exit 1
  fi
  (cd "$tmp_dir" && sha256sum --check SHA256SUMS.selected)
  mkdir -p "${tmp_dir}/unpacked" "$prefix"
  tar -xzf "${tmp_dir}/${asset}" -C "${tmp_dir}/unpacked"
  if ldd "${tmp_dir}/unpacked/bin/ax" 2>&1 | grep -Eq 'not found|version .* not found'; then
    echo "This release binary needs system libraries that are missing here; building from source instead."
  else
    cp -a "${tmp_dir}/unpacked/." "$prefix/"
    installed_prebuilt=true
  fi
fi

if [[ "$installed_prebuilt" != true ]]; then
  echo "No matching release archive found; building AX from source for ${machine}."
  if ! command -v cmake >/dev/null 2>&1 || ! command -v c++ >/dev/null 2>&1; then
    echo "Source installation needs CMake 3.22+ and a C++20 compiler." >&2
    exit 2
  fi
  curl -fsSL "$source_url" -o "${tmp_dir}/source.tar.gz"
  mkdir -p "${tmp_dir}/source"
  tar -xzf "${tmp_dir}/source.tar.gz" --strip-components=1 -C "${tmp_dir}/source"
  cmake -S "${tmp_dir}/source" -B "${tmp_dir}/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
  cmake --build "${tmp_dir}/build" --parallel "${AX_JOBS:-2}"
  cmake --install "${tmp_dir}/build"
fi

if [[ "$prefix" == "$HOME/.local" ]]; then
  shell_name=${SHELL##*/}
  case "$shell_name" in
    fish)
      fish_config="$HOME/.config/fish/conf.d/ax-path.fish"
      mkdir -p "$(dirname "$fish_config")"
      if [[ ! -f "$fish_config" ]]; then
        printf 'fish_add_path --path "$HOME/.local/bin"\n' > "$fish_config"
      fi
      ;;
    zsh)
      shell_config="$HOME/.zshrc"
      if ! grep -Fq '# AX language tools' "$shell_config" 2>/dev/null; then
        printf '\n# AX language tools\ncase ":$PATH:" in *:"$HOME/.local/bin":*) ;; *) export PATH="$HOME/.local/bin:$PATH" ;; esac\n' >> "$shell_config"
      fi
      ;;
    *)
      for shell_config in "$HOME/.profile" "$HOME/.bashrc"; do
        if ! grep -Fq '# AX language tools' "$shell_config" 2>/dev/null; then
          printf '\n# AX language tools\ncase ":$PATH:" in *:"$HOME/.local/bin":*) ;; *) export PATH="$HOME/.local/bin:$PATH" ;; esac\n' >> "$shell_config"
        fi
      done
      ;;
  esac
fi

echo "AX installed under ${prefix}."
echo "Open a new terminal, or run: export PATH=\"${prefix}/bin:\$PATH\""
echo "Then use: ax build file.ax -o program && ./program"
echo "Editor: ax edit file.ax"
echo "@gpu additionally needs a Vulkan compute driver and glslc or glslangValidator."
