#!/bin/bash
set -e

board_dir=$(cd -- "$(dirname -- "$0")" && pwd)
: "${RK_PROJECT_PACKAGE_OEM_DIR:?OEM staging directory is required}"

# BlueZ builds this tool, but the standard rootfs does not install it.
sdk_root=${SDK_ROOT_DIR:-$(cd "$board_dir/../../.." && pwd)}
btmgmt_source=
for candidate in "$sdk_root"/sysdrv/source/buildroot/buildroot-*/output/build/bluez5_utils-*/tools/btmgmt; do
    if [ -f "$candidate" ]; then
        btmgmt_source=$candidate
        break
    fi
done
if [ -z "$btmgmt_source" ]; then
    echo "MyBoard BLE requires the Buildroot BlueZ tools build; run ./build.sh rootfs first." >&2
    exit 1
fi

# Keep the standard Buildroot packaging cleanup, then apply MyBoard startup.
bash "$board_dir/luckfox-buildroot-oem-pre.sh"
install -D -m 0755 "$board_dir/myboard-startup/RkLunch.sh" \
    "$RK_PROJECT_PACKAGE_OEM_DIR/usr/bin/RkLunch.sh"
install -D -m 0755 "$board_dir/myboard-startup/insmod_ko.sh" \
    "$RK_PROJECT_PACKAGE_OEM_DIR/usr/ko/insmod_ko.sh"
install -D -m 0755 "$btmgmt_source" \
    "$RK_PROJECT_PACKAGE_OEM_DIR/usr/bin/btmgmt"
