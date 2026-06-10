# CMake toolchain for cross-compiling the host build to 64-bit Windows from
# Linux/WSL2 with MinGW-w64. Produces a native .exe + SDL2.dll for
# distribution (Steam-ready). See docs/PC.md.
#
#   cmake -B build_win -S host -DCMAKE_TOOLCHAIN_FILE=host/toolchain-mingw-w64.cmake
#   cmake --build build_win -j8
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
# Tools (the mesh baker) build for the host; libs/headers come from the target.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
