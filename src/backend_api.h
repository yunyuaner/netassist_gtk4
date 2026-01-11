#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// UI -> Backend 的“命令/意图”接口（由 controller 实现）
// 你后续可以把这些映射到：DSL parser/VM + UDP socket + timers

typedef struct {
    const char* local_ip;
    int         local_port;

    const char* target_ip;
    int         target_port;

    int         rx_hex;     // 1=HEX, 0=ASCII
    int         tx_hex;     // 1=HEX, 0=ASCII
} NetConfig;

typedef enum {
    SCRIPT_STOPPED = 0,
    SCRIPT_RUNNING,
    SCRIPT_PAUSED,
    SCRIPT_ERROR
} ScriptState;

typedef struct BackendAPI {
    // --- 网络配置/连接控制 ---
    void (*on_apply_config)(void* user, const NetConfig* cfg);
    void (*on_close)(void* user);

    // --- 手动发送 ---
    void (*on_send_manual)(void* user, const uint8_t* data, size_t len, int is_hex_mode);

    // --- 脚本 ---
    void (*on_script_run)(void* user, const char* script_text);
    void (*on_script_pause)(void* user);
    void (*on_script_stop)(void* user);

    // --- 脚本导入/保存（可选，先留空）---
    void (*on_script_load_file)(void* user, const char* path);
    void (*on_script_save_file)(void* user, const char* path, const char* script_text);

    // --- 日志清理 ---
    void (*on_clear_log)(void* user);
} BackendAPI;

#ifdef __cplusplus
}
#endif
