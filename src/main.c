#include <gtk/gtk.h>
#include "app_controller.h"
#include "ui_main.h"

static void on_activate(GtkApplication* app, gpointer user_data) {
    (void)user_data;

    AppController* ctrl = app_controller_new();
    const BackendAPI* api = app_controller_api(ctrl);

    // Create UI
    UIMain* ui = ui_main_new(app, api, app_controller_user(ctrl));

    // Bind controller -> UI callbacks
    app_controller_bind_ui(ctrl, (void*)ui, ui_main_log_append, ui_main_set_script_state);

    // NOTE:
    // - 这里 ctrl/ui 的生命周期示例没有做复杂管理。
    // - 你后续可以把 ctrl 放到 application data 里，在 shutdown 时释放。
    (void)ui;
}

int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("com.example.netassist", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
