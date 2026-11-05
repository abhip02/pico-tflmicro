#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Separate build caches when the same checkout is opened on macOS and Linux.
build_dir="$repo_dir/build/host-$(uname -s)-$(uname -m)"

cmake -S "$repo_dir" -B "$build_dir" \
  -DTFLM_BUILD_HOST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel 4
ctest --test-dir "$build_dir" --output-on-failure
"$build_dir/hello_world"
