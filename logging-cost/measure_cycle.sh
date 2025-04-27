#!/bin/bash

source ./shared.sh

run_measure_cycles() {
  prep
  start_server &
  sleep 2

  measure_cycles &
  sleep 1

  $CLIENT >/dev/null 2>&1

  stop_server
  stop_perf
}

echo "Running measure_cycles."
run_measure_cycles
echo "Finished measure_cycles."
