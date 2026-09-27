#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:-results}"
compiler="${CC:-gcc}"
mkdir -p "$out"
log="$out/$(date +%m%d)_1.log"
# -a retains previous runs on the same day.
{
  printf 'Experiment timestamp: %s\n' "$(date -Iseconds)"
  uname -a
  lscpu
  printf '\nperf_event_paranoid='
  cat /proc/sys/kernel/perf_event_paranoid
  printf '\nCompiler\n'
  "$compiler" --version | head -n 1
  printf '\nBuild command: make (C11, O2, no O3)\n'
  make CC="$compiler"
  for pattern in varied shared-prefix; do
    for mode in head tail hit miss mixed; do
      printf '\nCommand: ./build/linkbench --lists 4 --nodes 4096 --size 64 --queries 1000 --repeat 3 --mode %s --pattern %s\n' "$mode" "$pattern"
      ./build/linkbench --lists 4 --nodes 4096 --size 64 --queries 1000 --repeat 3 --mode "$mode" --pattern "$pattern"
    done
  done
} 2>&1 | tee -a "$log"
printf '\nSaved: %s\n' "$log"
