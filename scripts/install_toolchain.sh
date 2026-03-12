#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p tools

echo "[bmcli] Creating venv (.venv)"
python3 -m venv .venv

echo "[bmcli] Bootstrapping pip"
curl -L -o /tmp/get-pip.py https://bootstrap.pypa.io/get-pip.py
.venv/bin/python /tmp/get-pip.py >/dev/null

echo "[bmcli] Installing cmake + ninja (user-space)"
.venv/bin/pip install --no-cache-dir cmake ninja ziglang

ZIG_BIN="$(.venv/bin/python -c 'import ziglang, pathlib; print(pathlib.Path(ziglang.__file__).resolve().parent / "zig")')"

if [[ ! -x "${ZIG_BIN}" ]]; then
  echo "[bmcli] error: zig not found inside ziglang package at ${ZIG_BIN}" >&2
  exit 2
fi

echo "[bmcli] Toolchain ready"
echo "  - cmake: $(.venv/bin/cmake --version | head -n1)"
echo "  - ninja: $(.venv/bin/ninja --version)"
echo "  - zig:   $("${ZIG_BIN}" version)"
echo ""
echo "Next:"
echo "  scripts/build.sh"
