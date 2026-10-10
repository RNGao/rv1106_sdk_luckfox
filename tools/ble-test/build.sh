#!/bin/bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
bluez="$repo_root/sysdrv/source/buildroot/buildroot-2023.02.6/output/build/bluez5_utils-5.65"
cc="$repo_root/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc"

"$cc" -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
    -I"$bluez" "$repo_root/tools/ble-test/ble_test.c" \
    "$bluez/src/.libs/libshared-mainloop.a" \
    "$bluez/lib/.libs/libbluetooth-internal.a" \
    -o "$repo_root/tools/ble-test/ble_test"
