// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal BlueZ ATT server for the RV1106 AIC8800DC board.
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "lib/bluetooth.h"
#include "lib/hci.h"
#include "lib/hci_lib.h"
#include "lib/l2cap.h"
#include "lib/uuid.h"
#include "src/shared/att.h"
#include "src/shared/queue.h"
#include "src/shared/gatt-db.h"
#include "src/shared/gatt-server.h"
#include "src/shared/mainloop.h"
#include "src/shared/timeout.h"
#include "src/shared/util.h"

#define BTMGMT "/oem/usr/bin/btmgmt"
#define NAME "BLETest"
#define SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define WRITE_UUID   "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NOTIFY_UUID  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define ATT_CID 4

struct app;

struct connection {
    struct app *app;
    struct bt_att *att;
    struct gatt_db *db;
    struct bt_gatt_server *gatt;
    uint16_t write_handle;
    uint16_t notify_handle;
    uint16_t ccc_handle;
    bool subscribed;
};

struct app {
    int listen_fd;
    struct connection *conn;
    bool advertising;
    bool self_test;
    bool self_test_restart;
    bool restart_passed;
    unsigned int tick;
    unsigned int adv_seq;
};

static int run_btmgmt(char *const args[])
{
    pid_t pid = fork();
    int status;

    if (pid < 0)
        return -1;
    if (pid == 0) {
        execv(BTMGMT, args);
        perror("exec btmgmt");
        _exit(127);
    }
    if (waitpid(pid, &status, 0) != pid)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int advertising_instances(void)
{
    FILE *stream = popen(BTMGMT " advinfo", "r");
    char line[160];
    int count = -1;

    if (!stream)
        return -1;
    while (fgets(line, sizeof(line), stream)) {
        if (sscanf(line, "Instances list with %d items", &count) == 1)
            break;
    }
    pclose(stream);
    return count;
}

static void stop_advertising(struct app *app)
{
    char *const args[] = { BTMGMT, "clr-adv", NULL };

    if (!app->advertising)
        return;
    if (advertising_instances() > 0 && run_btmgmt(args))
        fprintf(stderr, "[BLE] Could not clear advertising instance\n");
    app->advertising = false;
    puts("[BLE] Advertising stopped");
}

static bool start_advertising(struct app *app)
{
    char payload[64];
    unsigned int nonce = (unsigned int) time(NULL) ^ ++app->adv_seq;
    char *const clear[] = { BTMGMT, "clr-adv", NULL };
    char *const add[] = { BTMGMT, "add-adv", "-c", "-g", "-t", "0",
                          "-d", payload, "1", NULL };

    // Name plus a changing development nonce. The nonce works around this
    // SDK's stale advertising cache after an instance is recreated.
    snprintf(payload, sizeof(payload), "0809424c455465737407ffffff%08x", nonce);
    if (advertising_instances() > 0 && run_btmgmt(clear)) {
        fprintf(stderr, "[BLE] Could not clear previous advertising instance\n");
        return false;
    }
    if (run_btmgmt(add)) {
        fprintf(stderr, "[BLE] Could not start advertising\n");
        return false;
    }
    app->advertising = true;
    printf("[BLE] Advertising %s, connectable, instance 1\n", NAME);
    return true;
}

static void print_write(const uint8_t *value, size_t len)
{
    size_t i;

    printf("[GATT] WRITE %zu bytes hex=", len);
    for (i = 0; i < len; i++)
        printf("%02x", value[i]);
    printf(" text=\"");
    for (i = 0; i < len; i++)
        putchar(value[i] >= 32 && value[i] < 127 ? value[i] : '.');
    puts("\"");
}

static bool send_notify(struct connection *conn, const uint8_t *value,
                        size_t len)
{
    if (!conn->subscribed) {
        puts("[GATT] Notify skipped: client has not enabled CCC");
        return false;
    }
    if (!bt_gatt_server_send_notification(conn->gatt, conn->notify_handle,
                                          value, len, false)) {
        fprintf(stderr, "[GATT] Notify failed\n");
        return false;
    }
    printf("[GATT] NOTIFY %zu bytes\n", len);
    return true;
}

static void name_read(struct gatt_db_attribute *attr, unsigned int id,
                      uint16_t offset, uint8_t opcode, struct bt_att *att,
                      void *user_data)
{
    size_t len = strlen(NAME);

    if (offset > len) {
        gatt_db_attribute_read_result(attr, id,
                                      BT_ATT_ERROR_INVALID_OFFSET, NULL, 0);
        return;
    }
    gatt_db_attribute_read_result(attr, id, 0,
                                  (const uint8_t *) NAME + offset,
                                  len - offset);
}

static void write_value(struct gatt_db_attribute *attr, unsigned int id,
                        uint16_t offset, const uint8_t *value, size_t len,
                        uint8_t opcode, struct bt_att *att, void *user_data)
{
    struct connection *conn = user_data;
    uint8_t error = 0;

    if (offset)
        error = BT_ATT_ERROR_INVALID_OFFSET;
    else if (len > 244)
        error = BT_ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LEN;
    else if (len && !value)
        error = BT_ATT_ERROR_UNLIKELY;

    gatt_db_attribute_write_result(attr, id, error);
    if (error)
        return;

    print_write(value, len);
    send_notify(conn, value, len);
}

static void ccc_read(struct gatt_db_attribute *attr, unsigned int id,
                     uint16_t offset, uint8_t opcode, struct bt_att *att,
                     void *user_data)
{
    struct connection *conn = user_data;
    uint8_t value[] = { conn->subscribed ? 1 : 0, 0 };

    if (offset > sizeof(value)) {
        gatt_db_attribute_read_result(attr, id,
                                      BT_ATT_ERROR_INVALID_OFFSET, NULL, 0);
        return;
    }
    gatt_db_attribute_read_result(attr, id, 0, value + offset,
                                  sizeof(value) - offset);
}

static void ccc_write(struct gatt_db_attribute *attr, unsigned int id,
                      uint16_t offset, const uint8_t *value, size_t len,
                      uint8_t opcode, struct bt_att *att, void *user_data)
{
    struct connection *conn = user_data;
    uint8_t error = 0;

    if (offset)
        error = BT_ATT_ERROR_INVALID_OFFSET;
    else if (!value || len != 2)
        error = BT_ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LEN;
    else if (value[1] || value[0] > 1)
        error = BT_ATT_ERROR_VALUE_NOT_ALLOWED;
    else
        conn->subscribed = value[0] == 1;

    gatt_db_attribute_write_result(attr, id, error);
    if (error)
        return;
    printf("[GATT] Notify %s\n", conn->subscribed ? "enabled" : "disabled");
    if (conn->subscribed)
        send_notify(conn, (const uint8_t *) "ready", 5);
}

static bool populate_db(struct connection *conn)
{
    bt_uuid_t uuid;
    struct gatt_db_attribute *service, *attr;

    bt_uuid16_create(&uuid, 0x1800);
    service = gatt_db_add_service(conn->db, &uuid, true, 3);
    if (!service)
        return false;
    bt_uuid16_create(&uuid, 0x2a00);
    if (!gatt_db_service_add_characteristic(service, &uuid, BT_ATT_PERM_READ,
                                            BT_GATT_CHRC_PROP_READ,
                                            name_read, NULL, conn))
        return false;
    gatt_db_service_set_active(service, true);

    if (bt_string_to_uuid(&uuid, SERVICE_UUID))
        return false;
    service = gatt_db_add_service(conn->db, &uuid, true, 6);
    if (!service)
        return false;

    if (bt_string_to_uuid(&uuid, WRITE_UUID))
        return false;
    attr = gatt_db_service_add_characteristic(service, &uuid, BT_ATT_PERM_WRITE,
                            BT_GATT_CHRC_PROP_WRITE |
                            BT_GATT_CHRC_PROP_WRITE_WITHOUT_RESP,
                            NULL, write_value, conn);
    if (!attr)
        return false;
    conn->write_handle = gatt_db_attribute_get_handle(attr);

    if (bt_string_to_uuid(&uuid, NOTIFY_UUID))
        return false;
    attr = gatt_db_service_add_characteristic(service, &uuid, BT_ATT_PERM_NONE,
                                               BT_GATT_CHRC_PROP_NOTIFY,
                                               NULL, NULL, conn);
    if (!attr)
        return false;
    conn->notify_handle = gatt_db_attribute_get_handle(attr);

    bt_uuid16_create(&uuid, GATT_CLIENT_CHARAC_CFG_UUID);
    attr = gatt_db_service_add_descriptor(service, &uuid,
                            BT_ATT_PERM_READ | BT_ATT_PERM_WRITE,
                            ccc_read, ccc_write, conn);
    if (!attr)
        return false;
    conn->ccc_handle = gatt_db_attribute_get_handle(attr);
    gatt_db_service_set_active(service, true);

    printf("[GATT] Service %s; WRITE handle=0x%04x, NOTIFY handle=0x%04x, CCC handle=0x%04x\n",
           SERVICE_UUID, conn->write_handle, conn->notify_handle,
           conn->ccc_handle);
    return true;
}

static void connection_destroy(struct connection *conn)
{
    if (!conn)
        return;
    bt_gatt_server_unref(conn->gatt);
    gatt_db_unref(conn->db);
    bt_att_unref(conn->att);
    free(conn);
}

static bool disconnected_later(void *user_data)
{
    struct app *app = user_data;

    connection_destroy(app->conn);
    app->conn = NULL;
    if (app->self_test) {
        if (app->self_test_restart) {
            app->restart_passed = start_advertising(app) &&
                                  advertising_instances() == 1;
            if (app->restart_passed)
                puts("[SELFTEST] Advertising restarted after disconnect");
            stop_advertising(app);
        }
        mainloop_quit();
    } else {
        start_advertising(app);
    }
    return false;
}

static void att_disconnected(int err, void *user_data)
{
    struct connection *conn = user_data;

    printf("[BLE] Disconnected: %s\n", strerror(err));
    timeout_add(1, disconnected_later, conn->app, NULL);
}

static struct connection *connection_new(struct app *app, int fd)
{
    struct connection *conn = calloc(1, sizeof(*conn));

    if (!conn)
        return NULL;
    conn->app = app;
    conn->att = bt_att_new(fd, false);
    conn->db = gatt_db_new();
    if (!conn->att || !conn->db ||
        !bt_att_set_close_on_unref(conn->att, true))
        goto fail;
    conn->gatt = bt_gatt_server_new(conn->db, conn->att, 247, 0);
    if (!conn->gatt || !populate_db(conn) ||
        !bt_att_register_disconnect(conn->att, att_disconnected, conn, NULL))
        goto fail;
    return conn;

fail:
    connection_destroy(conn);
    return NULL;
}

static int listen_att(const bdaddr_t *src)
{
    struct sockaddr_l2 addr = { 0 };
    struct bt_security sec = { .level = BT_SECURITY_LOW };
    int fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);

    if (fd < 0)
        return -1;
    addr.l2_family = AF_BLUETOOTH;
    addr.l2_cid = htobs(ATT_CID);
    addr.l2_bdaddr_type = BDADDR_LE_PUBLIC;
    bacpy(&addr.l2_bdaddr, src);
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) ||
        setsockopt(fd, SOL_BLUETOOTH, BT_SECURITY, &sec, sizeof(sec)) ||
        listen(fd, 1)) {
        close(fd);
        return -1;
    }
    return fd;
}

static void accepted(int fd, uint32_t events, void *user_data)
{
    struct app *app = user_data;
    struct sockaddr_l2 addr = { 0 };
    socklen_t len = sizeof(addr);
    char peer[18];
    int client;

    if (!(events & EPOLLIN))
        return;
    client = accept(fd, (struct sockaddr *) &addr, &len);
    if (client < 0)
        return;
    if (app->conn) {
        close(client);
        return;
    }
    ba2str(&addr.l2_bdaddr, peer);
    stop_advertising(app);
    app->conn = connection_new(app, client);
    if (!app->conn) {
        close(client);
        start_advertising(app);
        return;
    }
    printf("[BLE] Connected: %s\n", peer);
}

static bool periodic_notify(void *user_data)
{
    struct app *app = user_data;
    char value[24];

    if (!app->conn || !app->conn->subscribed)
        return true;
    snprintf(value, sizeof(value), "tick:%u", ++app->tick);
    send_notify(app->conn, (const uint8_t *) value, strlen(value));
    return true;
}

static void signal_cb(int sig, void *user_data)
{
    if (sig != SIGINT && sig != SIGTERM)
        return;
    printf("[BLE] Signal %d, stopping\n", sig);
    mainloop_quit();
}

static void hangup_cb(int sig)
{
    // mainloop_run_with_signal handles SIGTERM via signalfd. Relay terminal
    // hangup there so the normal cleanup path removes advertising.
    kill(getpid(), SIGTERM);
}

static int fake_client(int fd, uint16_t write_handle, uint16_t ccc_handle)
{
    struct timeval tv = { .tv_sec = 3 };
    uint8_t buf[256];
    uint8_t ccc[] = { 0x12, ccc_handle & 0xff, ccc_handle >> 8, 1, 0 };
    uint8_t write[] = { 0x12, write_handle & 0xff, write_handle >> 8,
                        'h', 'e', 'l', 'l', 'o' };
    bool ccc_rsp = false, write_rsp = false, echo = false;
    ssize_t n;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (send(fd, ccc, sizeof(ccc), 0) != sizeof(ccc))
        return 1;
    while (!ccc_rsp) {
        n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            return 1;
        ccc_rsp = buf[0] == BT_ATT_OP_WRITE_RSP;
    }
    if (send(fd, write, sizeof(write), 0) != sizeof(write))
        return 1;
    while (!write_rsp || !echo) {
        n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            return 1;
        if (buf[0] == BT_ATT_OP_WRITE_RSP)
            write_rsp = true;
        if (n == 8 && buf[0] == BT_ATT_OP_HANDLE_NFY &&
            !memcmp(buf + 3, "hello", 5))
            echo = true;
    }
    puts("[SELFTEST] Write response and echo notification received");
    return 0;
}

static int self_test(struct app *app)
{
    int fds[2], status;
    pid_t pid;

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds))
        return 1;
    app->conn = connection_new(app, fds[0]);
    if (!app->conn) {
        close(fds[0]);
        close(fds[1]);
        return 1;
    }
    pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0) {
        int result;
        close(fds[0]);
        result = fake_client(fds[1], app->conn->write_handle,
                             app->conn->ccc_handle);
        close(fds[1]);
        _exit(result);
    }
    close(fds[1]);
    mainloop_run_with_signal(signal_cb, NULL);
    waitpid(pid, &status, 0);
    if (app->conn) {
        connection_destroy(app->conn);
        app->conn = NULL;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) ||
        (app->self_test_restart && !app->restart_passed)) {
        fprintf(stderr, "[SELFTEST] Failed\n");
        return 1;
    }
    puts("[SELFTEST] PASS");
    return 0;
}

int main(int argc, char **argv)
{
    struct app app = { .listen_fd = -1 };
    bdaddr_t addr;
    int dev, result;

    setvbuf(stdout, NULL, _IONBF, 0);
    app.self_test_restart = argc == 2 &&
                            !strcmp(argv[1], "--self-test-restart");
    app.self_test = app.self_test_restart ||
                    (argc == 2 && !strcmp(argv[1], "--self-test"));
    if (argc > 1 && !app.self_test) {
        fprintf(stderr, "Usage: %s [--self-test|--self-test-restart]\n",
                argv[0]);
        return 2;
    }
    mainloop_init();
    if (app.self_test)
        return self_test(&app);

    signal(SIGHUP, hangup_cb);
    signal(SIGPIPE, SIG_IGN);

    dev = hci_devid("hci0");
    if (dev < 0 || hci_devba(dev, &addr) < 0) {
        fprintf(stderr, "[BLE] hci0 is not ready\n");
        return 1;
    }
    app.listen_fd = listen_att(&addr);
    if (app.listen_fd < 0 ||
        mainloop_add_fd(app.listen_fd, EPOLLIN, accepted, &app, NULL) < 0) {
        perror("[BLE] ATT listen");
        if (app.listen_fd >= 0)
            close(app.listen_fd);
        return 1;
    }
    puts("[BLE] ATT server listening; Ctrl-C stops the test");
    if (!start_advertising(&app)) {
        mainloop_remove_fd(app.listen_fd);
        close(app.listen_fd);
        return 1;
    }
    timeout_add(5000, periodic_notify, &app, NULL);
    result = mainloop_run_with_signal(signal_cb, NULL);
    stop_advertising(&app);
    connection_destroy(app.conn);
    mainloop_remove_fd(app.listen_fd);
    close(app.listen_fd);
    return result ? 1 : 0;
}
