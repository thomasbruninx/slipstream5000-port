# Building Slipstream (macOS, Linux x64, Windows x64)

The build never contains original game data: the game reads the files of **your own** copy (GOG install) at run time. Point it at the folder that holds `SLIPSTRM.RES` /
`SLIPCD.RES` with `--data DIR`, the `SLIPSTREAM_DATA` environment variable, or put it in one of the folders that are searched automatically (current folder, `~/Downloads/slip5000`,
`~/Games/slip5000`, `~/slip5000`, `~/GOG Games/Slipstream 5000`, on Windows also `C:\GOG Games\Slipstream 5000`). The folder that worked is remembered.

Requirements everywhere: a C++20 compiler (GCC >= 11, Clang >= 14 or MinGW-w64 GCC; MSVC is not supported), CMake >= 3.20, and SDL3. SDL3 is taken from the system when installed;
otherwise CMake downloads the SDL3 release (`SLIP_SDL3_VERSION`, default 3.4.16) and builds it statically (`-DSLIP_FETCH_SDL3=ON` forces that). FluidSynth gives the MIDI music; without it
the game builds and runs with sound effects only.

| Target | Script | Result | MIDI music |
|---|---|---|---|
| macOS (Apple Silicon) | `scripts/package_macos.sh` | `dist/Slipstream.app` | yes |
| Linux x64, native | `scripts/build_linux.sh` | `dist/slipstream-linux-<arch>.tar.gz` | yes (if libfluidsynth-dev is installed) |
| Linux x64, from macOS / Windows / anything with Docker | `scripts/build_linux_docker.sh` | `dist/slipstream-linux-x86_64.tar.gz` | yes |
| Windows x64, cross compiled from macOS or Linux | `scripts/build_windows_cross.sh` | `dist/slipstream-windows-x64.zip` (one static .exe) | **no** |
| Windows x64, native (MSYS2) | `scripts/build_windows_msys2.sh` | `dist/slipstream-windows-x64/` with the DLLs | yes |

## macOS
```sh
brew install cmake ninja sdl3 fluid-synth
./scripts/package_macos.sh
```

## Linux x64 (native)
```sh
# Debian / Ubuntu (SDL3 is only packaged from Ubuntu 25.04 on; otherwise it is built from source, which needs the -dev packages below)
sudo apt install build-essential cmake ninja-build pkg-config libfluidsynth-dev \
  libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libudev-dev libdbus-1-dev
./scripts/build_linux.sh
dist/slipstream-linux-x86_64/slipstream.sh --data ~/Games/slip5000
```
Fedora: `sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config fluidsynth-devel SDL3-devel` (or the X11 / Wayland / ALSA / Pulse `-devel` packages when SDL3 is built from source).
At run time the machine needs `libfluidsynth3` and the usual audio / X11 / Wayland libraries (SDL itself is linked statically in a fetched build).
The extra arguments of the script go to CMake (`./scripts/build_linux.sh -DSLIP_FETCH_SDL3=ON`).

### Linux from another machine (Docker)
```sh
./scripts/build_linux_docker.sh                    # Ubuntu 24.04, linux/amd64 (emulated on Apple Silicon: slow, ~15 minutes the first time)
PLATFORM=linux/arm64 ./scripts/build_linux_docker.sh   # native speed on Apple Silicon, produces an arm64 Linux build
```

## Windows x64
### Cross compiling from macOS or Linux (MinGW-w64)
```sh
brew install mingw-w64 cmake ninja                 # macOS
sudo apt install g++-mingw-w64-x86-64-posix cmake ninja-build   # Debian / Ubuntu  (select the posix flavour: update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix)
./scripts/build_windows_cross.sh
```
`cmake/toolchain-mingw-w64-x86_64.cmake` does the work; SDL3 is downloaded and built statically (needs network the first time) and the exe links libgcc / libstdc++ statically,
so `slipstream.exe` needs no DLLs. FluidSynth is **not** included in the cross build (there is no MinGW package of it that a cross compiler can use), so the Windows
cross build has sound effects but no MIDI music; build natively with MSYS2 for the music. Extra CMake options can be appended to the script.

### Native (MSYS2)
Install [MSYS2](https://www.msys2.org), open the "MSYS2 MINGW64" shell:
```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-sdl3 mingw-w64-x86_64-fluidsynth zip
./scripts/build_windows_msys2.sh
```
The script copies the DLLs the exe needs next to it. Run `slipstream.bat` (it passes the bundled SoundFont) or `slipstream.exe --data "C:\path\to\Slipstream5000"`.

## Files the port writes
| OS | Folder |
|---|---|
| macOS | `~/Library/Application Support/Slipstream` |
| Windows | `%APPDATA%\Slipstream` |
| Linux | `$XDG_CONFIG_HOME/slipstream` (default `~/.config/slipstream`) |

It holds `config.txt` (options, key and controller bindings), `records.txt` (best laps), `slot1.sav`.. (championship saves), `data_dir.txt` (remembered game folder) and the optional
`gamecontrollerdb.txt` (extra SDL gamepad mappings).

## Status of the ports (honest summary)
* macOS arm64: built and played every session.
* Linux x86-64: builds in the Docker container (Ubuntu 24.04, GCC 13), all unit tests pass there (under emulation), the binary starts. Not run on a desktop with a window or sound device.
* Windows x64: builds and links with MinGW-w64 (GCC 14). **Never run** (no Windows machine / Wine here): the Winsock port of the multiplayer code (`src/net/socket.cpp`, `WSAPoll`,
  adapter enumeration with `GetAdaptersInfo`) and the controller code in particular need a real test. Please report problems.
* The game is a console-subsystem program on Windows (a console window shows the log lines); no icon / version resources yet.
