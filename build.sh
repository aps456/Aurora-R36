#!/bin/sh
set -eu
cd "$(dirname "$0")"
if ! pkg-config --exists sdl2; then
  echo "SDL2 development files are required (pkg-config could not find sdl2)." >&2
  echo "Build against the SDL2 headers/libraries for your exact ArkOS image." >&2
  exit 2
fi
make release
./build/aurora --version
