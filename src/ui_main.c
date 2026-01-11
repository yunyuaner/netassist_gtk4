#include "ui_main.h"
#include <string.h>
#if defined(HAVE_GTK_SOURCE) || defined(HAVE_GTK_SOURCE_5)
#include <gtksourceview/gtksource.h>
#endif

struct UIMain {
    GtkApplication* app;
    GtkWindow* win;

    const BackendAPI* api;
    void* api_user;

    // 左侧配置控件（只保留关键几个）
    GtkDropDown* dd_proto;
    GtkEntry*    ent_local_ip;
    GtkSpinButton* sp_local_port;

    GtkEntry*    ent_target_ip;
    GtkSpinButton* sp_target_port;

    GtkToggleButton* tg_rx_hex;
    GtkToggleButton* tg_tx_hex;

    // 右侧日志/脚本
    GtkTextView* tv_log;
    GtkTextBuffer* buf_log;

    GtkTextView* tv_pkt;
    GtkTextBuffer* buf_pkt;

    GtkTextView* tv_script;
    GtkTextBuffer* buf_script;
    GtkTextView* tv_script_gutter; /* unused when using GtkSourceView line numbers */
    /* syntax tags */
    GtkTextTag* tag_kw;
    GtkTextTag* tag_str;
    GtkTextTag* tag_comment;
    GtkTextTag* tag_num;
    GtkTextTag* tag_pp;
    GtkTextTag* tag_op;

    // 脚本输出（可折叠先省略，先用同一日志）
    GtkLabel* lb_script_state;

    // 底部发送区
    GtkTextView* tv_send;
    GtkTextBuffer* buf_send;
    GtkButton* btn_send;

    GtkToggleButton* tg_send_mode_manual;
    GtkToggleButton* tg_send_mode_script;

    // 脚本控制
    GtkButton* btn_run;
    GtkButton* btn_pause;
    GtkButton* btn_stop;
};

static void apply_script_highlight(UIMain* ui);

static void append_text(GtkTextBuffer* b, const char* s) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(b, &end);
    gtk_text_buffer_insert(b, &end, s, -1);
    gtk_text_buffer_insert(b, &end, "\n", -1);
}

static void append_hexdump(GtkTextBuffer* b, const uint8_t* data, size_t len) {
    if (!b || !data || len == 0) return;
    GString* out = g_string_new(NULL);
    for (size_t i = 0; i < len; i += 16) {
        g_string_append_printf(out, "%08zx  ", i);
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < len) g_string_append_printf(out, "%02x ", data[i + j]);
            else g_string_append(out, "   ");
            if (j == 7) g_string_append(out, " ");
        }
        g_string_append(out, " | ");
        size_t ascii_count = (len - i) < 16 ? (len - i) : 16;
        for (size_t j = 0; j < ascii_count; ++j) {
            uint8_t c = data[i + j];
            g_string_append_c(out, (c >= 32 && c <= 126) ? (char)c : '.');
        }
        for (size_t j = ascii_count; j < 16; ++j) g_string_append_c(out, ' ');
        g_string_append(out, " |");
        g_string_append(out, "\n");
    }
    append_text(b, out->str);
    g_string_free(out, TRUE);
}

static NetConfig ui_collect_cfg(UIMain* ui) {
    NetConfig c;
    memset(&c, 0, sizeof(c));

    c.local_ip = gtk_editable_get_text(GTK_EDITABLE(ui->ent_local_ip));
    c.local_port = (int)gtk_spin_button_get_value(ui->sp_local_port);

    c.target_ip = gtk_editable_get_text(GTK_EDITABLE(ui->ent_target_ip));
    c.target_port = (int)gtk_spin_button_get_value(ui->sp_target_port);

    c.rx_hex = gtk_toggle_button_get_active(ui->tg_rx_hex) ? 1 : 0;
    c.tx_hex = gtk_toggle_button_get_active(ui->tg_tx_hex) ? 1 : 0;
    return c;
}

static void on_apply_clicked(GtkButton* b, gpointer user_data) {
    (void)b;
    UIMain* ui = (UIMain*)user_data;
    if (!ui->api || !ui->api->on_apply_config) return;
    NetConfig cfg = ui_collect_cfg(ui);
    ui->api->on_apply_config(ui->api_user, &cfg);
}

static void on_clear_log_clicked(GtkButton* b, gpointer user_data) {
    (void)b;
    UIMain* ui = (UIMain*)user_data;
    gtk_text_buffer_set_text(ui->buf_log, "", -1);
    if (ui->api && ui->api->on_clear_log) ui->api->on_clear_log(ui->api_user);
}

static void on_send_clicked(GtkButton* b, gpointer user_data) {
    (void)b;
    UIMain* ui = (UIMain*)user_data;
    if (!ui->api || !ui->api->on_send_manual) return;

    // 取发送区文本（演示：直接按 ASCII bytes 发；HEX 模式后续你接入解析）
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ui->buf_send, &start, &end);
    char* txt = gtk_text_buffer_get_text(ui->buf_send, &start, &end, FALSE);

    // demo：把文本当 bytes 发送
    const uint8_t* data = (const uint8_t*)txt;
    size_t len = txt ? strlen(txt) : 0;

    int is_hex = gtk_toggle_button_get_active(ui->tg_tx_hex) ? 1 : 0;
    ui->api->on_send_manual(ui->api_user, data, len, is_hex);

    g_free(txt);
}

static void on_script_run(GtkButton* b, gpointer user_data) {
    (void)b;
    UIMain* ui = (UIMain*)user_data;
    if (!ui->api || !ui->api->on_script_run) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ui->buf_script, &start, &end);
    char* script = gtk_text_buffer_get_text(ui->buf_script, &start, &end, FALSE);

    ui->api->on_script_run(ui->api_user, script ? script : "");
    g_free(script);
}

static void on_script_pause(GtkButton* b, gpointer user_data) {
    (void)b;
    UIMain* ui = (UIMain*)user_data;
    if (ui->api && ui->api->on_script_pause) ui->api->on_script_pause(ui->api_user);
}

static void on_script_stop(GtkButton* b, gpointer user_data) {
    (void)b;
    UIMain* ui = (UIMain*)user_data;
    if (ui->api && ui->api->on_script_stop) ui->api->on_script_stop(ui->api_user);
}

static void on_script_buffer_changed(GtkTextBuffer* buf, gpointer user_data) {
    UIMain* ui = (UIMain*)user_data;
    // if custom gutter exists, update it; otherwise rely on GtkSourceView's line numbers
    if (ui->tv_script_gutter) {
        int lines = gtk_text_buffer_get_line_count(buf);
        GString* s = g_string_new(NULL);
        for (int i = 1; i <= lines; ++i) g_string_append_printf(s, "%d\n", i);
        GtkTextBuffer* gb = gtk_text_view_get_buffer(ui->tv_script_gutter);
        if (gb) gtk_text_buffer_set_text(gb, s->str, -1);
        g_string_free(s, TRUE);
    }

    apply_script_highlight(ui);
}

/* simple regex-based highlighting for our DSL (works in both GtkSourceView and plain TextView) */
static void apply_script_highlight(UIMain* ui) {
    if (!ui || !ui->buf_script) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ui->buf_script, &start, &end);
    // clear previous tags
    gtk_text_buffer_remove_tag(ui->buf_script, ui->tag_kw, &start, &end);
    gtk_text_buffer_remove_tag(ui->buf_script, ui->tag_str, &start, &end);
    gtk_text_buffer_remove_tag(ui->buf_script, ui->tag_comment, &start, &end);
    gtk_text_buffer_remove_tag(ui->buf_script, ui->tag_num, &start, &end);
    gtk_text_buffer_remove_tag(ui->buf_script, ui->tag_pp, &start, &end);
    gtk_text_buffer_remove_tag(ui->buf_script, ui->tag_op, &start, &end);

    // compile regexes once
    static GRegex* re_kw = NULL;
    static GRegex* re_str = NULL;
    static GRegex* re_comment = NULL;
    static GRegex* re_num = NULL;
    static GRegex* re_pp = NULL;
    static GRegex* re_op = NULL;
    if (!re_kw) {
        re_kw = g_regex_new("\\b(loop|break|continue|if|else|return|fn|let|var|while|for|sleep|udp\\.send|rand_int|rand_bytes|byte_at|crc16|printf)\\b", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
        re_str = g_regex_new("\"([^\"\\\\]|\\\\.)*\"", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
        re_comment = g_regex_new("//.*$", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
        re_num = g_regex_new("\\b[0-9]+(\\.[0-9]+)?\\b", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
        re_pp = g_regex_new("^\\s*(#\\w+|%define|%set|%include).*$", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
        re_op = g_regex_new("[\\(\\)\\{\\}\\[\\]\\+\\-\\*/=<>!]+", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
    }

    char* text = gtk_text_buffer_get_text(ui->buf_script, &start, &end, FALSE);
    if (!text) return;

    GMatchInfo* info = NULL;
    // helper macro to avoid repetition
#define APPLY_TAG(re, tagptr) \
    do { \
        if ((re) && (tagptr)) { \
            g_regex_match((re), text, G_REGEX_MATCH_NOTEMPTY, &info); \
            while (info && g_match_info_matches(info)) { \
                int s = 0, e = 0; \
                if (g_match_info_fetch_pos(info, 0, &s, &e)) { \
                    GtkTextIter ts, te; \
                    gtk_text_buffer_get_iter_at_offset(ui->buf_script, &ts, s); \
                    gtk_text_buffer_get_iter_at_offset(ui->buf_script, &te, e); \
                    gtk_text_buffer_apply_tag(ui->buf_script, (tagptr), &ts, &te); \
                } \
                g_match_info_next(info, NULL); \
            } \
            if (info) { g_match_info_free(info); info = NULL; } \
        } \
    } while (0)

    APPLY_TAG(re_kw, ui->tag_kw);
    APPLY_TAG(re_str, ui->tag_str);
    APPLY_TAG(re_comment, ui->tag_comment);
    APPLY_TAG(re_num, ui->tag_num);
    APPLY_TAG(re_pp, ui->tag_pp);
    APPLY_TAG(re_op, ui->tag_op);

#undef APPLY_TAG
    g_free(text);
}

/* forward decl for async file dialog callback (GTK>=4.10) */
static void on_file_dialog_opened(GObject* source_object, GAsyncResult* res, gpointer user_data);

static void on_script_load_clicked(GtkButton* b, gpointer user_data) {
    (void)b;
    UIMain* ui = (UIMain*)user_data;
#if GTK_CHECK_VERSION(4,10,0)
    GtkFileDialog* dlg = gtk_file_dialog_new();
    gtk_file_dialog_open(dlg, GTK_WINDOW(ui->win), NULL, (GAsyncReadyCallback)on_file_dialog_opened, ui);
#else
    GtkWidget* dlg = gtk_dialog_new_with_buttons("Load Script", GTK_WINDOW(ui->win),
        GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_hexpand(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(content), entry);
    gtk_widget_show(GTK_WIDGET(entry));

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == GTK_RESPONSE_ACCEPT) {
        const char* path = gtk_entry_get_text(GTK_ENTRY(entry));
        if (path && *path) {
            gchar* contents = NULL;
            gsize len = 0;
            GError* err = NULL;
            if (g_file_get_contents(path, &contents, &len, &err)) {
                gtk_text_buffer_set_text(ui->buf_script, contents, (gint)len);
                g_free(contents);
                on_script_buffer_changed(ui->buf_script, ui);
            } else {
                if (err) g_clear_error(&err);
            }
            if (ui->api && ui->api->on_script_load_file) ui->api->on_script_load_file(ui->api_user, path);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dlg));
#endif
}

static GtkWidget* build_left_panel(UIMain* ui) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);

    GtkWidget* fr_net = gtk_frame_new("Network");
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_widget_set_margin_top(grid, 8);
    gtk_widget_set_margin_bottom(grid, 8);
    gtk_widget_set_margin_start(grid, 8);
    gtk_widget_set_margin_end(grid, 8);
    gtk_frame_set_child(GTK_FRAME(fr_net), grid);

    GtkWidget* lb_proto = gtk_label_new("Protocol");
    const char* protos[] = {"UDP", "TCP", NULL};
    ui->dd_proto = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(protos));

    GtkWidget* lb_lip = gtk_label_new("Local IP");
    ui->ent_local_ip = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(ui->ent_local_ip), "127.0.0.1");

    GtkWidget* lb_lport = gtk_label_new("Local Port");
    ui->sp_local_port = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 65535, 1));
    gtk_spin_button_set_value(ui->sp_local_port, 9000);

    GtkWidget* lb_tip = gtk_label_new("Remote IP");
    ui->ent_target_ip = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(ui->ent_target_ip), "127.0.0.1");

    GtkWidget* lb_tport = gtk_label_new("Remote Port");
    ui->sp_target_port = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 65535, 1));
    gtk_spin_button_set_value(ui->sp_target_port, 9001);

    GtkWidget* btn_apply = gtk_button_new_with_label("Apply");
    gtk_widget_set_margin_top(btn_apply, 4);
    gtk_widget_set_margin_bottom(btn_apply, 2);
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_apply_clicked), ui);

    gtk_grid_attach(GTK_GRID(grid), lb_proto, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ui->dd_proto), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lb_lip, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ui->ent_local_ip), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lb_lport, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ui->sp_local_port), 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lb_tip, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ui->ent_target_ip), 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lb_tport, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ui->sp_target_port), 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_apply, 0, 5, 2, 1);

    GtkWidget* fr_mode = gtk_frame_new("IO Settings");
    GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(v, 6);
    gtk_widget_set_margin_bottom(v, 6);
    gtk_widget_set_margin_start(v, 6);
    gtk_widget_set_margin_end(v, 6);
    gtk_frame_set_child(GTK_FRAME(fr_mode), v);

    ui->tg_rx_hex = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label("RX HEX"));
    ui->tg_tx_hex = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label("TX HEX"));
    gtk_toggle_button_set_active(ui->tg_rx_hex, TRUE);
    gtk_toggle_button_set_active(ui->tg_tx_hex, TRUE);
    gtk_box_append(GTK_BOX(v), GTK_WIDGET(ui->tg_rx_hex));
    gtk_box_append(GTK_BOX(v), GTK_WIDGET(ui->tg_tx_hex));

    GtkWidget* fr_script = gtk_frame_new("Script Settings");
    GtkWidget* v2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(v2, 6);
    gtk_widget_set_margin_bottom(v2, 6);
    gtk_widget_set_margin_start(v2, 6);
    gtk_widget_set_margin_end(v2, 6);
    gtk_frame_set_child(GTK_FRAME(fr_script), v2);

    GtkWidget* ck_enable = gtk_check_button_new_with_label("Enable Script Mode (UI)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ck_enable), TRUE);
    gtk_box_append(GTK_BOX(v2), ck_enable);

    gtk_box_append(GTK_BOX(box), fr_net);
    gtk_box_append(GTK_BOX(box), fr_mode);
    gtk_box_append(GTK_BOX(box), fr_script);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(box), spacer);

    return box;
}

#if GTK_CHECK_VERSION(4,10,0)
static void on_file_dialog_opened(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    UIMain* ui = (UIMain*)user_data;
    GError* err = NULL;
    GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source_object), res, &err);
    if (!file) {
        if (err) g_clear_error(&err);
        return;
    }

    char* path = g_file_get_path(file);
    if (path) {
        gchar* contents = NULL;
        gsize len = 0;
        GError* err2 = NULL;
        if (g_file_get_contents(path, &contents, &len, &err2)) {
            gtk_text_buffer_set_text(ui->buf_script, contents, (gint)len);
            g_free(contents);
            // update gutter and highlighting
            on_script_buffer_changed(ui->buf_script, ui);
        } else {
            if (err2) g_clear_error(&err2);
        }

        if (ui->api && ui->api->on_script_load_file) ui->api->on_script_load_file(ui->api_user, path);
        g_free(path);
    }

    g_object_unref(file);
}
#endif

static GtkWidget* build_log_tab(UIMain* ui) {
    GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget* h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* btn_clear = gtk_button_new_with_label("Clear Log");
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_clear_log_clicked), ui);
    gtk_box_append(GTK_BOX(h), btn_clear);
    {
        // style toolbar with light background to separate from content
        GtkCssProvider* css = gtk_css_provider_new();
        const char* css_data = ".tab-toolbar { background-color: #f5f5f5; padding: 6px; }";
        gtk_css_provider_load_from_string(css, css_data);
        // register provider for display so the class rule applies
        GdkDisplay* disp = gdk_display_get_default();
        if (disp) gtk_style_context_add_provider_for_display(disp, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        // add CSS class to the toolbar widget (modern API)
        gtk_widget_add_css_class(h, "tab-toolbar");
        g_object_unref(css);
    }

    gtk_box_append(GTK_BOX(v), h);

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 2);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(v), sep);

    // system log (compact, ~2-3 lines, scrollable)
    ui->tv_log = GTK_TEXT_VIEW(gtk_text_view_new());
    ui->buf_log = gtk_text_view_get_buffer(ui->tv_log);
    gtk_text_view_set_editable(ui->tv_log, FALSE);
    gtk_text_view_set_monospace(ui->tv_log, TRUE);

    GtkWidget* sc_sys = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc_sys), GTK_WIDGET(ui->tv_log));
    gtk_widget_set_margin_start(sc_sys, 8);
    gtk_widget_set_margin_end(sc_sys, 8);
    gtk_widget_set_margin_top(sc_sys, 2);
    gtk_widget_set_margin_bottom(sc_sys, 6);
    gtk_widget_set_margin_start(GTK_WIDGET(ui->tv_log), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(ui->tv_log), 6);
    gtk_widget_set_size_request(sc_sys, -1, 64); // about 2-3 lines tall
    {
        GtkCssProvider* css = gtk_css_provider_new();
        const char* css_data = ".sys-log-area { background: #eef2fa; border: 1px solid #c7d1ea; }";
        gtk_css_provider_load_from_string(css, css_data);
        GdkDisplay* disp = gdk_display_get_default();
        if (disp) gtk_style_context_add_provider_for_display(disp, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        gtk_widget_add_css_class(sc_sys, "sys-log-area");
        g_object_unref(css);
    }
    gtk_box_append(GTK_BOX(v), sc_sys);

    // packet hexdump area (large, takes most space)
    ui->tv_pkt = GTK_TEXT_VIEW(gtk_text_view_new());
    ui->buf_pkt = gtk_text_view_get_buffer(ui->tv_pkt);
    gtk_text_view_set_editable(ui->tv_pkt, FALSE);
    gtk_text_view_set_monospace(ui->tv_pkt, TRUE);

    GtkWidget* sc_pkt = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc_pkt), GTK_WIDGET(ui->tv_pkt));
    gtk_widget_set_vexpand(sc_pkt, TRUE);
    gtk_widget_set_hexpand(sc_pkt, TRUE);
    gtk_widget_set_margin_start(sc_pkt, 8);
    gtk_widget_set_margin_end(sc_pkt, 8);
    gtk_widget_set_margin_top(sc_pkt, 2);
    gtk_widget_set_margin_bottom(sc_pkt, 4);
    gtk_widget_set_margin_start(GTK_WIDGET(ui->tv_pkt), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(ui->tv_pkt), 6);
    gtk_box_append(GTK_BOX(v), sc_pkt);

    return v;
}

static GtkWidget* build_script_tab(UIMain* ui) {
    GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    // toolbar
    GtkWidget* h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui->btn_run = GTK_BUTTON(gtk_button_new_with_label("> Run"));
    ui->btn_pause = GTK_BUTTON(gtk_button_new_with_label("|| Pause"));
    ui->btn_stop = GTK_BUTTON(gtk_button_new_with_label("[] Stop"));
    GtkWidget* btn_load = gtk_button_new_with_label("Load");

    g_signal_connect(ui->btn_run, "clicked", G_CALLBACK(on_script_run), ui);
    g_signal_connect(ui->btn_pause, "clicked", G_CALLBACK(on_script_pause), ui);
    g_signal_connect(ui->btn_stop, "clicked", G_CALLBACK(on_script_stop), ui);
    g_signal_connect(btn_load, "clicked", G_CALLBACK(on_script_load_clicked), ui);

    ui->lb_script_state = GTK_LABEL(gtk_label_new("Script: Stopped"));

    gtk_box_append(GTK_BOX(h), GTK_WIDGET(ui->btn_run));
    gtk_box_append(GTK_BOX(h), GTK_WIDGET(ui->btn_pause));
    gtk_box_append(GTK_BOX(h), GTK_WIDGET(ui->btn_stop));
    gtk_box_append(GTK_BOX(h), btn_load);
    gtk_box_append(GTK_BOX(h), GTK_WIDGET(ui->lb_script_state));

    gtk_box_append(GTK_BOX(v), h);
    {
        // style toolbar with light background to separate from editor
        GtkCssProvider* css = gtk_css_provider_new();
        const char* css_data = ".tab-toolbar { background-color: #f5f5f5; padding: 6px; }";
        gtk_css_provider_load_from_string(css, css_data);
        GdkDisplay* disp = gdk_display_get_default();
        if (disp) gtk_style_context_add_provider_for_display(disp, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        gtk_widget_add_css_class(h, "tab-toolbar");
        g_object_unref(css);
    }

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 2);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(v), sep);

    /* Editor setup: prefer GtkSourceView when available (gtksourceview-4 or -5), otherwise use GtkTextView + gutter */
    #if defined(HAVE_GTK_SOURCE) || defined(HAVE_GTK_SOURCE_5)
    {
        GtkSourceBuffer* sb = gtk_source_buffer_new(NULL);
        ui->tv_script = GTK_TEXT_VIEW(gtk_source_view_new_with_buffer(sb));
        ui->buf_script = gtk_text_view_get_buffer(ui->tv_script);
        gtk_text_view_set_monospace(ui->tv_script, TRUE);
        gtk_widget_set_hexpand(GTK_WIDGET(ui->tv_script), TRUE);
        gtk_widget_set_vexpand(GTK_WIDGET(ui->tv_script), TRUE);
        gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(ui->tv_script), TRUE);
        ui->tv_script_gutter = NULL;
    }
    #else
    {
        ui->tv_script = GTK_TEXT_VIEW(gtk_text_view_new());
        ui->buf_script = gtk_text_view_get_buffer(ui->tv_script);
        gtk_text_view_set_monospace(ui->tv_script, TRUE);

        // gutter (line numbers)
        ui->tv_script_gutter = GTK_TEXT_VIEW(gtk_text_view_new());
        gtk_text_view_set_editable(ui->tv_script_gutter, FALSE);
        gtk_text_view_set_cursor_visible(ui->tv_script_gutter, FALSE);
        gtk_text_view_set_monospace(ui->tv_script_gutter, TRUE);
        gtk_widget_set_hexpand(GTK_WIDGET(ui->tv_script_gutter), FALSE);
        gtk_widget_set_size_request(GTK_WIDGET(ui->tv_script_gutter), 48, -1);

        // ensure editor expands and is visible
        gtk_widget_set_hexpand(GTK_WIDGET(ui->tv_script), TRUE);
        gtk_widget_set_vexpand(GTK_WIDGET(ui->tv_script), TRUE);

        // apply simple CSS to guarantee readable foreground/background for editor/gutter
        {
            GtkCssProvider* css = gtk_css_provider_new();
            const char* css_data = 
                ".script-editor { color: #000000; background-color: #ffffff; }"
                ".script-gutter { color: #000000; background-color: #ffffff; }";
            gtk_css_provider_load_from_string(css, css_data);
            GdkDisplay* disp = gdk_display_get_default();
            if (disp) gtk_style_context_add_provider_for_display(disp, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            gtk_widget_add_css_class(GTK_WIDGET(ui->tv_script), "script-editor");
            gtk_widget_add_css_class(GTK_WIDGET(ui->tv_script_gutter), "script-gutter");
            g_object_unref(css);
        }
    }
#endif

    // preset script
    const char* preset =
        "loop {\n"
        "  delay_ms = rand_int(30,200)\n"
        "  len      = rand_int(20,1400)\n"
        "  payload  = rand_bytes(len)\n"
        "  udp.send(payload)\n"
        "  sleep(delay_ms)\n"
        "}\n";
    gtk_text_buffer_set_text(ui->buf_script, preset, -1);

    // create syntax tags
    ui->tag_kw = gtk_text_buffer_create_tag(ui->buf_script, "kw",
        "foreground", "#00007f", "weight", PANGO_WEIGHT_BOLD, NULL);
    ui->tag_str = gtk_text_buffer_create_tag(ui->buf_script, "str",
        "foreground", "#8B4513", NULL);
    ui->tag_comment = gtk_text_buffer_create_tag(ui->buf_script, "comment",
        "foreground", "#777777", "style", PANGO_STYLE_ITALIC, NULL);
    ui->tag_num = gtk_text_buffer_create_tag(ui->buf_script, "num",
        "foreground", "#800080", NULL);
    ui->tag_pp = gtk_text_buffer_create_tag(ui->buf_script, "pp",
        "foreground", "#006400", NULL);
    ui->tag_op = gtk_text_buffer_create_tag(ui->buf_script, "op",
        "foreground", "#aa0000", NULL);

    GtkWidget* sc = gtk_scrolled_window_new();

    if (ui->tv_script_gutter) {
        // put gutter and editor side-by-side
        GtkWidget* ed_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_box_append(GTK_BOX(ed_hbox), GTK_WIDGET(ui->tv_script_gutter));
        gtk_box_append(GTK_BOX(ed_hbox), GTK_WIDGET(ui->tv_script));
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), ed_hbox);
    } else {
        // SourceView mode: no custom gutter
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), GTK_WIDGET(ui->tv_script));
    }
    gtk_widget_set_vexpand(sc, TRUE);
    gtk_widget_set_margin_start(sc, 8);
    gtk_widget_set_margin_end(sc, 8);
    gtk_widget_set_margin_top(sc, 4);
    gtk_widget_set_margin_bottom(sc, 4);
    gtk_widget_set_margin_start(GTK_WIDGET(ui->tv_script), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(ui->tv_script), 6);
    gtk_box_append(GTK_BOX(v), sc);

    // update highlighting when buffer changes
    g_signal_connect(ui->buf_script, "changed", G_CALLBACK(on_script_buffer_changed), ui);
    // initialize highlighting
    on_script_buffer_changed(ui->buf_script, ui);

    return v;
}

static GtkWidget* build_bottom_send(UIMain* ui) {
    GtkWidget* fr = gtk_frame_new("Send");
    GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(v, 6);
    gtk_widget_set_margin_bottom(v, 6);
    gtk_widget_set_margin_start(v, 6);
    gtk_widget_set_margin_end(v, 6);
    gtk_frame_set_child(GTK_FRAME(fr), v);

    // mode switch
    GtkWidget* h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui->tg_send_mode_manual = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("Manual"));
    ui->tg_send_mode_script = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("Script"));
    gtk_toggle_button_set_active(ui->tg_send_mode_manual, TRUE);

    gtk_box_append(GTK_BOX(h), GTK_WIDGET(ui->tg_send_mode_manual));
    gtk_box_append(GTK_BOX(h), GTK_WIDGET(ui->tg_send_mode_script));

    GtkWidget* hint = gtk_label_new("Hint: script mode can show next-send countdown/length after VM integration.");
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_append(GTK_BOX(h), hint);

    gtk_box_append(GTK_BOX(v), h);

    // editor + send button
    GtkWidget* h2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    ui->tv_send = GTK_TEXT_VIEW(gtk_text_view_new());
    ui->buf_send = gtk_text_view_get_buffer(ui->tv_send);
    gtk_text_view_set_monospace(ui->tv_send, TRUE);
    gtk_text_buffer_set_text(ui->buf_send, "57 65 6C 63 6F 6D 65 20 74 6F 20 4E 65 74 41 73 73 69 73 74", -1);

    GtkWidget* sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), GTK_WIDGET(ui->tv_send));
    gtk_widget_set_hexpand(sc, TRUE);
    gtk_widget_set_vexpand(sc, TRUE);

    ui->btn_send = GTK_BUTTON(gtk_button_new_with_label("Send"));
    g_signal_connect(ui->btn_send, "clicked", G_CALLBACK(on_send_clicked), ui);

    gtk_box_append(GTK_BOX(h2), sc);
    gtk_box_append(GTK_BOX(h2), GTK_WIDGET(ui->btn_send));

    gtk_box_append(GTK_BOX(v), h2);
    return fr;
}

static GtkWidget* build_statusbar(UIMain* ui) {
    (void)ui;
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(bar, 4);
    gtk_widget_set_margin_bottom(bar, 4);
    gtk_widget_set_margin_start(bar, 8);
    gtk_widget_set_margin_end(bar, 8);

    GtkWidget* lb = gtk_label_new("RX: 0    TX: 0    Script: Stopped");
    gtk_label_set_xalign(GTK_LABEL(lb), 0.0f);
    gtk_widget_set_hexpand(lb, TRUE);

    gtk_box_append(GTK_BOX(bar), lb);
    return bar;
}

UIMain* ui_main_new(GtkApplication* app, const BackendAPI* api, void* api_user) {
    UIMain* ui = g_new0(UIMain, 1);
    ui->app = app;
    ui->api = api;
    ui->api_user = api_user;

    ui->win = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(ui->win, "NetAssist - Script Ready (GTK4)");
    gtk_window_set_default_size(ui->win, 1100, 720);

    // root: vertical
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(ui->win, root);

    // main: horizontal paned
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);

    GtkWidget* left = build_left_panel(ui);
    gtk_widget_set_size_request(left, 280, -1);
    gtk_paned_set_start_child(GTK_PANED(paned), left);

    // right: vertical split (top notebook + bottom send)
    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(right, 6);
    gtk_widget_set_margin_bottom(right, 6);
    gtk_widget_set_margin_start(right, 6);
    gtk_widget_set_margin_end(right, 6);
    gtk_paned_set_end_child(GTK_PANED(paned), right);

    GtkWidget* nb = gtk_notebook_new();
    gtk_widget_set_vexpand(nb, TRUE);
    gtk_widget_set_hexpand(nb, TRUE);

    GtkWidget* tab_log = build_log_tab(ui);
    GtkWidget* tab_script = build_script_tab(ui);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), tab_log, gtk_label_new("Log"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), tab_script, gtk_label_new("Script"));

    // Add a visible outline around the tabs by wrapping the notebook in a frame
    GtkWidget* nb_frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(nb_frame), nb);
    gtk_widget_set_vexpand(nb_frame, TRUE);
    gtk_widget_set_hexpand(nb_frame, TRUE);
    gtk_widget_set_margin_top(nb_frame, 2);
    gtk_widget_set_margin_bottom(nb_frame, 2);
    gtk_widget_set_margin_start(nb_frame, 2);
    gtk_widget_set_margin_end(nb_frame, 2);

    gtk_box_append(GTK_BOX(right), nb_frame);
    gtk_box_append(GTK_BOX(right), build_bottom_send(ui));
    gtk_box_append(GTK_BOX(root), build_statusbar(ui));

    // initial log
    append_text(ui->buf_log, "[UI] ready. Use left panel to apply config. Use Script tab to Run.");

    gtk_window_present(ui->win);
    return ui;
}

void ui_main_free(UIMain* ui) {
    if (!ui) return;
    // widgets managed by GTK
    g_free(ui);
}

GtkWindow* ui_main_window(UIMain* ui) {
    return ui ? ui->win : NULL;
}

// controller -> UI
void ui_main_log_append(void* ui_user, const char* line) {
    UIMain* ui = (UIMain*)ui_user;
    if (!ui || !ui->buf_log || !line) return;
    append_text(ui->buf_log, line);
}

void ui_main_packet_append(void* ui_user, const uint8_t* data, size_t len) {
    UIMain* ui = (UIMain*)ui_user;
    if (!ui || !ui->buf_pkt || !data || len == 0) return;
    append_hexdump(ui->buf_pkt, data, len);
}

void ui_main_set_script_state(void* ui_user, ScriptState st, const char* detail) {
    UIMain* ui = (UIMain*)ui_user;
    if (!ui || !ui->lb_script_state) return;

    const char* s = "Stopped";
    if (st == SCRIPT_RUNNING) s = "Running";
    else if (st == SCRIPT_PAUSED) s = "Paused";
    else if (st == SCRIPT_ERROR) s = "Error";

    char buf[256];
    snprintf(buf, sizeof(buf), "Script: %s%s%s", s, detail ? " - " : "", detail ? detail : "");
    gtk_label_set_text(ui->lb_script_state, buf);
}
