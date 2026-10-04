#!/bin/sh
# Keep audio and Wi-Fi/Bluetooth support; do not load camera/video modules.
script_path=$(realpath "$0") || exit 1
cd "$(dirname "$script_path")" || exit 1

if [ -f snd-soc-rv1106.ko ]; then
    insmod snd-soc-rv1106.ko
fi

# The existing Wi-Fi loader also initializes the AIC8800 Bluetooth support.
if [ -f ./insmod_wifi.sh ]; then
    sh ./insmod_wifi.sh &
fi
