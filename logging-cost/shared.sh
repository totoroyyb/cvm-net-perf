#!/bin/bash

GUEST_HOSTNAME="ybyan@localhost"
GUEST_PORT="2222"

# CLIENT="../echo/build/warmup"
CLIENT="../echo/build/echo_client_open"

# LOG_PREFIX="no_print_"
# LOG_PREFIX="print_"
# LOG_PREFIX="file_redirect_"
# LOG_PREFIX="log_"
# LOG_PREFIX="dummy_writer_"
# LOG_PREFIX="file_redirect_20c_2500p_"
LOG_PREFIX="no_print_20c_2500p_"

prep() {
  mkdir -p logs
}

start_server() {
  ssh $GUEST_HOSTNAME -p $GUEST_PORT "nohup /home/ybyan/cvm-net-perf/echo/build/echo_server_async > /tmp/server.log 2>&1" >/dev/null 2>&1
  # ssh $GUEST_HOSTNAME -p $GUEST_PORT "nohup /home/ybyan/cvm-net-perf/logging-cost/build/dummy_writer" >/dev/null 2>&1
}

stop_server() {
  ssh $GUEST_HOSTNAME -p $GUEST_PORT "pkill -9 echo_server_"
  ssh $GUEST_HOSTNAME -p $GUEST_PORT "pkill -9 dummy_"
}

measure_exits() {
  sudo perf record -e kvm:kvm_exit -a
  sudo perf script >logs/${LOG_PREFIX}all.log
  sudo perf script | grep 'kvm_exit' | wc -l >logs/${LOG_PREFIX}exits.log
  sudo perf script | grep 'vmgexit' | wc -l >logs/${LOG_PREFIX}vmgexit.log
  sudo perf script | grep 'hlt' | wc -l >logs/${LOG_PREFIX}hlt.log
  sudo rm perf.data
}

measure_cycles() {
  sudo perf stat -e instructions,cycles -a >logs/${LOG_PREFIX}cycles.log 2>&1
}

stop_perf() {
  sudo pkill -2 perf
  sleep 2 # ensure perf is done collecting data.
}

clean() {
  sudo rm perf.*
}
