#!/bin/bash

source ./shared.sh

TYPE=$1

if [[ -z $TYPE ]]; then
  echo "Usage: $0 <TYPE>"
  echo "<TYPE>: 'cycle' | 'exit' "
fi

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

run_measure_cycles() {
  prep
  start_server&
  sleep 2

  measure_cycles &
  sleep 1

  $CLIENT >/dev/null 2>&1

  stop_server
  stop_perf
}

if [[ $TYPE == "cycle" ]]; then
  echo "Running measure_cycles."
  run_measure_cycles
  echo "Finished measure_cycles."
elif [[ $TYPE == "exit" ]]; then
  echo "Running measure_exits."
  run_measure_exits
  echo "Finished measure_exits."
else
  echo "Unknow measurement type." 
  exit 1
fi

echo "Waiting a bit for perf to wrap up..."
sleep 5

