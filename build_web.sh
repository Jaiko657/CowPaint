#!/usr/bin/env bash
# Build CowPaint for web (Emscripten). Run from repo root.
# Requires: Emscripten SDK (emcc in PATH). Raylib source for web lib + shell.
set -e

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
RAYLIB_SRC="${RAYLIB_SRC:-$REPO_ROOT/raylib/src}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
WEB_DIR="${WEB_DIR:-$REPO_ROOT/web}"

# 1) Build raylib for web if we have raylib source
if [ -f "$RAYLIB_SRC/Makefile" ]; then
  echo "Building raylib for PLATFORM_WEB..."
  (cd "$RAYLIB_SRC" && make PLATFORM=PLATFORM_WEB -B)
  RAYLIB_A="$RAYLIB_SRC/libraylib.web.a"
  RAYLIB_INCLUDE="$RAYLIB_SRC"
  SHELL_HTML="$RAYLIB_SRC/minshell.html"
else
  echo "No raylib source at RAYLIB_SRC=$RAYLIB_SRC"
  echo "Set RAYLIB_SRC to raylib/src, or clone: git clone https://github.com/raysan5/raylib.git"
  exit 1
fi

if [ ! -f "$RAYLIB_A" ]; then
  echo "Missing $RAYLIB_A"
  exit 1
fi

mkdir -p "$WEB_DIR"

# 2) Build CowPaint with emcc (ASYNCIFY so while(!WindowShouldClose()) works)
echo "Building CowPaint for web..."
emcc -o "$WEB_DIR/cowpaint.html" \
  -Os -Wall -std=c99 \
  "$REPO_ROOT/src/main.c" \
  "$RAYLIB_A" \
  -I "$RAYLIB_INCLUDE" \
  -s USE_GLFW=3 \
  -s ASYNCIFY \
  -s ASSERTIONS=0 \
  -DPLATFORM_WEB \
  --shell-file "$SHELL_HTML"

echo "Web build done: $WEB_DIR/cowpaint.html (.js, .wasm)"
echo "Serve locally: python -m http.server 8080 --directory $WEB_DIR"
echo "For GitHub Pages: use index.html (rename cowpaint.html) in the deployed folder."
cp "$WEB_DIR/cowpaint.html" "$WEB_DIR/index.html"
