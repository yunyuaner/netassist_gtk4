#pragma once
#include "backend_api.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*udp_log_fn)(void* user, const char* line);
typedef void (*udp_packet_fn)(void* user, const uint8_t* data, size_t len);

typedef struct UdpIo UdpIo;

UdpIo* udp_io_new(udp_log_fn log_cb, void* log_user,
				  udp_packet_fn pkt_cb, void* pkt_user);
void udp_io_free(UdpIo* io);

// store/copy config (strings are duplicated)
gboolean udp_io_apply_config(UdpIo* io, const NetConfig* cfg);

// open/close socket for current config
gboolean udp_io_open(UdpIo* io);
void udp_io_close(UdpIo* io);

// send payload (hex parsing when is_hex_mode=1)
gboolean udp_io_send(UdpIo* io, const uint8_t* data, size_t len, int is_hex_mode);

#ifdef __cplusplus
}
#endif
