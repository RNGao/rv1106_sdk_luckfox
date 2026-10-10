#!/bin/sh
# MyBoard has an AIC8800DC on mmc1. Retry once after resetting only that host
# when the SDIO card stops responding during module probe.
cd /oem/usr/ko || exit 1

loaded() {
    grep -q "^$1 " /proc/modules
}

load_module() {
    loaded "$1" || insmod "./$1.ko"
}

for module in cfg80211 libarc4 ctr ccm aes_generic aic8800_bsp; do
    load_module "$module" || exit 1
done

if ! load_module aic8800_fdrv; then
    host=ffaa0000.mmc
    driver=/sys/bus/platform/drivers/dwmmc_rockchip
    [ -e "$driver/$host" ] || exit 1
    echo "[WiFi] Resetting mmc1 after AIC8800 SDIO probe failed" >&2
    echo "$host" > "$driver/unbind" || exit 1
    sleep 1
    echo "$host" > "$driver/bind" || exit 1
    sleep 2
    load_module aic8800_fdrv || exit 1
fi

load_module aic8800_btlpm || exit 1

tries=0
while [ ! -d /sys/class/net/wlan0 ]; do
    tries=$((tries + 1))
    [ "$tries" -lt 10 ] || exit 1
    sleep 1
done

rkwifi_server start >/dev/null 2>&1 &
