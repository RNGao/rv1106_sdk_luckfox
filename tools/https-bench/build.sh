#!/bin/sh
set -eu
bench_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk_dir=$(CDPATH= cd -- "$bench_dir/../.." && pwd)
compiler="$sdk_dir/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc"
bench_out=${1:-"$sdk_dir/output/https-bench/https_bench"}
mkdir -p "$(dirname -- "$bench_out")"
"$compiler" -O2 -Wall -Wextra -Werror "$bench_dir/https_bench.c" -o "$bench_out" -lssl -lcrypto
printf 'Built: %s\n' "$bench_out"
