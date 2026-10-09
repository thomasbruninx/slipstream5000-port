#!/bin/sh
# Cross compiles Slipstream for Windows x64 from macOS or Linux with MinGW-w64 into dist/slipstream-windows-x64/ (+ .zip), WITH MIDI music:
# the MSYS2 project's prebuilt MinGW packages (fluidsynth, SDL3 and their DLLs, the same ones `pacman -S mingw-w64-x86_64-fluidsynth` installs on Windows) are
# downloaded into build-win64-sysroot/ by scripts/fetch_msys2_deps.py and used for compiling and for the DLLs next to the exe.
#   macOS:   brew install mingw-w64 cmake ninja zstd
#   Debian:  sudo apt install g++-mingw-w64-x86-64-posix cmake ninja-build zstd pkg-config zip   (then: sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix)
#   Fedora:  sudo dnf install mingw64-gcc-c++ cmake ninja-build zstd pkgconf zip
# NOMUSIC=1 skips the download: SDL3 is then built from source and linked statically, the single .exe has no MIDI music (needs no DLLs at all).
set -e
cd "$(dirname "$0")/.."
GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
SYS=$PWD/build-win64-sysroot
rm -rf build-win64
if [ -z "$NOMUSIC" ]; then
  python3 scripts/fetch_msys2_deps.py "$SYS" fluidsynth sdl3
  export PKG_CONFIG_LIBDIR="$SYS/mingw64/lib/pkgconfig:$SYS/mingw64/share/pkgconfig"
  export PKG_CONFIG_SYSROOT_DIR="$SYS"
  EXTRA="-DCMAKE_FIND_ROOT_PATH=$SYS/mingw64 -DCMAKE_PREFIX_PATH=$SYS/mingw64 -DSLIP_PKGCONFIG_CROSS=ON"
else
  EXTRA="-DSLIP_FETCH_SDL3=ON"
fi
cmake -S . -B build-win64 $GEN -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64-x86_64.cmake $EXTRA "$@"
cmake --build build-win64 --target slipstream -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
OUT=dist/slipstream-windows-x64
rm -rf "$OUT"
mkdir -p "$OUT"
cp build-win64/bin/slipstream.exe "$OUT/"
if [ -z "$NOMUSIC" ]; then
  # the DLLs the exe needs, followed recursively (objdump lists the imports); Windows' own DLLs are not in the sysroot and are skipped
  OBJDUMP=$(command -v x86_64-w64-mingw32-objdump)
  copy_deps() {
    "$OBJDUMP" -p "$1" | awk '/DLL Name:/ {print $3}' | while read -r dll; do
      if [ -f "$SYS/mingw64/bin/$dll" ] && [ ! -f "$OUT/$dll" ]; then cp "$SYS/mingw64/bin/$dll" "$OUT/"; copy_deps "$SYS/mingw64/bin/$dll"; fi
    done
  }
  copy_deps "$OUT/slipstream.exe"
fi
cp resources/GeneralUser-GS.sf2 "$OUT/"
cp README.md "$OUT/"
cp -r docs "$OUT/docs"
printf '@echo off\r\nrem starts the game; add --data "C:\\path\\to\\Slipstream5000" if the original game is not found automatically\r\n"%%~dp0slipstream.exe" --soundfont "%%~dp0GeneralUser-GS.sf2" %%*\r\n' > "$OUT/slipstream.bat"
(cd dist && rm -f slipstream-windows-x64.zip && zip -qr slipstream-windows-x64.zip slipstream-windows-x64)
echo "built $OUT and dist/slipstream-windows-x64.zip"
