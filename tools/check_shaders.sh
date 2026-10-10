#!/bin/sh
# Validates every built-in GLSL shader of the OpenGL renderer with the reference compiler (glslangValidator: brew install glslang / apt install glslang-tools).
# Strict drivers (NVIDIA, Windows) reject things lenient ones (Apple) accept, so run this after every shader change.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
python3 - "$T" <<'PY'
import re, sys
out = sys.argv[1]
s = open('src/renderer/gl_shaders.hpp').read()
hdr = re.search(r'kPostHeader = R"\((.*?)\)";', s, re.S).group(1)
for m in re.finditer(r'inline constexpr const char\* (k\w+) = R"\((.*?)\)";', s, re.S):
    n, b = m.group(1), m.group(2)
    if n == 'kPostHeader': continue
    if n.startswith('kPost'): b = hdr + b
    ext = 'vert' if n in ('kSceneVert', 'kFullscreenVert', 'kOverlayVert', 'kShadowVert') else 'frag'
    open(f'{out}/{n}.{ext}', 'w').write(b)
PY
fail=0
for f in "$T"/*; do glslangValidator "$f" >"$T/log" 2>&1 || { echo "FAILED $(basename "$f")"; cat "$T/log"; fail=1; }; done
rm -rf "$T"
[ $fail = 0 ] && echo "all shaders valid"
exit $fail
