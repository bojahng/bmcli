#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

if [[ ! -x ".venv/bin/cmake" ]]; then
  echo "missing .venv cmake; run: scripts/install_toolchain.sh" >&2
  exit 2
fi

export PATH="${ROOT_DIR}/.venv/bin:${PATH}"

chmod +x scripts/zig-cc scripts/zig-cxx

.venv/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_CXX_COMPILER="${ROOT_DIR}/scripts/zig-cxx" \
  -DCMAKE_MAKE_PROGRAM="${ROOT_DIR}/.venv/bin/ninja"

.venv/bin/cmake --build build
./build/bmcli --help >/dev/null
echo "ok: build/bmcli"
