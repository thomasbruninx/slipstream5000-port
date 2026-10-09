#!/bin/sh
# Cross compiles Slipstream for Windows x64 from macOS or Linux with MinGW-w64 into dist/slipstream-windows-x64/ (+ .zip).
#   macOS:   brew install mingw-w64 cmake ninja
#   Debian:  sudo apt install g++-mingw-w64-x86-64-posix cmake ninja-build        (then: sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix)
#   Fedora:  sudo dnf install mingw64-gcc-c++ cmake ninja-build
# SDL3 is downloaded and built statically by CMake (needs network on the first run); the exe is fully static. FluidSynth (MIDI music) is not part of this build
# (no MinGW build of it is available for cross compiling): the game runs with effects but without MIDI music. Use build_windows_msys2.sh on Windows for the music.
set -e
cd "$(dirname "$0")/.."
GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
cmake -S . -B build-win64 $GEN -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64-x86_64.cmake -DSLIP_FETCH_SDL3=ON "$@"
cmake --build build-win64 --target slipstream -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
OUT=dist/slipstream-windows-x64
rm -rf "$OUT"
mkdir -p "$OUT"
cp build-win64/bin/slipstream.exe "$OUT/"
cp resources/GeneralUser-GS.sf2 "$OUT/"
cp README.md "$OUT/"
cp -r docs "$OUT/docs"
cat > "$OUT/slipstream.bat" <<'BAT'
@echo off
rem starts the game; add --data "C:\path\to\Slipstream5000" if the original game is not found automatically
"%~dp0slipstream.exe" --soundfont "%~dp0GeneralUser-GS.sf2" %*
BAT
sed -i.bak 's/$/\r/' "$OUT/slipstream.bat" && rm -f "$OUT/slipstream.bat.bak"
(cd dist && rm -f slipstream-windows-x64.zip && zip -qr slipstream-windows-x64.zip slipstream-windows-x64)
echo "built $OUT and dist/slipstream-windows-x64.zip"
