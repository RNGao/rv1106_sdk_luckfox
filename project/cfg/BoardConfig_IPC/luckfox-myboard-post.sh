#!/bin/bash
set -e

: "${SDK_ROOT_DIR:?SDK root is required}"
: "${RK_PROJECT_PACKAGE_ROOTFS_DIR:?Rootfs staging directory is required}"

bash "$SDK_ROOT_DIR/tools/ble-test/build.sh"
install -D -m 0755 "$SDK_ROOT_DIR/tools/ble-test/ble_test" \
    "$RK_PROJECT_PACKAGE_ROOTFS_DIR/ble_test/ble_test"
install -D -m 0644 "$SDK_ROOT_DIR/tools/ble-test/ble_test.c" \
    "$RK_PROJECT_PACKAGE_ROOTFS_DIR/ble_test/ble_test.c"
install -D -m 0644 "$SDK_ROOT_DIR/tools/ble-test/README.md" \
    "$RK_PROJECT_PACKAGE_ROOTFS_DIR/ble_test/README.md"
