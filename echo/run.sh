#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# FILEPATH = $1

# if [ -z "$FILEPATH" ]; then
#   echo "Usage: $0 <file_path>"
#   exit 1
# fi

# FILEPATH=$DIR/logs/$FILEPATH

make -j$(nproc)
# Prewarm (use open loop to warm 5000 packets)
./build/warmup > /dev/null 2>&1

# Run the echo test
# ./build/echo_client_closed
