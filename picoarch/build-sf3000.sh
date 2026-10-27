#!/bin/sh
set -eu
: "${CROSS_COMPILE:?Set CROSS_COMPILE to the SDK compiler prefix}"
: "${SF3000_SYSROOT:?Set SF3000_SYSROOT to the SDK sysroot}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build-sf3000"}
cmake -S "$root" -B "$build" \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/mips32-sf3000.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DHCGBA_PICOARCH=ON -DHCGBA_LTO=OFF
cmake --build "$build" --parallel "${JOBS:-2}"
"${CROSS_COMPILE}readelf" -h -A -d "$build/hcgba_libretro.so"
