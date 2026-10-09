#!/bin/sh
# Native Windows build inside an MSYS2 "MINGW64" shell (https://www.msys2.org). Includes FluidSynth (MIDI music) and copies the DLLs it needs next to the exe.
#   pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-sdl3 mingw-w64-x86_64-fluidsynth
set -e
cd "$(dirname "$0")/.."
cmake -S . -B build-win64 -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-win64 --target slipstream
OUT=dist/slipstream-windows-x64
rm -rf "$OUT"
mkdir -p "$OUT"
cp build-win64/bin/slipstream.exe "$OUT/"
# every non-system DLL the exe (and, recursively, those DLLs) needs
copy_deps() {
  ldd "$1" | awk '/=> \/mingw64/ {print $3}' | while read -r dll; do
    [ -f "$OUT/$(basename "$dll")" ] || { cp "$dll" "$OUT/"; copy_deps "$dll"; }
  done
}
copy_deps "$OUT/slipstream.exe"
cp resources/GeneralUser-GS.sf2 "$OUT/"
cp README.md "$OUT/"
cp -r docs "$OUT/docs"
printf '@echo off\r\n"%%~dp0slipstream.exe" --soundfont "%%~dp0GeneralUser-GS.sf2" %%*\r\n' > "$OUT/slipstream.bat"
echo "built $OUT"
