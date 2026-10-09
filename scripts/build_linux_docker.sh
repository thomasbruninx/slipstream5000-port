#!/bin/sh
# Builds the Linux x86-64 release inside an Ubuntu 24.04 container (works from macOS / Windows / any Linux with Docker): dist/slipstream-linux-x86_64.tar.gz
# On Apple Silicon the amd64 image runs under emulation, so expect a slow first build (about 10 minutes). Use PLATFORM=linux/arm64 for a native arm64 Linux build.
set -e
cd "$(dirname "$0")/.."
PLATFORM=${PLATFORM:-linux/amd64}
docker run --rm --platform "$PLATFORM" -v "$PWD":/src -w /src ubuntu:24.04 sh -c '
  set -e
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config ca-certificates curl \
    libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
    libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libudev-dev libdbus-1-dev libfluidsynth-dev >/dev/null
  rm -rf build-linux
  ./scripts/build_linux.sh
'
