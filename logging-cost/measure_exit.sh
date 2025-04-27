#!/bin/bash

source ./shared.sh

run_measure_exits() {
  prep
  start_server &
  sleep 2

  measure_exits &
  sleep 1

  $CLIENT >/dev/null 2>&1

  stop_server
  stop_perf
}

echo "Running measure_exits."
run_measure_exits
echo "Finished measure_exits."
