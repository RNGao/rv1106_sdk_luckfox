# RV1106 MyBoard BLE/WiFi 与 PREEMPT 记录

记录日期：2026-10-05。板子为 Luckfox Pico Zero MyBoard，芯片为 AIC8800DC，运行内核为 Linux 5.10.160 PREEMPT。

## 这次改了什么

此前板上运行 PREEMPT 内核，但 OEM 分区中的无线模块仍是非 PREEMPT 版本，启动日志出现 `version magic ... should be ... preempt ...`，WiFi 和 BLE 均未起来。本次针对当前内核重新编译并更新了模块；未改动 AIC8800DC 驱动的 C 源码，也未改动音频驱动。

临时加载新模块时，AIC8800DC 的 SDIO 探测仍曾报 `-110`。排查发现错误发生在 SDIO 核心设置块大小时。将 WiFi 使用的 `ffaa0000.mmc` 主机解绑再绑定，使无线卡重新上电枚举后，WiFi 固件初始化成功。因此 MyBoard 的启动脚本增加了一次失败后的 SDIO 重新枚举和驱动重试。蓝牙启动脚本改为等待 `wlan0` 出现，再挂接 UART HCI 控制器。OEM 打包脚本增加 PREEMPT 模块版本检查，防止以后只编译内核时重新混入旧模块。

## 源码和构建产物在哪里

以下路径均相对 SDK 仓库根目录：

| 内容 | 仓库路径 | 说明 |
| --- | --- | --- |
| AIC8800DC 驱动源码 | `sysdrv/drv_ko/wifi/aic8800dc/` | `aic8800_bsp/` 是芯片、SDIO 和固件初始化；`aic8800_fdrv/` 是 WiFi 驱动；`aic8800_btlpm/` 是蓝牙低功耗辅助模块。 |
| 通用无线与蓝牙内核源码 | `sysdrv/source/kernel/net/wireless/`、`sysdrv/source/kernel/net/bluetooth/`、`sysdrv/source/kernel/drivers/bluetooth/` | `cfg80211` 编译为模块；蓝牙协议栈和 UART HCI 支持编进内核。 |
| 内核配置 | `sysdrv/source/kernel/arch/arm/configs/ads1299-myboard.config`、`sysdrv/source/objs_kernel/.config` | 当前配置含 `CONFIG_PREEMPT=y`、`CONFIG_BT=y`、`CONFIG_BT_HCIUART=y`、`CONFIG_CFG80211=m`、`CONFIG_RFKILL=y`。 |
| 板级设备树 | `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-zero.dts` | 定义 SDIO 电源时序、WiFi 所在 MMC 主机和蓝牙 UART；MyBoard DTS 在 `rv1106g-luckfox-pico-zero-myboard.dts`。 |
| 新编的模块 | `output/out/sysdrv_out/kernel_drv_ko/*.ko` | OEM 打包从这里取模块；包括 `cfg80211.ko`、`libarc4.ko`、`ctr.ko`、`ccm.ko`、`aes_generic.ko` 和三个 `aic8800_*.ko`。 |
| MyBoard WiFi 加载脚本 | `project/cfg/BoardConfig_IPC/myboard-startup/insmod_wifi.sh` | 加载依赖和 AIC 模块；首次探测失败时只重置 WiFi SDIO 主机并重试一次。 |
| MyBoard OEM 打包脚本 | `project/cfg/BoardConfig_IPC/luckfox-myboard-oem-pre.sh` | 安装板级脚本和 `btmgmt`，拒绝打包非 PREEMPT 无线模块。 |
| BLE 启动脚本 | `project/cfg/BoardConfig_IPC/overlay/overlay-myboard-ble/etc/init.d/S99hciinit`、`S99zble` | 前者初始化 HCI；后者目前不在开机时广播，广播由手动启动的 GATT 测试程序管理。 |

`aic8800_bsp.ko`、`aic8800_fdrv.ko` 和 `aic8800_btlpm.ko` 是独立驱动模块。蓝牙协议栈本身和 UART HCI 支持编进内核；蓝牙控制器通过用户态 `hciattach` 挂到 `/dev/ttyS1`。BLE 广播由 `/oem/usr/bin/btmgmt` 设置。

## 板上的文件和启动顺序

| 板上路径 | 作用 |
| --- | --- |
| `/oem/usr/ko/*.ko` | WiFi/BLE 的 AIC 模块及其无线、加密依赖模块。 |
| `/oem/usr/ko/insmod_ko.sh` | 从 OEM 启动链调用板级 WiFi 加载脚本。 |
| `/oem/usr/ko/insmod_wifi.sh` | 加载模块，必要时重置 `ffaa0000.mmc` 并重试，然后启动 `rkwifi_server`。 |
| `/etc/init.d/S99hciinit` | 等待 `wlan0`，运行 `hciattach -s 1500000 /dev/ttyS1 any 1500000 flow nosleep`，再将 `hci0` 置为 UP。 |
| `/etc/init.d/S99zble` | 保留的启动入口，目前 `start` 不广播。 |
| `/ble_test/ble_test` | 手动启动的 GATT 测试程序，负责广播、Write、Notify 和断连重播。 |
| `/oem/usr/bin/btmgmt` | BlueZ 的蓝牙管理工具，供测试程序管理广播。 |

开机时 `/etc/init.d/S21appinit` 调用 `/oem/usr/bin/RkLunch.sh`，后者调用 `/oem/usr/ko/insmod_ko.sh`，再异步运行 `/oem/usr/ko/insmod_wifi.sh`。随后 `S99hciinit` 等待 WiFi 驱动就绪并创建 `hci0`。`S99zble` 不再自动广播；运行 `/ble_test/ble_test` 才会开始广播。

### WiFi/BLE 模块怎样加载

`insmod_ko.sh` 在 `/oem/usr/ko/` 中异步执行 `insmod_wifi.sh`。后者先查 `/proc/modules`，已加载的模块会跳过；尚未加载的模块按下表顺序用 `insmod` 从 `/oem/usr/ko/` 加载：

| 顺序 | 模块 | 作用 |
| --- | --- | --- |
| 1 | `cfg80211.ko` | Linux WiFi 配置与管理接口。 |
| 2 | `libarc4.ko` | 无线驱动需要的加密依赖。 |
| 3 | `ctr.ko` | CTR 加密模式依赖。 |
| 4 | `ccm.ko` | CCM 加密模式依赖。 |
| 5 | `aes_generic.ko` | AES 加密实现依赖。 |
| 6 | `aic8800_bsp.ko` | AIC8800DC 共用底层、SDIO 和固件初始化。 |
| 7 | `aic8800_fdrv.ko` | AIC8800DC WiFi 驱动，成功后创建 `wlan0`。 |
| 8 | `aic8800_btlpm.ko` | AIC8800DC 蓝牙低功耗辅助模块。 |

如果第 7 步首次加载失败，脚本只对 WiFi SDIO 主机 `ffaa0000.mmc` 执行一次 `unbind`/`bind`，等待重新枚举后再加载 `aic8800_fdrv.ko`；第二次仍失败就退出。加载完成后最多等待约 10 秒确认 `wlan0`，再启动用户态 `rkwifi_server`。蓝牙 HCI 初始化不由 `.ko` 自动完成，而是后面的 `S99hciinit` 通过 `hciattach` 挂接 `/dev/ttyS1`；广播由手动启动的 `/ble_test/ble_test` 使用 `btmgmt` 管理。`mac80211.ko` 虽在构建产物中，但这块板的 MyBoard 启动脚本没有加载它。

`insmod_ko.sh` 还保留了一个仅在文件存在时才加载 `snd-soc-rv1106.ko` 的分支；当前 codec 驱动编进内核，所以板上没有依赖这一步。蓝牙协议栈和 UART HCI 也是内建配置，不存在对应的开机 `insmod` 步骤。

## 重新构建与检查

更改 PREEMPT 内核配置后，必须重编驱动模块；只运行 `./build.sh kernel` 不会更新 OEM 中的 `.ko`。常规构建顺序：

```sh
./build.sh driver
./build.sh firmware
```

本次已生成 `output/image/boot.img`、`oem.img`、`rootfs.img` 和 `update.img`。当前板子已通过 ADB 更新所需模块及脚本，并重启验证，**不需要再烧录 `update.img`**。该完整镜像包含 `userdata.img`，整包烧录可能覆盖板上现有用户数据。

板上可用以下命令核对：

```sh
uname -a
cat /proc/modules | grep -E 'cfg80211|aic8800'
ls /sys/class/net/wlan0 /sys/class/bluetooth/hci0
iw dev wlan0 scan
hciconfig hci0
/oem/usr/bin/btmgmt info
/ble_test/ble_test --self-test-restart
dmesg | grep -E 'version magic|aicbsp|mmc1'
```

此前重启后确认：无模块版本不匹配日志；`wlan0` 能扫描到 AP；`hci0` 为 `UP RUNNING`。之后增加了手动启动的 BLE GATT 测试程序，源码和测试方法见 `tools/ble-test/README.md`。Write、Notify 和断连重播已通过板上的本地 ATT 自测，手动广播已验证；手机端实际连接和 WiFi 入网尚未验证。测试程序没有实现 nRF 工程的业务数据协议。

板载 RV1106 codec 使用内建 `CONFIG_SND_SOC_RV1106=y`，不依赖这批无线 `.ko`。已用双声道 16 kHz PCM 成功录音和回放，因此这次没有改音频驱动；该硬件接口的单声道参数在测试中被拒绝。

## 板上备份

更新前的八个无线模块、`insmod_wifi.sh` 和 `S99hciinit` 已备份到 `/userdata/myboard-preempt-backup-20261005/`。其中旧无线模块是非 PREEMPT 版本，**不能单独复制回当前 PREEMPT 内核使用**；只有配套恢复旧内核时才可使用。当前板上的文件是通过 ADB 更新的，仓库生成的镜像也包含新模块和脚本。
