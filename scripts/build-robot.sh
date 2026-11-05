#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_dir/build/robot-$(uname -s)-$(uname -m)"

cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
  -DTFLM_BUILD_HOST=OFF -DPICO_BOARD=pololu_3pi_2040_robot \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --target hello_world --parallel 4
mkdir -p "$repo_dir/build/firmware"
cp "$build_dir/examples/hello_world/hello_world.uf2" \
  "$repo_dir/build/firmware/hello_world.uf2"
cp "$build_dir/examples/hello_world/hello_world.elf" \
  "$repo_dir/build/firmware/hello_world.elf"
arm-none-eabi-size "$build_dir/examples/hello_world/hello_world.elf"
printf '\nReady to copy to RPI-RP2: %s\n' \
  "$repo_dir/build/firmware/hello_world.uf2"
