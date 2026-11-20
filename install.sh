#!/bin/bash
set -euo pipefail

# Builds Hasky Shelf from this folder and installs it for the current user
# only, under ~/.local. Nothing is written outside the home folder except
# the build packages, which the package manager asks for itself.

source_dir="$(cd "$(dirname "$0")" && pwd)"
build_dir="${XDG_CACHE_HOME:-$HOME/.cache}/hasky-shelf"

if (( EUID == 0 )); then
  echo "Run this as yourself, not as root: the app installs into your own home folder." >&2
  exit 1
fi

if ! command -v cmake >/dev/null || ! command -v qmake6 >/dev/null; then
  echo "Installing what the build needs..."
  if command -v omarchy-pkg-add >/dev/null; then
    omarchy-pkg-add base-devel cmake qt6-base qt6-declarative qt6-svg qt6-wayland
  else
    sudo pacman -S --needed base-devel cmake qt6-base qt6-declarative qt6-svg qt6-wayland
  fi
fi

echo "Building Hasky Shelf..."
cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build_dir" --target haskyshelf -j"$(nproc)"
cmake --install "$build_dir" --prefix "$HOME/.local" >/dev/null
gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" &>/dev/null || true

echo
echo "Hasky Shelf is installed. Find it in the app launcher."
