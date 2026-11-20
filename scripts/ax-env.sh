#!/usr/bin/env bash

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "Source this file in your shell: source scripts/ax-env.sh" >&2
  exit 2
fi

_ax_project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
_ax_build_dir=${AX_BUILD_DIR:-"$_ax_project_root/build"}
if [[ ! -x "$_ax_build_dir/ax" ]]; then
  if [[ -x "$_ax_build_dir/axc" && ! -e "$_ax_build_dir/ax" && ! -L "$_ax_build_dir/ax" ]]; then
    if ! ln -s axc "$_ax_build_dir/ax"; then
      echo "ax: could not create the short command link in $_ax_build_dir" >&2
      unset _ax_project_root _ax_build_dir
      return 2
    fi
  else
    echo "ax: compiler not found at $_ax_build_dir/ax or $_ax_build_dir/axc; build the project first" >&2
    unset _ax_project_root _ax_build_dir
    return 2
  fi
fi
case ":$PATH:" in
  *:"$_ax_build_dir":*) ;;
  *) export PATH="$_ax_build_dir:$PATH" ;;
esac
unset _ax_project_root _ax_build_dir
