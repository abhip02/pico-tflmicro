#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
docker build -t pico-tflmicro-dev:local "$repo_dir/.devcontainer"
docker run --rm \
  --mount "type=bind,source=$repo_dir,target=/workspaces/pico-tflmicro" \
  --workdir /workspaces/pico-tflmicro \
  pico-tflmicro-dev:local bash -euo pipefail -c '
    # Compile on the Linux filesystem to avoid slow macOS bind-mount reads.
    mkdir -p /tmp/pico-tflmicro
    tar --exclude=./build --exclude=./.git -cf - . \
      | tar -xf - -C /tmp/pico-tflmicro
    /tmp/pico-tflmicro/scripts/build-robot.sh
    mkdir -p build/firmware
    cp /tmp/pico-tflmicro/build/firmware/hello_world.uf2 build/firmware/
    cp /tmp/pico-tflmicro/build/firmware/hello_world.elf build/firmware/
  '
printf '\nReady to drag onto RPI-RP2: %s\n' "$repo_dir/build/firmware/hello_world.uf2"
