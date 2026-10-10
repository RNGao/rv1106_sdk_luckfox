/* Small single-client HTTPS streaming benchmark, not a production web server. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#define BLOCK 65536
#define TOTAL (64ULL * 1024 * 1024)
static unsigned char pattern[BLOCK];
static char token[128];

static int send_all(SSL *ssl, const void *buf, size_t n)
{
    const unsigned char *p = buf;
    while (n) {
        int r = SSL_write(ssl, p, (int)n);
        if (r <= 0) return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int response(SSL *ssl, int status, const char *type, uint64_t n)
{
    char hdr[256];
    int len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %" PRIu64
        "\r\nConnection: close\r\n\r\n", status, status == 200 ? "OK" : "Error", type, n);
    return send_all(ssl, hdr, (size_t)len);
}

static int handle(SSL *ssl)
{
    char hdr[8192], method[16], path[128];
    size_t used = 0;
    while (used < sizeof(hdr) - 1) {
        if (SSL_read(ssl, hdr + used, 1) != 1) return -1;
        used++;
        if (used >= 4 && !memcmp(hdr + used - 4, "\r\n\r\n", 4)) break;
    }
    hdr[used] = 0;
    if (used < 4 || memcmp(hdr + used - 4, "\r\n\r\n", 4)) return -1;
    if (sscanf(hdr, "%15s %127s", method, path) != 2) return -1;
    char *save = NULL, *line = strtok_r(hdr, "\r\n", &save);
    int auth = 0, chunked = 0;
    uint64_t length = 0;
    while ((line = strtok_r(NULL, "\r\n", &save))) {
        if (!strncasecmp(line, "Authorization: Bearer ", 22)) {
            const char *candidate = line + 22;
            auth = strlen(candidate) == strlen(token) &&
                CRYPTO_memcmp(candidate, token, strlen(token)) == 0;
        }
        if (!strncasecmp(line, "Content-Length:", 15)) {
            char *end;
            errno = 0;
            length = strtoull(line + 15, &end, 10);
            if (errno || *end) return -1;
        }
        if (!strncasecmp(line, "Transfer-Encoding:", 18)) chunked = 1;
    }
    if (!auth) return response(ssl, 403, "text/plain", 0);
    if (!strcmp(method, "GET") && !strcmp(path, "/download")) {
        if (response(ssl, 200, "application/octet-stream", TOTAL)) return -1;
        for (uint64_t sent = 0; sent < TOTAL; sent += BLOCK)
            if (send_all(ssl, pattern, BLOCK)) return -1;
        printf("download bytes=%llu tls=%s\n", TOTAL, SSL_get_version(ssl));
        return 0;
    }
    if (!strcmp(method, "POST") && !strcmp(path, "/upload")) {
        if (length != TOTAL || chunked) return response(ssl, 400, "text/plain", 0);
        unsigned char buf[BLOCK], digest[SHA256_DIGEST_LENGTH];
        SHA256_CTX hash;
        if (!SHA256_Init(&hash)) return -1;
        uint64_t remaining = length;
        while (remaining) {
            int want = remaining > BLOCK ? BLOCK : (int)remaining;
            int n = SSL_read(ssl, buf, want);
            if (n <= 0) return -1;
            if (!SHA256_Update(&hash, buf, (size_t)n)) return -1;
            remaining -= (uint64_t)n;
        }
        if (!SHA256_Final(digest, &hash)) return -1;
        char hex[65], body[256];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
            snprintf(hex + 2*i, 3, "%02x", digest[i]);
        int n = snprintf(body, sizeof(body),
            "{\"bytes\":%" PRIu64 ",\"sha256\":\"%s\",\"tls\":\"%s\"}\n",
            length, hex, SSL_get_version(ssl));
        printf("upload %s", body);
        if (response(ssl, 200, "application/json", (uint64_t)n)) return -1;
        return send_all(ssl, body, (size_t)n);
    }
    return response(ssl, 404, "text/plain", 0);
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "Usage: %s BIND_IP CERT KEY TOKEN_FILE BLOCK_FILE\n", argv[0]);
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    FILE *f = fopen(argv[4], "r");
    if (!f || !fgets(token, sizeof(token), f)) { perror("token"); return 1; }
    fclose(f);
    token[strcspn(token, "\r\n")] = 0;
    if (strlen(token) < 24) { fprintf(stderr, "Token too short\n"); return 1; }
    f = fopen(argv[5], "rb");
    if (!f || fread(pattern, 1, BLOCK, f) != BLOCK) { perror("block"); return 1; }
    fclose(f);
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx || !SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) ||
        SSL_CTX_use_certificate_chain_file(ctx, argv[2]) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, argv[3], SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx) != 1) {
        ERR_print_errors_fp(stderr); return 1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0), reuse = 1;
    if (fd < 0) { perror("socket"); return 1; }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8443);
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1 ||
        bind(fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(fd, 4)) {
        perror("bind/listen"); return 1;
    }
    printf("C HTTPS benchmark listening on %s:8443, 64 MiB/request\n", argv[1]);
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int client = accept(fd, (struct sockaddr *)&peer, &peer_len);
        if (client < 0) { if (errno == EINTR) continue; perror("accept"); break; }
        struct timeval timeout;
        timeout.tv_sec = 30;
        timeout.tv_usec = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        SSL *ssl = SSL_new(ctx);
        if (ssl) {
            SSL_set_fd(ssl, client);
            if (SSL_accept(ssl) == 1) {
                printf("peer=%s cipher=%s\n", inet_ntoa(peer.sin_addr), SSL_get_cipher(ssl));
                if (handle(ssl)) fprintf(stderr, "Request failed/incomplete\n");
                SSL_shutdown(ssl);
            } else ERR_print_errors_fp(stderr);
            SSL_free(ssl);
        }
        close(client);
    }
    close(fd);
    SSL_CTX_free(ctx);
    return 1;
}
