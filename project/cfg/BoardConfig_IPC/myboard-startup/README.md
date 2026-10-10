# MyBoard startup

BLE/WiFi 驱动、板上路径、启动顺序和 PREEMPT 构建记录见
[BLE_WIFI_PREEMPT.md](BLE_WIFI_PREEMPT.md)。

The MyBoard OEM packaging hook installs the acquisition startup scripts and
the Buildroot-built `btmgmt` tool. Camera/video modules and `rkipc` are not
started. Wi-Fi/Bluetooth initialization and the existing audio path remain.
The MyBoard Wi-Fi loader uses the AIC8800DC modules. If the first SDIO probe
fails, it resets only the Wi-Fi SDIO host and retries once. `S99hciinit` waits
for `wlan0` before attaching the Bluetooth UART controller.

`S99hciinit` brings up `hci0` at boot. `S99zble` no longer starts advertising;
the BLE GATT test program owns advertising when run manually:

```sh
/ble_test/ble_test --self-test-restart
/ble_test/ble_test
```

Source and test instructions are in `tools/ble-test/README.md`. The test
program accepts Write, sends Notify, logs connections and disconnects, and
restarts advertising after a disconnect. It is not yet the nRF application's
data protocol.

Run `./build.sh driver` after changing the kernel PREEMPT setting, then run
`./build.sh firmware`. The OEM packaging hook rejects Wi-Fi/Bluetooth modules
whose version magic does not match PREEMPT. Both `rootfs.img` (init and
configuration) and `oem.img` (modules and btmgmt) are required, or use the
resulting full `update.img`. Existing boot images must include Bluetooth and
DEBUG_FS.
