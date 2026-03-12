#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

if [[ ! -x ".venv/bin/cmake" ]]; then
  echo "missing .venv cmake; run: scripts/install_toolchain.sh" >&2
  exit 2
fi

if [[ ! -x "tools/zig-current/zig" ]]; then
  echo "missing Zig; run: scripts/install_toolchain.sh" >&2
  exit 2
fi

chmod +x scripts/zig-cc scripts/zig-cxx

.venv/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="${ROOT_DIR}/scripts/zig-cc" \
  -DCMAKE_CXX_COMPILER="${ROOT_DIR}/scripts/zig-cxx"

.venv/bin/cmake --build build
./build/bmcli --help >/dev/null
echo "ok: build/bmcli"

