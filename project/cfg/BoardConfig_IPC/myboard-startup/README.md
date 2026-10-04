# MyBoard startup

The MyBoard OEM packaging hook installs the acquisition startup scripts and
the Buildroot-built `btmgmt` tool. Camera/video modules and `rkipc` are not
started. Wi-Fi/Bluetooth initialization and the existing audio path remain.

The board-specific `overlay-myboard-ble` installs `/etc/init.d/S99zble`, which
runs after `S99hciinit`. It selects the Pico Zero onboard antenna (GPIO123,
GPIO3_D3 = 1), waits up to 30 seconds for the Bluetooth controller, and starts
one connectable, indefinitely advertising `Luckfox-BLE` instance. The name is
in the advertising packet rather than depending on a scan response.

```sh
/etc/init.d/S99zble status
/etc/init.d/S99zble stop
/etc/init.d/S99zble start
/etc/init.d/S99zble restart
```

Settings are in `/etc/default/luckfox-ble`. The default 100–150 ms interval is
applied through kernel debugfs because this SDK does not apply the interval
requested by `bluetoothctl`. These GPIO mappings are for the original Pico
Zero, not arbitrary RV1106 boards.

This script owns advertising on hci0: start/stop clears existing advertising
instances, so do not also run a bluetoothctl advertiser. Bluetooth remains
powered when advertising is stopped. A fresh four-byte development nonce
in manufacturer data forces the old kernel to resend the payload on restart;
the total payload remains within the legacy 31-byte limit.

This is boot-time advertising, not a GATT data service or a connection
supervisor. ADS1299 data transfer and reconnect behavior require a GATT
application. The script does not pair with phones or change authentication.

Run `./build.sh firmware` to include the changes. Both `rootfs.img` (init and
configuration) and `oem.img` (btmgmt) are required, or use the resulting full
`update.img`. Existing boot images must include Bluetooth and DEBUG_FS.
