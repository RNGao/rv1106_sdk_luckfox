# Luckfox Pico Zero MyBoard SDK 修改记录

最后核对：2026-10-15（北京时间）。范围是本仓库相对 2026-09-15 上游同步基线的 MyBoard 定制、构建工具调整，以及与板子配套的独立工具。日期优先采用 Git 提交日期；未提交的 CPU 调频改动和板端验证采用实际操作日期。以下“验证”只表示列出的检查已做，不等于所有硬件功能都经过长期测试。

## 按日期汇总

| 日期 | 变更 | 主要文件或提交 | 结果与边界 |
| --- | --- | --- | --- |
| 2026-09-15 | 同步上游 SDK 基线 | `aaecc58d9` | 后续记录以此为比较起点；不计作 MyBoard 定制。 |
| 2026-09-24 | 新增 Pico Zero MyBoard 构建配置、设备树和 ADS1299 IIO 驱动 | `8adc07538` | 使用独立板型配置和 DTS；ADS1299 接在 SPI0，驱动源码纳入 SDK。 |
| 2026-09-28 | 更新 `tools` 子模块引用，随后将 `tools/` 转成主仓库普通目录 | `d3fc94713`、`56095edb2` | 工具链和打包工具随主仓库管理；其中已有的 NumPy 辅助工具被一并纳入。此日期是**纳入主仓库日期**，不能据此推断 NumPy 工具最初编写或部署的日期。 |
| 2026-10-04 | 调整 ADS1299 采集相关内核配置、PREEMPT、板级启动与 BLE 基础流程 | `7cc20d847` | 启用 PREEMPT，增加 MyBoard OEM 打包和启动脚本，并适配单核编译。 |
| 2026-10-05 | 排查并记录 PREEMPT 内核与 WiFi/BLE 模块匹配、SDIO 重试及板端验证 | [BLE_WIFI_PREEMPT.md](../project/cfg/BoardConfig_IPC/myboard-startup/BLE_WIFI_PREEMPT.md) 中的记录日期；相关代码于 2026-10-10 提交 | 记录的板端结果包括 WiFi 扫描、蓝牙 HCI 启动及模块版本匹配；记录日期与 Git 提交日期分开保留。 |
| 2026-10-10 | 完善 WiFi/BLE 启动和 OEM 模块检查，加入 BLE GATT 与 HTTPS 测试工具 | `219686a8a` | 包含 AIC8800DC 加载脚本、失败后一次 SDIO 重新枚举、等待 `wlan0` 的 HCI 脚本、BLE 测试程序，以及打包时的 PREEMPT 模块版本检查。 |
| 2026-10-10 | 在 MyBoard DTS 中启用 I²C3 并增加 ICM20948 节点 | `03b636940` | 地址 `0x68`；中断引脚最初写为 GPIO1_D3，10 月 14 日改为 GPIO1_C6。该提交本身不能证明传感器实测通过。 |
| 2026-10-14 | 更新按键、LED 和 ICM20948 引脚；加入 Pico Zero 原理图 | `d1875e6bd`、`6dc647849` | 按键为 GPIO1_C7、GPIO0_A3、GPIO0_A4；LED 改为 GPIO0_A0；IMU 中断改为 GPIO1_C6。原理图位于 `schematic/Luckfox-Pico-Zero.pdf`。 |
| 2026-10-14 | 修复 MyBoard CPU 固定供电描述并限制 OPP | 本次提交的 `rv1106g-luckfox-pico-zero-myboard.dts` 改动 | `VDD_ARM` 按原理图中的固定 `VDD_0V9` 电源轨声明为 900000 µV；仅保留 408、600、816、1104 MHz。 |
| 2026-10-15 | 用新 DTB 重新打包镜像，烧录后验证自动调频 | `output/image/boot.img`、`output/image/update.img`；板端 ADB 检查 | `vdd-arm`、`cpufreq-dt` 已绑定，`policy0` 使用 `ondemand`；实际时钟曾读到 408 MHz。完整内核重编译未完成，打包沿用原有内核二进制。 |

## 各项修改说明

### MyBoard、ADS1299 与采集配置（2026-09-24、10-04）

- `project/cfg/BoardConfig_IPC/BoardConfig-EMMC-Buildroot-RV1106_Luckfox_Pico_Zero_MyBoard-IPC.mk` 选择 RV1106、eMMC、MyBoard DTS 和 `ads1299-myboard.config`。MyBoard 配置还指定了 OEM 钩子与启动覆盖目录。
- `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-zero-myboard.dts` 开启 SPI0，增加 `ti,ads1299` 节点和 START、PWDN、RESET、DRDY 引脚描述；后续将 SPI 上限从 1 MHz 调为 4 MHz。
- `sysdrv/source/kernel/drivers/iio/adc/ads1299_myboard.c` 是定制 IIO 驱动源码；Kconfig/Makefile 配套入口已加入。`ads1299-myboard.config` 开启所需 IIO 支持和 PREEMPT，驱动本身按工程构建方式处理，不能仅凭源码存在断定已编入运行内核。
- 10 月 4 日为单核构建给 `drivers/cpufreq/rockchip-cpufreq.c` 的 SMP 专用代码加了 `CONFIG_SMP` 条件保护；这是 PREEMPT 相关构建改动，与 10 月 14 日的板级调频修复是两件事。

### WiFi、BLE、启动和打包（2026-10-04、10-05、10-10）

- `luckfox-myboard-oem-pre.sh` 安装 MyBoard 启动脚本、无线模块加载脚本和 `btmgmt`；当内核为 PREEMPT 时，打包前检查八个 WiFi/BLE 及依赖模块的 `vermagic`，防止把非 PREEMPT 模块放进 OEM 镜像。
- `myboard-startup/insmod_wifi.sh` 按顺序加载 AIC8800DC 与依赖模块。首次探测失败时只重置 WiFi 所用的 SDIO 主机并重试一次；`S99hciinit` 等待 `wlan0` 后再挂接蓝牙 UART HCI。
- `S99zble` 保留启动入口，但不在开机时自动广播。手动运行的 `tools/ble-test/ble_test` 提供 GATT Write/Notify 与断连后重新广播测试；`luckfox-myboard-post.sh` 在打包时把它安装到根文件系统。`tools/https-bench/` 是独立 HTTPS 测试工具。
- 2026-10-05 的实机排查、模块顺序、板上备份和验证命令详见 [BLE/WiFi 与 PREEMPT 记录](../project/cfg/BoardConfig_IPC/myboard-startup/BLE_WIFI_PREEMPT.md)；BLE 工具用法详见 [BLE 测试说明](../tools/ble-test/README.md)。

### 其他板级外设（2026-10-10、10-14）

- I²C3 的 ICM20948 节点地址为 `0x68`，总线配置为 400 kHz。10 月 14 日将其中断和 pinctrl 都改为 GPIO1_C6。
- 10 月 14 日的 `gpio-keys` 增加三个低电平有效按键；工作 LED 由 GPIO1_C6 迁到 GPIO0_A0。引脚以当前 MyBoard DTS 为准，早期提交中的引脚已被后续提交替换。
- 新增的 [Pico Zero 原理图](../schematic/Luckfox-Pico-Zero.pdf) 是本次 CPU 供电核对的依据之一。原理图表明 `VDD_ARM` 通过 R70（0 Ω）接到固定的 `VDD_0V9` 电源轨；900000 µV 是标称描述，尚未用仪器实测。

### CPU 自动调频（2026-10-14 修改；10-15 构建、烧录与验证）

修改前的 [调频排查交接](rv1106_cpu_cpufreq_handoff.md) 记录了：内核已配置 `CONFIG_CPUFREQ_DT=y` 和默认 `ondemand`，但板端没有 `policy0`；`vdd-arm` 使用 `regulator-fixed`，却将最小/最大电压分别写为 800000/1000000 µV，固定稳压器驱动因此无法正常绑定。该文档是**修改前的历史状态**，应以本节作为后续结果。

2026-10-14 在 MyBoard DTS 中覆盖 `vdd_arm` 的最小、最大电压为 900000 µV；删除 1200、1296、1416、1512、1608 MHz 的 OPP，保留 408、600、816、1104 MHz。未修改共享的 SoC OPP 表，也未提高原来 1.104 GHz 的上限。`ondemand` 已是内核默认 governor，无须另行改配置。

2026-10-15 构建的 MyBoard DTB 通过 `dtc` 编译。完整 `make` 内核重编译多次被终止；由于本次源码改动仅涉及 DTS，使用现有 `zImage` 和新 DTB 经 SDK 的 `scripts/mkimg` 重新生成 `boot.img`，再打包 `update.img`。FIT 镜像中 FDT 的 SHA-256 与新 DTB 相同。`update.img` 的 SHA-256 为 `3099714fa4d6f13f5ac1311bafede4c26217a843363d19801bc47ccfc7e7e6c6`；这些输出文件属于构建产物，不是提交到 Git 的源码。

烧录后，板端 `/sys/bus/platform/devices/vdd-arm/driver` 和 `cpufreq-dt/driver` 均存在，`policy0` 出现，`scaling_governor=ondemand`，`scaling_available_frequencies` 为 `408000 600000 816000 1104000`（kHz），`scaling_min_freq=408000`、`scaling_max_freq=1104000`。`/sys/class/regulator/.../microvolts` 报 900000，表示内核中的固定电压声明，**不是实测供电电压**。短时负载下保持 1104000 kHz；空闲采样曾读到 408000、600000 和 816000 kHz，且 408000 kHz 时 `cpuinfo_cur_freq` 也读到 408000 kHz，说明实际时钟曾降频。单次读到 1104000 kHz 不代表频率锁定；未进行长时间稳定性或供电实测。

板端复查命令：

```sh
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq
cat /sys/devices/system/cpu/cpufreq/policy0/cpuinfo_cur_freq
```

### 独立工具与板端运行环境（2026-09-28 纳入仓库）

`tools/numpy-runtime/` 提供独立的 ARMv7 musl/Python 3.12/NumPy 环境及组装脚本，安装目标为板端 `/opt/python-numpy`。它不替换 SDK 的 Buildroot/uClibc 或系统 Python，也不属于上述 `boot.img` 的内核改动。主仓库只证明它在 2026-09-28 随 `tools/` 目录被纳入；原始编写、首次安装日期无法由主仓库提交记录确认。版本、部署和验证范围见 [NumPy 环境说明](../tools/numpy-runtime/README.md)。

## 当前源码状态与维护约定

本次调频的 MyBoard DTS、修改前的排查交接和本记录一同纳入版本控制；历史改动可用上表中的短提交号追溯。修改内核配置（例如 PREEMPT）后需重编匹配的外置模块；仅改 MyBoard DTS 时，应核对新的 DTB 确实进入 `boot.img`，并在板端确认 `policy0` 和实际频率。完整 `update.img` 还包含其他分区镜像，烧录前应核对目标分区及数据保留需求。
