#include "app_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "udp_io.h"

struct AppController {
    BackendAPI api;
    void* ui_user;

    ui_log_append_fn log_append;
    ui_script_state_fn script_state_set;
    ui_packet_append_fn pkt_append;

    NetConfig last_cfg;
    UdpIo* udp;
};

static void app_logf(AppController* c, const char* fmt, ...) {
    if (!c || !c->log_append) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    c->log_append(c->ui_user, buf);
}

static void api_apply_config(void* user, const NetConfig* cfg) {
    AppController* c = (AppController*)user;
    if (!cfg) return;
    c->last_cfg = *cfg;

    app_logf(c, "[CFG] local=%s:%d target=%s:%d rx=%s tx=%s",
         cfg->local_ip ? cfg->local_ip : "(null)", cfg->local_port,
         cfg->target_ip ? cfg->target_ip : "(null)", cfg->target_port,
         cfg->rx_hex ? "HEX" : "ASCII",
         cfg->tx_hex ? "HEX" : "ASCII");

    if (c->udp) {
        udp_io_apply_config(c->udp, cfg);
        udp_io_open(c->udp);
    }
}

static void api_close(void* user) {
    AppController* c = (AppController*)user;
    app_logf(c, "[NET] close requested");
    if (c->udp) udp_io_close(c->udp);
}

static void api_send_manual(void* user, const uint8_t* data, size_t len, int is_hex_mode) {
    AppController* c = (AppController*)user;
    if (c->udp) udp_io_send(c->udp, data, len, is_hex_mode);
    else app_logf(c, "[SEND] UDP not initialized");
}

static void api_script_run(void* user, const char* script_text) {
    AppController* c = (AppController*)user;
    if (c->script_state_set) c->script_state_set(c->ui_user, SCRIPT_RUNNING, "running");
    app_logf(c, "[SCRIPT] RUN (%u bytes)", (unsigned)(script_text ? strlen(script_text) : 0));
    // TODO: 后续接入 parser/VM，运行时把 print/log 打到 log_append
}

static void api_script_pause(void* user) {
    AppController* c = (AppController*)user;
    if (c->script_state_set) c->script_state_set(c->ui_user, SCRIPT_PAUSED, "paused");
    app_logf(c, "[SCRIPT] PAUSE");
}

static void api_script_stop(void* user) {
    AppController* c = (AppController*)user;
    if (c->script_state_set) c->script_state_set(c->ui_user, SCRIPT_STOPPED, "stopped");
    app_logf(c, "[SCRIPT] STOP");
}

static void api_script_load(void* user, const char* path) {
    AppController* c = (AppController*)user;
    app_logf(c, "[SCRIPT] load file: %s", path ? path : "(null)");
}

static void api_script_save(void* user, const char* path, const char* script_text) {
    AppController* c = (AppController*)user;
    app_logf(c, "[SCRIPT] save file: %s (%u bytes)", path ? path : "(null)",
         (unsigned)(script_text ? strlen(script_text) : 0));
}

static void api_clear_log(void* user) {
    AppController* c = (AppController*)user;
    app_logf(c, "[UI] clear log requested (UI should clear its view)");
}

AppController* app_controller_new(void) {
    AppController* c = (AppController*)calloc(1, sizeof(AppController));
    if (!c) return NULL;

    c->api.on_apply_config = api_apply_config;
    c->api.on_close = api_close;
    c->api.on_send_manual = api_send_manual;
    c->api.on_script_run = api_script_run;
    c->api.on_script_pause = api_script_pause;
    c->api.on_script_stop = api_script_stop;
    c->api.on_script_load_file = api_script_load;
    c->api.on_script_save_file = api_script_save;
    c->api.on_clear_log = api_clear_log;

    // 默认配置（方便演示）
    c->last_cfg.local_ip = "127.0.0.1";
    c->last_cfg.local_port = 9000;
    c->last_cfg.target_ip = "127.0.0.1";
    c->last_cfg.target_port = 9001;
    c->last_cfg.rx_hex = 1;
    c->last_cfg.tx_hex = 1;

    c->udp = NULL;

    return c;
}

void app_controller_free(AppController* c) {
    if (!c) return;
    if (c->udp) udp_io_free(c->udp);
    free(c);
}

const BackendAPI* app_controller_api(AppController* c) {
    return c ? &c->api : NULL;
}

void* app_controller_user(AppController* c) {
    return (void*)c;
}

void app_controller_bind_ui(AppController* c, void* ui_user,
                            ui_log_append_fn log_append,
                            ui_script_state_fn script_state_set,
                            ui_packet_append_fn pkt_append) {
    if (!c) return;
    c->ui_user = ui_user;
    c->log_append = log_append;
    c->script_state_set = script_state_set;
    c->pkt_append = pkt_append;

    if (!c->udp && log_append) {
        c->udp = udp_io_new(log_append, ui_user, pkt_append, ui_user);
        udp_io_apply_config(c->udp, &c->last_cfg);
    }
}
