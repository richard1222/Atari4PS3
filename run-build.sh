#!/bin/bash
# Detached build runner: survives SSH drops (the caller launches it
# with nohup + & and returns immediately). Usage:
#   run-build.sh <jobname> <make targets...>
# Log: build-logs/<jobname>.log ; completion: build-logs/<jobname>.done
# (contains EXIT:<code>). Logs live on C: so they survive WSL restarts.
JOB="$1"
shift
cd "$(dirname "$0")" || exit 99
mkdir -p build-logs
export PATH=/opt/ps3sdk/bin:/opt/ps3sdk/ppu/bin:/opt/ps3sdk/spu/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PSL1GHT=/opt/ps3sdk PS3DEV=/opt/ps3sdk
make "$@" > "build-logs/$JOB.log" 2>&1
echo "EXIT:$?" > "build-logs/$JOB.done"
