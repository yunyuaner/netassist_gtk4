#include "udp_io.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#define closesocket close
#endif

struct UdpIo {
    udp_log_fn log_cb;
    void* log_user;

    udp_packet_fn pkt_cb;
    void* pkt_user;

    char* local_ip;
    char* target_ip;
    int local_port;
    int target_port;
    int rx_hex;
    int tx_hex;

    int sock;
    GThread* thread;
    GMutex lock;
    gboolean stop;
};

typedef struct {
    udp_log_fn fn;
    void* user;
    char* msg;
} UdpLogTask;

UdpIo* udp_io_new(udp_log_fn log_cb, void* log_user,
                  udp_packet_fn pkt_cb, void* pkt_user) {
    UdpIo* io = g_new0(UdpIo, 1);
    io->log_cb = log_cb;
    io->log_user = log_user;
    io->pkt_cb = pkt_cb;
    io->pkt_user = pkt_user;
    io->sock = -1;
    g_mutex_init(&io->lock);
    return io;
}

static gboolean log_idle_cb(gpointer data) {
    UdpLogTask* t = (UdpLogTask*)data;
    if (t && t->fn && t->msg) {
        t->fn(t->user, t->msg);
    }
    if (t) g_free(t->msg);
    g_free(t);
    return G_SOURCE_REMOVE;
}

static void log_async(UdpIo* io, const char* fmt, ...) {
    if (!io || !io->log_cb) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UdpLogTask* t = g_new0(UdpLogTask, 1);
    t->fn = io->log_cb;
    t->user = io->log_user;
    t->msg = g_strdup(buf);
    g_idle_add(log_idle_cb, t);
}

static void cfg_clear(UdpIo* io) {
    if (!io) return;
    g_free(io->local_ip); io->local_ip = NULL;
    g_free(io->target_ip); io->target_ip = NULL;
}

static void cfg_set(UdpIo* io, const NetConfig* cfg) {
    if (!io || !cfg) return;
    cfg_clear(io);
    io->local_ip = g_strdup(cfg->local_ip ? cfg->local_ip : "0.0.0.0");
    io->target_ip = g_strdup(cfg->target_ip ? cfg->target_ip : "127.0.0.1");
    io->local_port = cfg->local_port;
    io->target_port = cfg->target_port;
    io->rx_hex = cfg->rx_hex;
    io->tx_hex = cfg->tx_hex;
}

static gboolean ensure_winsock(void) {
#ifdef _WIN32
    static gboolean inited = FALSE;
    if (inited) return TRUE;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return FALSE;
    inited = TRUE;
#endif
    return TRUE;
}

static gboolean parse_hex_string(const char* s, uint8_t** out, size_t* out_len) {
    if (!s || !out || !out_len) return FALSE;
    size_t len = strlen(s);
    uint8_t* buf = g_new(uint8_t, len / 2 + 1);
    size_t bi = 0;
    int hi = -1;
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
        else { g_free(buf); return FALSE; }

        if (hi < 0) hi = v;
        else { buf[bi++] = (uint8_t)((hi << 4) | v); hi = -1; }
    }
    if (hi >= 0) { g_free(buf); return FALSE; }
    *out = buf;
    *out_len = bi;
    return TRUE;
}

static gpointer recv_thread(gpointer data) {
    UdpIo* io = (UdpIo*)data;
    uint8_t buf[2048];
    while (TRUE) {
        g_mutex_lock(&io->lock);
        gboolean stop = io->stop;
        int sock = io->sock;
        g_mutex_unlock(&io->lock);
        if (stop || sock < 0) break;

        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &flen); // Ensure recv uses current vars
        if (n > 0) {
            char addr[64];
            inet_ntop(AF_INET, &from.sin_addr, addr, sizeof(addr));
            log_async(io, "[RECV] %d bytes from %s:%d", n, addr, ntohs(from.sin_port));
            if (io->pkt_cb) io->pkt_cb(io->pkt_user, buf, (size_t)n);
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEINTR || err == WSAEWOULDBLOCK) continue;
            if (io->stop) break;
#else
            if (errno == EINTR || errno == EAGAIN) continue;
            if (io->stop) break;
#endif
            log_async(io, "[RECV] error, exiting loop");
            break;
        }
    }
    return NULL;
}

void udp_io_free(UdpIo* io) {
    if (!io) return;
    udp_io_close(io);
    cfg_clear(io);
    g_mutex_clear(&io->lock);
    g_free(io);
}

gboolean udp_io_apply_config(UdpIo* io, const NetConfig* cfg) {
    if (!io || !cfg) return FALSE;
    cfg_set(io, cfg);
    return TRUE;
}

void udp_io_close(UdpIo* io) {
    if (!io) return;
    g_mutex_lock(&io->lock);
    if (io->sock >= 0) {
        io->stop = TRUE;
        closesocket(io->sock);
    }
    g_mutex_unlock(&io->lock);

    if (io->thread) {
        g_thread_join(io->thread);
        io->thread = NULL;
    }

    g_mutex_lock(&io->lock);
    io->sock = -1;
    g_mutex_unlock(&io->lock);
}

gboolean udp_io_open(UdpIo* io) {
    if (!io) return FALSE;
    if (!ensure_winsock()) {
        log_async(io, "[NET] WSAStartup failed");
        return FALSE;
    }

    udp_io_close(io);

    int sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_async(io, "[NET] create socket failed");
        return FALSE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)io->local_port);
    inet_pton(AF_INET, io->local_ip ? io->local_ip : "0.0.0.0", &addr.sin_addr);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_async(io, "[NET] bind failed for %s:%d", io->local_ip ? io->local_ip : "0.0.0.0", io->local_port);
        closesocket(sock);
        return FALSE;
    }

#ifndef _WIN32
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#else
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);
#endif

    g_mutex_lock(&io->lock);
    io->sock = sock;
    io->stop = FALSE;
    io->thread = g_thread_new("udp-recv", recv_thread, io);
    g_mutex_unlock(&io->lock);

    log_async(io, "[NET] UDP bound at %s:%d", io->local_ip ? io->local_ip : "0.0.0.0", io->local_port);
    return TRUE;
}

gboolean udp_io_send(UdpIo* io, const uint8_t* data, size_t len, int is_hex_mode) {
    if (!io) return FALSE;
    if (!data) { log_async(io, "[SEND] empty payload skipped"); return FALSE; }

    const uint8_t* payload = data;
    size_t payload_len = len;
    uint8_t* hex_buf = NULL;
    if (is_hex_mode) {
        if (!parse_hex_string((const char*)data, &hex_buf, &payload_len)) {
            log_async(io, "[SEND] hex parse failed");
            return FALSE;
        }
        payload = hex_buf;
    }

    g_mutex_lock(&io->lock);
    int sock = io->sock;
    g_mutex_unlock(&io->lock);

    if (sock < 0) {
        log_async(io, "[SEND] socket not ready; apply config first");
        if (hex_buf) g_free(hex_buf);
        return FALSE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)io->target_port);
    inet_pton(AF_INET, io->target_ip ? io->target_ip : "127.0.0.1", &addr.sin_addr);

    int sent = sendto(sock, (const char*)payload, (int)payload_len, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        log_async(io, "[SEND] failed: errno=%d", errno);
    } else {
        log_async(io, "[SEND] manual len=%u mode=%s -> %s:%d", (unsigned)payload_len,
                  is_hex_mode ? "HEX" : "ASCII",
                  io->target_ip ? io->target_ip : "127.0.0.1",
                  io->target_port);
    }

    if (hex_buf) g_free(hex_buf);
    return sent >= 0;
}
