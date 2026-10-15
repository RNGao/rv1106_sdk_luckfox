# RV1106 CPU 调频排查交接

## 目标与边界

用户希望确认：只修改内核、不改硬件，能否让 CPU 调频。结论是可以在现有供电能力允许的范围内调节时钟；要启用可靠的自动调频，需要先修复当前设备树中的 CPU 供电描述，并限制频点到实际供电可支持的范围。不能仅凭设备树声明推断高于当前 1.104 GHz 的频率安全。

本轮仅做了只读排查，没有修改调频相关代码或设备树，没有编译、刷机，也没有向 `/proc/clk/rate` 写入频率。

## 仓库与板端

- 内核 SDK：`/home/grn/luckfox-pico`。当前本地分支 `dev`，包含 MyBoard 设备树和原理图 `schematic/Luckfox-Pico-Zero.pdf`；本次调频排查没有产生新的内核改动。
- 板端 ADB 序列号：`f6d5349ae3693d4d`。
- 实际使用的设备树入口：`sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-zero-myboard.dts`，它包含 `rv1106g-luckfox-pico-zero.dts` 和 `rv1106-luckfox-pico-zero-ipc.dtsi`。
- `rv1106g-luckfox-pico-zero.dts` 的 `&cru` 将 `ARMCLK` 启动频率设为 `1104000000` Hz。

## 已观察到的运行状态

板端读取命令：

```sh
cat /proc/clk/summary | grep ' armclk '
ls /sys/devices/system/cpu/cpufreq/
ls -l /sys/bus/platform/devices/vdd-arm/driver
ls -l /sys/bus/platform/devices/cpufreq-dt/driver
```

`armclk` 连续三次读数都是 `1104000000` Hz。`/sys/devices/system/cpu/cpufreq/` 没有 `policy0`，`cpu0/cpufreq/` 也不存在；`vdd-arm` 和 `cpufreq-dt` 两个 platform device 均无 `driver` 链接。`/sys/class/regulator/` 只看到 `regulator-dummy`、`vcc_1v8`、`vcc_3v3`，没有 `vdd_arm`。因此当前没有运行 cpufreq governor，CPU 实际停在 1.104 GHz；不能把内核默认配置中的 `ondemand` 视为板端已经启用。

## 内核配置与疑似阻断点

- `sysdrv/source/objs_kernel/.config` 已启用 `CONFIG_CPU_FREQ=y`、`CONFIG_CPUFREQ_DT=y`、`CONFIG_ARM_ROCKCHIP_CPUFREQ=y`、`CONFIG_CPU_FREQ_GOV_ONDEMAND=y`，默认 governor 是 `ondemand`。`CONFIG_REGULATOR_FIXED_VOLTAGE=y` 和 `CONFIG_REGULATOR_PWM=y` 也已启用。
- `arch/arm/boot/dts/rv1106.dtsi` 的 CPU OPP 表列出 408、600、816、1104、1200、1296、1416、1512、1608 MHz；不同 OPP 要求的电压不同。这是 SoC 的候选频点表，不代表当前板级供电能支持全部频点。
- `arch/arm/boot/dts/rv1106-luckfox-pico-zero-ipc.dtsi` 中 `&cpu0` 的 `cpu-supply` 指向 `vdd_arm`。同文件的 `vdd_arm` 使用 `compatible = "regulator-fixed"`，却写了 `regulator-min-microvolt = <800000>`、`regulator-max-microvolt = <1000000>`、`regulator-init-microvolt = <900000>`。
- `drivers/regulator/fixed.c` 的 `of_get_fixed_voltage_config()` 明确要求固定 regulator 的最小与最大电压相等，否则报 `Fixed regulator specified with variable voltages` 并返回 `-EINVAL`。板端 `vdd-arm` 未绑定，与这个冲突吻合。
- `drivers/cpufreq/cpufreq-dt.c` 在初始化时获取 CPU regulator 和 OPP；板端 `cpufreq-dt` 未绑定，也没有 cpufreq policy。**供电节点错误是有源码和现场状态支持的主要原因推断**；启动时的具体 probe 错误日志尚未捕获。

关键文件：

```text
/home/grn/luckfox-pico/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-luckfox-pico-zero-ipc.dtsi
/home/grn/luckfox-pico/sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-zero.dts
/home/grn/luckfox-pico/sysdrv/source/kernel/arch/arm/boot/dts/rv1106.dtsi
/home/grn/luckfox-pico/sysdrv/source/kernel/drivers/regulator/fixed.c
/home/grn/luckfox-pico/sysdrv/source/kernel/drivers/cpufreq/cpufreq-dt.c
/home/grn/luckfox-pico/sysdrv/source/kernel/drivers/cpufreq/rockchip-cpufreq.c
```

## 后续处理建议

1. 根据 `schematic/Luckfox-Pico-Zero.pdf` 和实板确认 `VDD_ARM` 的实际电压、是否能通过 PWM 或其他电源器件调节。当前设备树中的 `regulator-init-microvolt = <900000>` 不是实测电压。
2. 若供电不可调，用正确的固定电压值描述 `vdd_arm`（最小值等于最大值），并将 OPP 限制到该电压下经硬件确认安全的频率。不要仅为通过驱动 probe 而把 `min`、`max` 都随意改成 900000，也不要在供电不变时直接开放高压 OPP。
3. 若供电实际可调，按真实硬件实现 regulator 描述，再核对各 OPP 电压与调频稳定性。可参考 SDK 中其他 RV1106 板型的 `pwm-regulator` 写法，但不能直接照搬到本板。
4. 修改并部署后，确认 `vdd-arm`、`cpufreq-dt` 已绑定且出现 `/sys/devices/system/cpu/cpufreq/policy0/`。再读 `scaling_available_frequencies`、`scaling_governor`、`scaling_cur_freq`，分别验证空闲和负载下的变化、温度及长期稳定性。

正常启用 cpufreq 后，可按 SDK 的 `media/security/librkcrypto/perf_reports/ReadMe_CN.md` 使用 `policy0` 接口；其频率单位是 **kHz**。例如查看：

```sh
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq
```

当前内核另有 `/proc/clk/rate`，其写入格式为 `echo armclk <Hz> > /proc/clk/rate`。这会直接改时钟，绕过 cpufreq 的 OPP 与电压协调；本轮未执行，也不应把它当作正式调频方案。
