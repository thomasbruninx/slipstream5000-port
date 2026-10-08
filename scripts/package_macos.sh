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
# SDL3, FluidSynth and everything they link against (libsndfile, portaudio, ...) go into Contents/Frameworks
python3 scripts/bundle_dylibs.py "$APP/Contents/MacOS/Slipstream" "$APP/Contents/Frameworks"
# the default SoundFont for the MIDI music (override with --soundfont)
cp resources/GeneralUser-GS.sf2 "$APP/Contents/Resources/GeneralUser-GS.sf2"
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
 <key>NSLocalNetworkUsageDescription</key><string>Slipstream finds and joins multiplayer races on your local network.</string>
</dict></plist>
PLIST
codesign --force --deep -s - "$APP" 2>&1 | tail -1 || true
echo "built $APP"
