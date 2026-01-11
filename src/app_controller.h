#pragma once
#include "backend_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AppController AppController;

// 创建 controller（目前先做“空后端”，只打印/写到日志）
AppController* app_controller_new(void);
void app_controller_free(AppController* c);

// 获取 backend 接口（交给 UI 调用）
const BackendAPI* app_controller_api(AppController* c);
void* app_controller_user(AppController* c);

// controller -> UI 的回调（UI 在创建时注册）
typedef void (*ui_log_append_fn)(void* ui_user, const char* line);
typedef void (*ui_script_state_fn)(void* ui_user, ScriptState st, const char* detail);

void app_controller_bind_ui(AppController* c, void* ui_user,
                            ui_log_append_fn log_append,
                            ui_script_state_fn script_state_set);

#ifdef __cplusplus
}
#endif
