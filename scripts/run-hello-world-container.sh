#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
docker build -t pico-tflmicro-dev:local "$repo_dir/.devcontainer"
docker run --rm \
  --mount "type=bind,source=$repo_dir,target=/workspaces/pico-tflmicro" \
  --workdir /workspaces/pico-tflmicro \
  pico-tflmicro-dev:local ./scripts/run-hello-world.sh
