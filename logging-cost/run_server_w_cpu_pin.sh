#!/bin/bash

CORE_LIST="0,1,2,3"

sudo taskset -c "$CORE_LIST" /home/ybyan/cvm-net-perf/hires-logger/build/echo_server_hires > /tmp/server.log 2>&1

