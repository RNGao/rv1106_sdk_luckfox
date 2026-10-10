# RV1106 BLE GATT test

Build with `bash tools/ble-test/build.sh`. The script links against the BlueZ
5.65 libraries already built by this SDK. Copy `ble_test` to `/ble_test/` on the
board and run `/ble_test/ble_test` in the foreground. The program owns one
connectable `BLETest` advertising instance; Ctrl-C removes it. It listens on
the LE ATT channel, reports connections and disconnects to the shell, and
recreates advertising after a disconnect. The board must already have `hci0`
UP; its normal `S99hciinit` still provides that step.

The test GATT service uses Nordic UART style UUIDs for easy testing, but it is
an echo test rather than the nRF application's protocol:

| Attribute | UUID | Behaviour |
| --- | --- | --- |
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | Primary service. |
| Write | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | Write or Write Without Response; logs hex and printable text. |
| Notify | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | Subscribe via CCC; receives `ready`, written data echoed back, and `tick:N` every five seconds. |

Use a BLE central to find `BLETest`, connect, enable notifications on the
Notify characteristic, and write `hello` to the Write characteristic. The
shell should show the write, notification, connection and disconnection logs.
The advertising packet contains the name and a changing test nonce; the
service UUID is discovered after connection. The test uses no pairing or
authentication and is for local development only.

`/ble_test/ble_test --self-test` exercises the GATT write and notification
callbacks through a local ATT socket pair without a phone. It does not test
the radio link or a real connection. The host VM has no Bluetooth controller,
so on-device advertising and this self-test are the available automated checks.
`/ble_test/ble_test --self-test-restart` additionally recreates advertising
after the simulated disconnect, confirms one instance exists, then removes it.

The old `/etc/init.d/S99zble` entry is intentionally inert: boot no longer
starts advertising. Start this program manually from a shell to advertise.
