#!/bin/sh
# Builds Slipstream for Linux (x86-64, also works on arm64) into dist/slipstream-linux-<arch>/ and a .tar.gz.
# Needs: a C++20 compiler (g++ >= 11 / clang >= 14), cmake, ninja (or make), pkg-config and SDL3 (libsdl3-dev; when it is not installed the build downloads and
# builds SDL3 itself, which needs the X11 / Wayland / ALSA / PulseAudio development packages, see docs/building.md). libfluidsynth-dev adds the MIDI music.
# The result contains NO original game data (use --data / SLIPSTREAM_DATA / ~/Games/slip5000).
set -e
cd "$(dirname "$0")/.."
ARCH=$(uname -m)
GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
cmake -S . -B build-linux $GEN -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-linux -j "$(nproc 2>/dev/null || echo 4)"
OUT=dist/slipstream-linux-$ARCH
rm -rf "$OUT"
mkdir -p "$OUT"
cp build-linux/bin/slipstream "$OUT/"
cp resources/GeneralUser-GS.sf2 "$OUT/"
cp README.md "$OUT/"
cp -r docs "$OUT/docs"
cat > "$OUT/slipstream.sh" <<'LAUNCH'
#!/bin/sh
# starts the game next to this script; set SLIPSTREAM_DATA or pass --data DIR if the original game is not found automatically
here=$(dirname "$(readlink -f "$0")")
exec "$here/slipstream" --soundfont "$here/GeneralUser-GS.sf2" "$@"
LAUNCH
chmod +x "$OUT/slipstream.sh"
if ldd "$OUT/slipstream" | grep -q "not found"; then echo "warning: missing shared libraries:"; ldd "$OUT/slipstream" | grep "not found"; fi
tar -C dist -czf "dist/slipstream-linux-$ARCH.tar.gz" "slipstream-linux-$ARCH"
echo "built $OUT and dist/slipstream-linux-$ARCH.tar.gz"
