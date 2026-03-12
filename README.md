# bmcli

`bmcli` is a cross-platform (Linux/Windows) BMC CLI execution framework.

Status: MVP skeleton. It implements the universal execution model (targets + commands + repeat/concurrency) but does not talk to real Redfish/IPMI yet.

## Build

Linux:

```bash
cmake -S . -B build
cmake --build build
./build/bmcli --help
```

Windows (Developer Command Prompt):

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
