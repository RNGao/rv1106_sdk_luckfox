#!/usr/bin/env bash
set -euo pipefail

usage() {
	printf 'Usage: %s [--updateimg]\n' "${0##*/}" >&2
	exit 2
}

if (( $# > 1 )); then
	usage
fi
if (( $# == 1 )) && [[ $1 != --updateimg ]]; then
	usage
fi

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
kernel="$root/sysdrv/source/kernel"
objects="$root/sysdrv/source/objs_kernel"
uboot="$root/sysdrv/source/uboot/u-boot"
dtb=rv1106g-luckfox-pico-zero-myboard.dtb
toolchain="$root/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-"

for required in "$objects/.config" "$objects/arch/arm/boot/zImage" \
	"$kernel/boot.its" "${toolchain}gcc" "$uboot/tools/mkimage"; do
	if [[ ! -f $required ]]; then
		printf 'Missing required file: %s\nBuild the kernel once before using this script.\n' "$required" >&2
		exit 1
	fi
done

make -C "$kernel" O="$objects" ARCH=arm CROSS_COMPILE="$toolchain" "$dtb"

(
	cd "$objects"
	srctree="$kernel" objtree="$objects" ARCH=arm \
		BOOT_ITS="$kernel/boot.its" MKIMAGE="$uboot/tools/mkimage" \
		"$kernel/scripts/mkimg" --dtb "$dtb"
)

cp -- "$objects/boot.img" "$root/output/image/boot.img"
printf 'Created %s\n' "$root/output/image/boot.img"

if (( $# == 1 )); then
	(cd "$root" && ./build.sh updateimg)
	printf 'Created %s\n' "$root/output/image/update.img"
fi
