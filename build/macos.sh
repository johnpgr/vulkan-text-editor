#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$ROOT_DIR/bin"
MODE="${1:-debug}"
CXX="${CXX:-clang++}"

case "$MODE" in
  debug)
    MODE_FLAGS=(-g -O0)
    ;;
  release)
    MODE_FLAGS=(-O2 -DNDEBUG)
    ;;
  *)
    printf 'usage: %s [debug|release]\n' "$0" >&2
    exit 1
    ;;
esac

mkdir -p "$BIN_DIR"

COMMON_FLAGS=(
  -std=c++17
  -Wall
  -Wextra
  -Werror
  -Wno-unused-function
  -I"$ROOT_DIR/src"
)

read -r -a GLFW_CFLAGS <<<"$(pkg-config --cflags glfw3)"
read -r -a GLFW_LIBS <<<"$(pkg-config --libs glfw3)"

VULKAN_INCLUDE_DIR="$(pkg-config --variable=includedir vulkan)"
VULKAN_LIB_DIR="$(pkg-config --variable=libdir vulkan)"

if [[ ! -f "$VULKAN_INCLUDE_DIR/vulkan/vulkan.h" ]]; then
  for CANDIDATE in /usr/local/include /opt/homebrew/include; do
    if [[ -f "$CANDIDATE/vulkan/vulkan.h" ]]; then
      VULKAN_INCLUDE_DIR="$CANDIDATE"
      break
    fi
  done
fi

if [[ ! -f "$VULKAN_LIB_DIR/libvulkan.dylib" ]]; then
  for CANDIDATE in /usr/local/lib /opt/homebrew/lib; do
    if [[ -f "$CANDIDATE/libvulkan.dylib" ]]; then
      VULKAN_LIB_DIR="$CANDIDATE"
      break
    fi
  done
fi

VULKAN_CFLAGS=(-I"$VULKAN_INCLUDE_DIR")
VULKAN_LIBS=(-L"$VULKAN_LIB_DIR" -Wl,-rpath,"$VULKAN_LIB_DIR" -lvulkan)

"$CXX" \
  "${COMMON_FLAGS[@]}" \
  "${MODE_FLAGS[@]}" \
  "${GLFW_CFLAGS[@]}" \
  "${VULKAN_CFLAGS[@]}" \
  "$ROOT_DIR/src/main.cpp" \
  "${GLFW_LIBS[@]}" \
  "${VULKAN_LIBS[@]}" \
  -o "$BIN_DIR/main"

"$CXX" \
  "${COMMON_FLAGS[@]}" \
  "${MODE_FLAGS[@]}" \
  -dynamiclib \
  "$ROOT_DIR/src/game.cpp" \
  -o "$BIN_DIR/libgame.dylib"

printf 'built %s/main\n' "$BIN_DIR"
printf 'built %s/libgame.dylib\n' "$BIN_DIR"
