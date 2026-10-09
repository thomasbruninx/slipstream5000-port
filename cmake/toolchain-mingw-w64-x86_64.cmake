# Cross compile for Windows x64 with MinGW-w64 (from macOS or Linux): cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64-x86_64.cmake ...
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
# the posix-thread flavour (std::thread / std::mutex); Debian / Ubuntu call it x86_64-w64-mingw32-g++-posix, Homebrew and MSYS2 ship it as the default
find_program(SLIP_MINGW_CXX NAMES ${TOOLCHAIN_PREFIX}-g++-posix ${TOOLCHAIN_PREFIX}-g++ REQUIRED)
find_program(SLIP_MINGW_CC NAMES ${TOOLCHAIN_PREFIX}-gcc-posix ${TOOLCHAIN_PREFIX}-gcc REQUIRED)
find_program(SLIP_MINGW_RC NAMES ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_C_COMPILER ${SLIP_MINGW_CC})
set(CMAKE_CXX_COMPILER ${SLIP_MINGW_CXX})
if(SLIP_MINGW_RC)
  set(CMAKE_RC_COMPILER ${SLIP_MINGW_RC})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# one self-contained .exe: no libgcc / libstdc++ / winpthread DLLs next to it
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
