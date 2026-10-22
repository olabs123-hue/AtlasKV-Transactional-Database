#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
echo "Date (UTC): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
uname -a
g++ --version | head -1
echo "Workload: Current repository's in-memory single-key transaction commit benchmark; no durable disk commit."
for i in 1 2 3 4 5; do
  echo "========== RUN $i / 5 =========="
  cmake -S . -B "$tmp/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$tmp/build" -j2
  "$tmp/build/atlaskv_bench"
done
