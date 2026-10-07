#!/bin/sh
# Builds Slipstream.app (arm64) into dist/. The app contains NO original game data: it looks for the
# user's installation (SLIPSTREAM_DATA, --data, ~/Downloads/slip5000, remembered folder).
set -e
cd "$(dirname "$0")/.."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
APP=dist/Slipstream.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp build/bin/slipstream "$APP/Contents/MacOS/Slipstream"
SDL=$(otool -L build/bin/slipstream | awk '/libSDL3/ {print $1; exit}')
SDLREAL=$(python3 -c "import os,sys;print(os.path.realpath(sys.argv[1]))" "$SDL")
cp "$SDLREAL" "$APP/Contents/Frameworks/libSDL3.0.dylib"
install_name_tool -id @rpath/libSDL3.0.dylib "$APP/Contents/Frameworks/libSDL3.0.dylib"
install_name_tool -change "$SDL" @rpath/libSDL3.0.dylib "$APP/Contents/MacOS/Slipstream"
install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/Slipstream"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
 <key>CFBundleName</key><string>Slipstream</string>
 <key>CFBundleDisplayName</key><string>Slipstream (data viewer)</string>
 <key>CFBundleIdentifier</key><string>org.slipstream.reimpl</string>
 <key>CFBundleExecutable</key><string>Slipstream</string>
 <key>CFBundlePackageType</key><string>APPL</string>
 <key>CFBundleVersion</key><string>0.1</string>
 <key>CFBundleShortVersionString</key><string>0.1</string>
 <key>LSMinimumSystemVersion</key><string>12.0</string>
 <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --deep -s - "$APP" 2>&1 | tail -1 || true
echo "built $APP"
