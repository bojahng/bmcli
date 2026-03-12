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
.venv/bin/pip install --no-cache-dir cmake ninja

ZIG_VERSION="${ZIG_VERSION:-0.13.0}"
ZIG_TARBALL="zig-linux-x86_64-${ZIG_VERSION}.tar.xz"
ZIG_URL="https://ziglang.org/download/${ZIG_VERSION}/${ZIG_TARBALL}"
ZIG_DIR="tools/zig-${ZIG_VERSION}"

if [[ -x "${ZIG_DIR}/zig" ]]; then
  echo "[bmcli] Zig already installed at ${ZIG_DIR}"
else
  echo "[bmcli] Downloading Zig ${ZIG_VERSION}"
  curl -L --http1.1 --retry 5 --retry-all-errors --retry-delay 2 \
    -C - -o "tools/${ZIG_TARBALL}" "${ZIG_URL}"
  rm -rf "${ZIG_DIR}"
  mkdir -p "${ZIG_DIR}"
  tar -xf "tools/${ZIG_TARBALL}" -C tools
  # Extracted directory name matches tarball prefix.
  mv "tools/zig-linux-x86_64-${ZIG_VERSION}" "${ZIG_DIR}"
  ln -sfn "zig-${ZIG_VERSION}" tools/zig-current
fi

echo "[bmcli] Toolchain ready"
echo "  - cmake: $(.venv/bin/cmake --version | head -n1)"
echo "  - ninja: $(.venv/bin/ninja --version)"
echo "  - zig:   $(tools/zig-current/zig version)"
echo ""
echo "Next:"
echo "  scripts/build.sh"

