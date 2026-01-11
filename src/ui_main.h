#pragma once
#include <gtk/gtk.h>
#include "backend_api.h"

typedef struct UIMain UIMain;

UIMain* ui_main_new(GtkApplication* app,
                    const BackendAPI* api,
                    void* api_user);

void ui_main_free(UIMain* ui);

// controller -> UI 回调
void ui_main_log_append(void* ui_user, const char* line);
void ui_main_set_script_state(void* ui_user, ScriptState st, const char* detail);
void ui_main_packet_append(void* ui_user, const uint8_t* data, size_t len);

// 取得顶层 window
GtkWindow* ui_main_window(UIMain* ui);
