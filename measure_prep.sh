#!/bin/bash

echo 0 | sudo tee /proc/sys/kernel/perf_event_paranoid

echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost
