# bmcli

`bmcli` is a cross-platform (Linux/Windows) BMC CLI execution framework.

Status: MVP skeleton. It implements the universal execution model (targets + commands + repeat/concurrency) but does not talk to real Redfish/IPMI yet.

## Build

Linux (no root required, installs user-space toolchain):

```bash
./scripts/install_toolchain.sh
./scripts/build.sh
./build/bmcli --help
```

Windows:

Option A (no admin required, installs user-space toolchain):

```powershell
powershell -ExecutionPolicy Bypass -File .\\scripts\\install_toolchain.ps1
powershell -ExecutionPolicy Bypass -File .\\scripts\\build.ps1
build\\bmcli.exe --help
```

Option B (Developer Command Prompt / existing toolchain):

```bat
cmake -S . -B build
cmake --build build --config Release
build\\Release\\bmcli.exe --help
```

## Quick Start

Single target, single command:

```bash
./build/bmcli --host 10.0.0.12 --user admin --password xxx power status -o json
```

Multiple targets, multiple commands:

```bash
./build/bmcli --targets targets.txt --concurrency 20 \
  --cmd "power status" \
  --cmd "health summary" \
  -o table
```

Repeat execution:

```bash
./build/bmcli --targets targets.txt --cmd "power status" --every 10s --repeat 3 -o json
```

Targets file format (`targets.txt`): one target per line: `host[,user[,pass[,protocol]]]`. `protocol` is `redfish|ipmi|auto`.
