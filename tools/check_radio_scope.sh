#!/bin/sh
# Radio scope (security baseline): Bluetooth never linked; Wi-Fi referenced only by
# src/wifi_portal.cpp. Usage: tools/check_radio_scope.sh .pio/build/saucedeck
set -eu
build="$1"
nm="${NM:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-nm}"
elf="$build/firmware.elf"
syms=$("$nm" "$elf")
echo "$syms" | grep -q -E ' T (_Z5setupv|app_main)$' || { echo "radio_scope: bad ELF" >&2; exit 2; }
if echo "$syms" | grep -E ' [TtW] (esp_bt_controller_init|esp_bluedroid_init|nimble_port_init|btStart)$'; then
  echo "radio_scope: Bluetooth linked" >&2; exit 1
fi
found=0
bad=0
for o in "$build"/src/*.o; do
  found=1
  case "$o" in */wifi_portal.cpp.o) continue ;; esac
  if "$nm" -u "$o" | grep -q -E 'esp_wifi_|WiFiClass|WiFiManager|WiFiSTAClass|WiFiGenericClass|MDNSResponder'; then
    echo "radio_scope: Wi-Fi referenced by $o" >&2; bad=1
  fi
done
[ "$found" -eq 1 ] || { echo "radio_scope: no objects in $build/src" >&2; exit 2; }
[ "$bad" -eq 0 ] || exit 1
echo "radio_scope: OK (no BT; Wi-Fi only from wifi_portal.cpp)"
