#!/usr/bin/env bash
# Explicit --run is required; default mode only validates files/arguments.
# Timeout bounds host observation, not a guarantee against a GPU driver hang.
set -euo pipefail
probe_source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ $# -ne 7 && $# -ne 8 ]]; then
  echo "usage: bash $0 cubin main.fp8 skip.fp8 weights output-prefix width height [--run]" >&2
  exit 2
fi
probe_build_dir=$(mktemp -d /tmp/native-decoder-entry.XXXXXX)
echo "Probe build directory (retained): $probe_build_dir"
g++ -std=c++17 -O2 -Wall -Wextra \
  "$probe_source_dir/run_native_decoder_entry_probe.cpp" \
  -I/usr/local/cuda/include -L/usr/local/cuda/lib64 -lcuda \
  -o "$probe_build_dir/probe"
timeout --signal=TERM --kill-after=2s 15s "$probe_build_dir/probe" "$@"
