#!/bin/bash

source ./shared.sh

run_measure_exits() {
  prep
  measure_exits &
  sleep 1

  start_server

  stop_server
  stop_perf
}

run_measure_cycles() {
  prep
  measure_cycles &
  sleep 1

  start_server

  stop_server
  stop_perf
}

echo "Running measure_exits."
run_measure_exits
echo "Finished measure_exits."

sleep 3
echo "Running measure_cycles."
run_measure_cycles
echo "Finished measure_cycles."
