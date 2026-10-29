#!/bin/bash
# Run several Deck benches in a row, appending results to private_work/deck/bench_results.txt:
#   tools/deck_bench_queue.sh 'label|ENV=... PROTON_NAME=...|game args' ...
cd "$(dirname "$0")/.." || exit 1
for spec in "$@"; do
  IFS='|' read -r label env args <<< "$spec"
  out=$( (eval "export $env"; tools/deck_bench.sh "$label" $args) 2>&1 | grep -v "save restored" | tail -1)
  echo "$(date +%H:%M) $out${env:+  [$env]}" | tee -a private_work/deck/bench_results.txt
done
