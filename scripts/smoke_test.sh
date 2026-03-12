#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

PYTHON_BIN="${PYTHON_BIN:-python3}"
MOCK_PORT="${MOCK_PORT:-18080}"
MOCK_USER="${MOCK_USER:-admin}"
MOCK_PASS="${MOCK_PASS:-admin}"

echo "[smoke] starting mock redfish server on port ${MOCK_PORT}"
${PYTHON_BIN} mock/redfish/server.py --listen 127.0.0.1 --port "${MOCK_PORT}" --user "${MOCK_USER}" --password "${MOCK_PASS}" >/dev/null 2>&1 &
MOCK_PID=$!
trap 'kill ${MOCK_PID} >/dev/null 2>&1 || true' EXIT

sleep 0.2

echo "[smoke] build"
./scripts/install_toolchain.sh >/dev/null 2>&1
./scripts/build.sh >/dev/null

echo "[smoke] single target"
./build/bmcli --host "127.0.0.1:${MOCK_PORT}" --user "${MOCK_USER}" --password "${MOCK_PASS}" --protocol redfish power status -o json >/dev/null

echo "[smoke] multiple targets + cmd file"
./build/bmcli --targets examples/targets.txt --cmd-file examples/commands.txt -o json >/dev/null

echo "[smoke] repeat"
./build/bmcli --targets examples/targets.txt --cmd "power status" --every 100ms --repeat 3 -o table >/dev/null

echo "ok: smoke tests passed"
