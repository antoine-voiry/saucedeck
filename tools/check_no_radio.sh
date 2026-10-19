#!/bin/sh
# Fail if the firmware links Wi-Fi or Bluetooth init code (security baseline: radios unused).
# Usage: tools/check_no_radio.sh .pio/build/saucedeck/firmware.elf
set -eu
elf="$1"
nm="${NM:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-nm}"
syms=$("$nm" "$elf")
# Sanity: the scan must see our own code, else it proves nothing
echo "$syms" | grep -q -E ' T (_Z5setupv|app_main)$' || { echo "check_no_radio: no setup()/app_main symbol, bad ELF?" >&2; exit 2; }
if echo "$syms" | grep -E ' [TtW] (esp_wifi_init|esp_wifi_start|esp_bt_controller_init|esp_bluedroid_init|nimble_port_init|btStart)$'; then
  echo "check_no_radio: radio init code linked" >&2
  exit 1
fi
echo "check_no_radio: OK (no Wi-Fi/BT init symbols)"
