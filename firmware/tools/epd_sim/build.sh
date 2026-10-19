#!/bin/sh
# Builds the e-paper screenshot simulator: the REAL src/epaper_techo.cpp, compiled for the PC.
set -e
cd "$(dirname "$0")"
FW=../..
JSON=$FW/.pio/libdeps/techo_s140v7/ArduinoJson/src
g++ -std=gnu++17 -O1 -w -DFAKETEC_BOARD_TECHO -DTEcho -DAPP_VERSION_STR='"1.0alpha"' -DAPP_BUILD_NUM='"b112"' \
  -Istubs -include Arduino.h -I$FW/src -I$JSON -o epd_sim sim_main.cpp sim_stubs.cpp $FW/src/sota.cpp $FW/src/config.cpp $FW/src/ax25.cpp $FW/src/battery.cpp
