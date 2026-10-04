#!/bin/sh
# MyBoard acquisition startup: no camera, video processing or rkipc.

# Preserve any separately installed OEM services.
for service in /oem/usr/etc/init.d/S??*; do
    [ -f "$service" ] || continue
    case "$service" in
        *.sh) (trap - INT QUIT TSTP; set start; . "$service") ;;
        *) "$service" start ;;
    esac
done

ko_dir=/oem/usr/ko
[ -f "$ko_dir/insmod_ko.sh" ] || ko_dir=/ko
if [ -f "$ko_dir/insmod_ko.sh" ]; then
    sh "$ko_dir/insmod_ko.sh"
fi
