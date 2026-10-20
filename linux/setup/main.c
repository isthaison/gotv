#include "../../engine/engine.h"
#include "data.h"
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Existing desktop integration commands; report launch failures. */
static int run_shell(const char *command) {
    int status = system(command);
    if (status != 0) g_warning("Desktop command failed (status %d)", status);
    return status;
}

// --- Tray indicator (một phần của ứng dụng duy nhất, chạy với cờ --tray) ---
static AppIndicator *tray_indicator;
static GtkWidget *tray_item_telex;
static GtkWidget *tray_item_vni;

static char* tray_current_method(){
    char *path = gtv_user_path(g_get_user_config_dir(), "config");
    GKeyFile *kf = g_key_file_new();
    char *method = NULL;
    if(g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)){
        method = g_key_file_get_string(kf, "input", "method", NULL);
    }
    g_key_file_free(kf);
    g_free(path);
    if(!method) method = g_strdup("telex");
    return method;
}

static void tray_update_config_method(const char *method){
    char *path = gtv_user_path(g_get_user_config_dir(), "config");
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
    // Giữ nguyên các key khác ([ai]...), chỉ đổi input/method
    g_key_file_set_string(kf, "input", "method", method);
    if(!g_key_file_has_key(kf, "input", "modern", NULL))
        g_key_file_set_string(kf, "input", "modern", "true");
    if(!g_key_file_has_key(kf, "input", "spellcheck", NULL))
        g_key_file_set_string(kf, "input", "spellcheck", "true");
    gsize len = 0;
    gchar *data = g_key_file_to_data(kf, &len, NULL);
    if(data) g_file_set_contents(path, data, (gssize)len, NULL);
    g_free(data);
    g_key_file_free(kf);
    g_free(path);
}

static void tray_update_indicator_label(gboolean is_telex){
    if(!tray_indicator) return;
    // Hiển thị kiểu gõ ngay trên statusbar bên cạnh icon
    app_indicator_set_label(tray_indicator, is_telex ? "Telex" : "VNI", is_telex ? "Telex" : "VNI");
    app_indicator_set_title(tray_indicator, is_telex ? "GoTV — Telex" : "GoTV — VNI");
    const char *mode_icon = is_telex ? "gotv-telex" : "gotv-vni";
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    if(!icon_theme || !gtk_icon_theme_has_icon(icon_theme, mode_icon))
        mode_icon = "gotv";
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    app_indicator_set_icon_full(tray_indicator, mode_icon,
        is_telex ? "GoTV — Telex" : "GoTV — VNI");
    G_GNUC_END_IGNORE_DEPRECATIONS
}

static void tray_refresh_checks(){
    char *m = tray_current_method();
    gboolean is_telex = (g_strcmp0(m, "vni") != 0 && g_strcmp0(m, "VNI") != 0);
    // block signals while updating to avoid recursion
    g_signal_handlers_block_by_func(tray_item_telex, NULL, NULL);
    g_signal_handlers_block_by_func(tray_item_vni, NULL, NULL);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(is_telex ? tray_item_telex : tray_item_vni), TRUE);
    g_signal_handlers_unblock_by_func(tray_item_telex, NULL, NULL);
    g_signal_handlers_unblock_by_func(tray_item_vni, NULL, NULL);
    tray_update_indicator_label(is_telex);
    g_free(m);
}

static gboolean tray_check_config_timer(gpointer data){
    (void)data;
    char *path = gtv_user_path(g_get_user_config_dir(), "config");
    struct stat st;
    static time_t tray_last_mtime = 0;
    if(stat(path, &st) == 0){
        if(tray_last_mtime != 0 && st.st_mtime != tray_last_mtime){
            tray_refresh_checks();
        }
        tray_last_mtime = st.st_mtime;
    }
    g_free(path);
    return G_SOURCE_CONTINUE;
}

static void tray_on_activate_setup(GtkMenuItem *item, gpointer data){
    (void)item; (void)data;
    gchar *self = g_file_read_link("/proc/self/exe", NULL);
    if(self){
        gchar *setup_argv[] = {self, NULL};
        GError *err = NULL;
        if(g_spawn_async(NULL, setup_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)){
            g_free(self);
            return;
        }
        if(err){ g_warning("Failed to spawn setup: %s", err->message); g_error_free(err); }
        g_free(self);
    }
    run_shell("/usr/bin/gotv 2>/dev/null || gotv 2>/dev/null || /usr/libexec/ibus-setup-gotv 2>/dev/null &");
}
static void tray_on_activate_telex(GtkMenuItem *item, gpointer data){
    (void)data;
    if(!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) return;
    tray_update_config_method("telex");
    // Chỉ đổi method trong config (engine chung tự reload, engine ghim giữ
    // nguyên); KHÔNG đụng input-sources để khỏi xóa nguồn Telex/VNI của user.
    run_shell("notify-send 'GoTV' 'Đã chuyển sang Telex (s f r x j)' 2>/dev/null &");
    tray_refresh_checks();
}
static void tray_on_activate_vni(GtkMenuItem *item, gpointer data){
    (void)data;
    if(!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) return;
    tray_update_config_method("vni");
    run_shell("notify-send 'GoTV' 'Đã chuyển sang VNI (1-5, 6-9)' 2>/dev/null &");
    tray_refresh_checks();
}
static void tray_toggle_method(void){
    char *m = tray_current_method();
    gboolean is_telex = (g_strcmp0(m, "vni") != 0 && g_strcmp0(m, "VNI") != 0);
    g_free(m);
    if (is_telex) {
        tray_update_config_method("vni");
        run_shell("notify-send 'GoTV' 'Đã chuyển sang VNI (1-5, 6-9)' 2>/dev/null &");
    } else {
        tray_update_config_method("telex");
        run_shell("notify-send 'GoTV' 'Đã chuyển sang Telex (s f r x j)' 2>/dev/null &");
    }
    tray_refresh_checks();
}
static void tray_on_activate_toggle(GtkMenuItem *item, gpointer data){
    (void)item; (void)data;
    tray_toggle_method();
}
static void tray_on_activate_quit(GtkMenuItem *item, gpointer data){
    (void)item; (void)data;
    gtk_main_quit();
}

// --- Tự cập nhật qua GitHub releases (dùng chung engine/update.c) ---
typedef struct {
    gchar *tag;
    gchar *url;
    gchar *note;
    gboolean installed;
    gboolean manual;
    GtvUpdateStatus status;
} TrayUpdate;

/* Run pkexec via terminal emulator when /dev/tty is unavailable (GUI launch). */
static gboolean run_pkexec_argv(gchar **argv, gint *out_status, gchar **out_err, gchar **errmsg){
    GError *error = NULL;
    FILE *tty = fopen("/dev/tty", "r");
    if(tty){
        fclose(tty);
        if(!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                         NULL, NULL, out_err, NULL, out_status, &error)){
            *errmsg = g_strdup_printf("Không chạy được pkexec: %s",
                                      error ? error->message : "lỗi không rõ");
            g_clear_error(&error);
            return FALSE;
        }
        return TRUE;
    }
    const char *terms[] = {"x-terminal-emulator","gnome-terminal","konsole","xfce4-terminal","xterm",NULL};
    gchar *term = NULL;
    for(int i = 0; terms[i]; i++){
        if(g_find_program_in_path(terms[i])){ term = g_strdup(terms[i]); break; }
    }
    if(!term){ *errmsg = g_strdup("Không tìm thấy terminal emulator."); return FALSE; }
    GString *cmd = g_string_new("");
    for(int i = 0; argv[i]; i++){ if(i) g_string_append_c(cmd,' '); g_string_append(cmd, argv[i]); }
    gchar *wrapped[] = {term, "-e", cmd->str, NULL};
    if(!g_spawn_sync(NULL, wrapped, NULL, G_SPAWN_SEARCH_PATH,
                     NULL, NULL, out_err, NULL, out_status, &error)){
        *errmsg = g_strdup_printf("Không chạy được terminal: %s",
                                  error ? error->message : "lỗi không rõ");
        g_clear_error(&error);
        g_string_free(cmd, TRUE); g_free(term);
        return FALSE;
    }
    g_string_free(cmd, TRUE); g_free(term);
    return TRUE;
}

static void tray_update_free(TrayUpdate *u){
    if(!u) return;
    g_free(u->tag); g_free(u->url); g_free(u->note); g_free(u);
}
/* Phiên bản đã cài (không revision .deb) để so với tag GitHub. */
static gchar *tray_update_current_version(void){
    gchar *contents = NULL, *ver = NULL;
    if(g_file_get_contents("/usr/share/ibus/component/gotv.xml", &contents, NULL, NULL) && contents){
        gchar *p = strstr(contents, "<version>");
        if(p){
            p += strlen("<version>");
            gchar *e = strstr(p, "</version>");
            if(e) ver = g_strndup(p, e - p);
        }
    }
    g_free(contents);
    if(ver) g_strstrip(ver);
    if(ver && !*ver){ g_free(ver); ver = NULL; }
    return ver;
}
static gboolean deb_asset_match(const gchar *asset, const gchar *tagver, gpointer data){
    const gchar *arch = data;
    gchar *prefix = g_strdup_printf("gotv_%s", tagver);
    gboolean ok = g_str_has_prefix(asset, prefix) && g_str_has_suffix(asset, ".deb")
        && (!arch || !*arch || strstr(asset, arch) != NULL);
    g_free(prefix);
    return ok;
}
static gboolean tray_update_installed_cb(gpointer data){
    TrayUpdate *u = data;
    GtkWidget *d;
    if(u->installed){
        run_shell("ibus restart >/dev/null 2>&1 &");
        gchar *dest = g_build_filename(g_get_tmp_dir(), "gotv-update.deb", NULL);
        g_remove(dest);
        g_free(dest);
        d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Đã cài %s. IBus đang khởi động lại.", u->tag ? u->tag : "");
    }else{
        d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Cài đặt thất bại.%s%s", u->note ? "\n" : "", u->note ? u->note : "");
    }
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    tray_update_free(u);
    return G_SOURCE_REMOVE;
}
static gpointer tray_update_install_thread(gpointer data){
    TrayUpdate *u = data;
    u->installed = FALSE;
    g_free(u->note); u->note = NULL;
    gchar *dest = g_build_filename(g_get_tmp_dir(), "gotv-update.deb", NULL);
    if(!(u->url && gtv_update_download(u->url, dest))){
        u->note = g_strdup("Không tải được file .deb (cần mạng và curl).");
    }else{
        gchar *argv[] = {"pkexec", "dpkg", "-i", dest, NULL};
        gchar *out = NULL, *err = NULL;
        gint status = -1;
        gchar *errmsg = NULL;
        if(!run_pkexec_argv(argv, &status, &err, &errmsg)){
            u->note = g_strdup(errmsg);
            g_free(errmsg);
        }else if(status != 0){
            gchar *detail = (err && *err) ? g_strstrip(g_strdup(err)) : NULL;
            u->note = g_strdup_printf("dpkg báo lỗi (mã %d)%s%s",
                                      status, detail ? ": " : "", detail ? detail : "");
            g_free(detail);
        }else{
            u->installed = TRUE;
        }
        g_free(out); g_free(err);
    }
    g_free(dest);
    g_idle_add(tray_update_installed_cb, u);
    return NULL;
}
static gboolean tray_update_checked_cb(gpointer data){
    TrayUpdate *u = data;
    if(u->status == GTV_UPDATE_AVAILABLE && u->tag && u->url){
        GtkWidget *d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE, "Có bản mới %s. Tải và cài đặt ngay?", u->tag);
        gtk_dialog_add_button(GTK_DIALOG(d), "Để sau", GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(d), "Tải và cài đặt", GTK_RESPONSE_OK);
        gboolean install = (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK);
        gtk_widget_destroy(d);
        if(install){
            GThread *th = g_thread_new("gtv-update-install", tray_update_install_thread, u);
            if(th){ g_thread_unref(th); return G_SOURCE_REMOVE; }
        }
        tray_update_free(u);
    }else if(u->manual){
        GtkWidget *d;
        if(u->status == GTV_UPDATE_CURRENT){
            d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK, "Đang dùng bản mới nhất.");
        }else{
            d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK, "Không kiểm tra được bản mới (cần mạng và curl).");
        }
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        tray_update_free(u);
    }else{
        if(u->status == GTV_UPDATE_AVAILABLE && u->tag){
            gchar *cmd = g_strdup_printf(
                "notify-send 'GoTV' 'Đã có bản mới %s — mở menu tray, chọn Kiểm tra cập nhật để cài.' 2>/dev/null &",
                u->tag);
            run_shell(cmd);
            g_free(cmd);
        }
        tray_update_free(u);
    }
    return G_SOURCE_REMOVE;
}
static gpointer tray_update_check_thread(gpointer data){
    gboolean manual = GPOINTER_TO_INT(data);
    gchar *current = tray_update_current_version();
    gchar *arch = NULL;
    if(!g_spawn_command_line_sync("dpkg --print-architecture", &arch, NULL, NULL, NULL) || !arch){
        g_free(arch); arch = NULL;
    }else{
        g_strstrip(arch);
        if(!*arch){ g_free(arch); arch = NULL; }
    }
    TrayUpdate *u = g_new0(TrayUpdate, 1);
    u->manual = manual;
    if(current) u->status = gtv_update_check_full(NULL, current, deb_asset_match, arch, &u->tag, &u->url);
    else u->status = GTV_UPDATE_ERROR;
    g_free(current); g_free(arch);
    g_idle_add(tray_update_checked_cb, u);
    /* Dictionary refresh rides the same throttled check (tray + setup). */
    gtv_dict_update_check(NULL);
    return NULL;
}
static void tray_update_start(gboolean manual){
    GThread *th = g_thread_new("gtv-update-check", tray_update_check_thread, GINT_TO_POINTER(manual));
    if(th) g_thread_unref(th);
}
static void tray_on_activate_update(GtkMenuItem *item, gpointer data){
    (void)item; (void)data;
    tray_update_start(TRUE);
}

int tray_run(int argc, char *argv[]){
    g_set_prgname("gotv");
    g_set_application_name("GoTV");
    gtk_init(&argc, &argv);

    // Đảm bảo theme tìm thấy icon "gotv" kể cả khi hicolor cache chưa cập nhật
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    if(theme){
        gtk_icon_theme_append_search_path(theme, "/usr/share/gotv/icons");
        gtk_icon_theme_append_search_path(theme, "/usr/share/icons/hicolor");
    }

    // AppIndicator — hiện trên top bar GNOME qua extension ubuntu-appindicators
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    tray_indicator = app_indicator_new("gotv", "gotv",
                                  APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    G_GNUC_END_IGNORE_DEPRECATIONS
    app_indicator_set_status(tray_indicator, APP_INDICATOR_STATUS_ACTIVE);
    // Trỏ trực tiếp vào thư mục chứa icon để Shell resolve đúng icon
    app_indicator_set_icon_theme_path(tray_indicator, "/usr/share/gotv/icons");
    app_indicator_set_icon_full(tray_indicator, "gotv", "GoTV");
    app_indicator_set_title(tray_indicator, "GoTV — Telex/VNI");

    GtkWidget *menu = gtk_menu_new();
    GSList *group = NULL;
    tray_item_telex = gtk_radio_menu_item_new_with_label(group, "Telex (s f r x j, w)");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(tray_item_telex));
    tray_item_vni = gtk_radio_menu_item_new_with_label(group, "VNI (1-5, 6-9)");

    GtkWidget *tray_item_toggle = gtk_menu_item_new_with_label("Chuyển kiểu gõ (Telex ↔ VNI)");
    g_signal_connect(tray_item_toggle, "activate", G_CALLBACK(tray_on_activate_toggle), NULL);

    GtkWidget *tray_item_setup = gtk_menu_item_new_with_label("Mở GoTV Setup...");
    GtkWidget *tray_item_update = gtk_menu_item_new_with_label("Kiểm tra cập nhật...");
    GtkWidget *tray_item_quit = gtk_menu_item_new_with_label("Thoát");
    g_signal_connect(tray_item_telex, "toggled", G_CALLBACK(tray_on_activate_telex), NULL);
    g_signal_connect(tray_item_vni, "toggled", G_CALLBACK(tray_on_activate_vni), NULL);
    g_signal_connect(tray_item_setup, "activate", G_CALLBACK(tray_on_activate_setup), NULL);
    g_signal_connect(tray_item_update, "activate", G_CALLBACK(tray_on_activate_update), NULL);
    g_signal_connect(tray_item_quit, "activate", G_CALLBACK(tray_on_activate_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_toggle);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_telex);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_vni);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_setup);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_update);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_quit);
    gtk_widget_show_all(menu);

    app_indicator_set_menu(tray_indicator, GTK_MENU(menu));
    app_indicator_set_secondary_activate_target(tray_indicator, tray_item_toggle);
    tray_refresh_checks();

    if(gtv_update_should_autocheck()){
        gtv_update_mark_checked();
        tray_update_start(FALSE);
    }

    g_timeout_add(1000, (GSourceFunc)tray_check_config_timer, NULL);

    gtk_main();
    return 0;
}
// --- Hết phần tray ---

static GtkWidget *rb_telex, *rb_vni, *cb_modern, *cb_spell;
static GtkWidget *lbl_preview;
static GtkWidget *cb_suggest, *lbl_dict_ver, *lbl_dict_status, *btn_dict_update;
static char *config_path;

static void update_preview(){
    gboolean modern = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_modern));
    const char *txt = modern ?
        "Preview modern (hoà): <tt>hoaf→hoà, hoas→hoá, huow→hươu, thoas→thoá</tt>" :
        "Preview traditional (hòa): <tt>hoaf→hòa, hoas→hóa, huow→hươu, thoas→thóa</tt>";
    gtk_label_set_markup(GTK_LABEL(lbl_preview), txt);
}
static void on_modern_toggled(GtkWidget *w, gpointer data){
    update_preview();
}

static void dict_ui_refresh(void){
    if(!lbl_dict_ver) return;
    guint n = gtv_dict_count();
    const gchar *tag = gtv_dict_tag();
    gchar *ver = (n > 0)
        ? g_strdup_printf("Từ điển %s · %u từ", (tag && *tag) ? tag : "cài sẵn", n)
        : g_strdup("Chưa có từ điển");
    gtk_label_set_text(GTK_LABEL(lbl_dict_ver), ver);
    g_free(ver);
}
static gboolean dict_update_done(gpointer data){
    gboolean updated = GPOINTER_TO_INT(data);
    if(btn_dict_update) gtk_widget_set_sensitive(btn_dict_update, TRUE);
    dict_ui_refresh();
    if(lbl_dict_status){
        if(updated){
            const gchar *tag = gtv_dict_tag();
            gchar *msg = g_strdup_printf("Đã cập nhật từ điển %s (%u từ).",
                (tag && *tag) ? tag : "", gtv_dict_count());
            gtk_label_set_text(GTK_LABEL(lbl_dict_status), msg);
            g_free(msg);
        } else {
            gtk_label_set_text(GTK_LABEL(lbl_dict_status),
                "Từ điển đã mới nhất (hoặc không có mạng).");
        }
    }
    return G_SOURCE_REMOVE;
}
static gpointer dict_update_worker(gpointer data){
    (void)data;
    gboolean updated = gtv_dict_update_check(NULL);
    g_idle_add(dict_update_done, GINT_TO_POINTER(updated));
    return NULL;
}
static void on_dict_update(GtkWidget *w, gpointer data){
    (void)w; (void)data;
    if(btn_dict_update) gtk_widget_set_sensitive(btn_dict_update, FALSE);
    if(lbl_dict_status)
        gtk_label_set_text(GTK_LABEL(lbl_dict_status),
            "Đang kiểm tra từ điển mới trên GitHub...");
    GThread *th = g_thread_new("gtv-dict-update", dict_update_worker, NULL);
    if(th) g_thread_unref(th);
    else if(btn_dict_update) gtk_widget_set_sensitive(btn_dict_update, TRUE);
}

static void on_suggest_toggled(GtkWidget *w, gpointer data){
    (void)w; (void)data;
}

static void on_save(GtkWidget *w, gpointer data){
    GtkWindow *win = GTK_WINDOW(data);
    const char *method = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rb_vni)) ? "vni" : "telex";
    const char *modern = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_modern)) ? "true" : "false";
    const char *spell = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_spell)) ? "true" : "false";
    const char *suggest = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_suggest)) ? "true" : "false";
    GtvConfig config = {.mode = strcmp(method,"vni") == 0 ? GTV_VNI : GTV_TELEX,
        .modern = strcmp(modern,"true") == 0, .spellcheck = strcmp(spell,"true") == 0,
        .suggest_enabled = strcmp(suggest,"true") == 0};
    gchar *directory = g_path_get_dirname(config_path);
    GError *error = NULL;
    gboolean saved = gtv_config_save(&config,directory,&error);
    g_free(directory);
    if(!saved){
        GtkWidget *failure = gtk_message_dialog_new(win,GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,
            "Không lưu được cấu hình: %s",error->message);
        gtk_dialog_run(GTK_DIALOG(failure));gtk_widget_destroy(failure);g_error_free(error);
        return;
    }
    // Restart ibus
    run_shell("ibus restart 2>/dev/null &");
    GtkWidget *dlg = gtk_message_dialog_new(win, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "Đã lưu %s, modern=%s, gợi ý từ điển=%s. Đã restart ibus.", method, modern, suggest);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    gtk_main_quit();
}
static void on_cancel(GtkWidget *w, gpointer data){
    gtk_main_quit();
}
static void on_data(GtkWidget *w, gpointer data){
    (void)w;
    data_show(GTK_WINDOW(data));
}
int setup_ui(int argc, char *argv[], const char *cur_method, const char *cur_modern, const char *cur_spell, const char *cur_suggest, const char *cfg){
    g_set_prgname("gotv");
    g_set_application_name("GoTV");
    config_path = strdup(cfg);
    gtk_init(&argc, &argv);

    GtkIconTheme *theme = gtk_icon_theme_get_default();
    if(theme){
        gtk_icon_theme_append_search_path(theme, "/usr/share/gotv/icons");
        gtk_icon_theme_append_search_path(theme, "/usr/share/icons/hicolor");
    }
    gtk_window_set_default_icon_name("gotv");

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "GoTV Setup — github.com/isthaison/gotv");
    gtk_window_set_default_size(GTK_WINDOW(win), 540, 620);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(win), "gotv");
    gtk_window_set_icon_from_file(GTK_WINDOW(win), "/usr/share/gotv/icons/gotv.png", NULL);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_container_add(GTK_CONTAINER(win), vbox);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>GoTV</b> — Gõ Telex/VNI thuần hệ thống\n<span size='small'>Chỉ dùng lib hệ thống (Go + gtk+-3.0), không lib ngoài</span>");
    gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *f1 = gtk_frame_new("Kiểu gõ");
    gtk_box_pack_start(GTK_BOX(vbox), f1, FALSE, FALSE, 0);
    GtkWidget *box1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box1), 8);
    gtk_container_add(GTK_CONTAINER(f1), box1);
    rb_telex = gtk_radio_button_new_with_label(NULL, "Telex  — s f r x j, w z, aa ee oo aw ow uw dd, uow→ươ");
    rb_vni = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rb_telex), "VNI     — 1 2 3 4 5, 6 7 8 9 0, a6→â, a8→ă, o7→ơ");
    if(strcmp(cur_method,"vni")==0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb_vni), TRUE);
    else gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb_telex), TRUE);
    gtk_box_pack_start(GTK_BOX(box1), rb_telex, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box1), rb_vni, FALSE, FALSE, 0);

    GtkWidget *f2 = gtk_frame_new("Tùy chọn");
    gtk_box_pack_start(GTK_BOX(vbox), f2, FALSE, FALSE, 0);
    GtkWidget *box2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box2), 8);
    gtk_container_add(GTK_CONTAINER(f2), box2);
    cb_modern = gtk_check_button_new_with_label("Kiểu mới (modern) — hoà thay vì hòa cho oa/oe/uy (Bộ GD 2018)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_modern), strcmp(cur_modern,"true")==0);
    g_signal_connect(cb_modern, "toggled", G_CALLBACK(on_modern_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(box2), cb_modern, FALSE, FALSE, 0);
    lbl_preview = gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_preview), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl_preview), 0.0);
    update_preview();
    gtk_box_pack_start(GTK_BOX(box2), lbl_preview, FALSE, FALSE, 0);
    cb_spell = gtk_check_button_new_with_label("Kiểm tra chính tả (chỉ gợi ý, không chặn)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_spell), strcmp(cur_spell,"true")==0);
    gtk_box_pack_start(GTK_BOX(box2), cb_spell, FALSE, FALSE, 0);
    GtkWidget *w2 = gtk_check_button_new_with_label("Cho phép w đơn -> ư (gõ w đầu từ ra ư)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w2), TRUE);
    gtk_widget_set_sensitive(w2, FALSE);
    gtk_box_pack_start(GTK_BOX(box2), w2, FALSE, FALSE, 0);

    // Dictionary frame (offline suggestions, GitHub-updated word list)
    GtkWidget *f_dict = gtk_frame_new("Gợi ý từ điển (offline)");
    gtk_box_pack_start(GTK_BOX(vbox), f_dict, FALSE, FALSE, 0);
    GtkWidget *box_dict = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box_dict), 8);
    gtk_container_add(GTK_CONTAINER(f_dict), box_dict);
    cb_suggest = gtk_check_button_new_with_label("Bật gợi ý từ thông minh");
    gtk_box_pack_start(GTK_BOX(box_dict), cb_suggest, FALSE, FALSE, 0);
    g_signal_connect(cb_suggest, "toggled", G_CALLBACK(on_suggest_toggled), NULL);
    lbl_dict_ver = gtk_label_new("Từ điển...");
    gtk_label_set_xalign(GTK_LABEL(lbl_dict_ver), 0.0);
    gtk_box_pack_start(GTK_BOX(box_dict), lbl_dict_ver, FALSE, FALSE, 0);
    btn_dict_update = gtk_button_new_with_label("Cập nhật từ điển từ GitHub...");
    g_signal_connect(btn_dict_update, "clicked", G_CALLBACK(on_dict_update), NULL);
    gtk_box_pack_start(GTK_BOX(box_dict), btn_dict_update, FALSE, FALSE, 0);
    lbl_dict_status = gtk_label_new("Gợi ý offline 100%, không cần mạng.");
    gtk_label_set_line_wrap(GTK_LABEL(lbl_dict_status), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl_dict_status), 0.0);
    gtk_box_pack_start(GTK_BOX(box_dict), lbl_dict_status, FALSE, FALSE, 0);
    // Khởi tạo gợi ý từ config
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_suggest), strcmp(cur_suggest,"true")==0);
    dict_ui_refresh();

    GtkWidget *info = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info), "<span size='small'>Cấu hình lưu tại <tt>~/.config/gotv/config</tt>\nGõ lại ký tự để xóa dấu: <tt>as-&gt;á, á s-&gt;a</tt>, <tt>uw-&gt;ư, ư w-&gt;u</tt></span>");
    gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 0);

    GtkWidget *bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(bbox, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);
    GtkWidget *btn_data = gtk_button_new_with_label("Dữ liệu...");
    g_signal_connect(btn_data, "clicked", G_CALLBACK(on_data), win);
    gtk_box_pack_start(GTK_BOX(bbox), btn_data, FALSE, FALSE, 0);
    GtkWidget *btn_cancel = gtk_button_new_with_label("Hủy");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_cancel), NULL);
    gtk_box_pack_start(GTK_BOX(bbox), btn_cancel, FALSE, FALSE, 0);
    GtkWidget *btn_ok = gtk_button_new_with_label("Lưu & Đóng");
    GtkStyleContext *ctx = gtk_widget_get_style_context(btn_ok);
    gtk_style_context_add_class(ctx, "suggested-action");
    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_save), win);
    gtk_box_pack_start(GTK_BOX(bbox), btn_ok, FALSE, FALSE, 0);

    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

static void ensure_input_source(void) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) return;
    GSettingsSchema *schema = g_settings_schema_source_lookup(source, "org.gnome.desktop.input-sources", TRUE);
    if (!schema) return;
    GSettings *settings = g_settings_new_full(schema, NULL, NULL);
    GVariant *current = g_settings_get_value(settings, "sources");
    GVariantIter iter;
    const gchar *kind, *name;
    gboolean found = FALSE, changed = FALSE;
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ss)"));
    g_variant_iter_init(&iter, current);
    while (g_variant_iter_next(&iter, "(&s&s)", &kind, &name)) {
        if (!strcmp(kind, "ibus") && !strcmp(name, "gotiengviet")) {
            changed = TRUE;
            if (!found) {
                g_variant_builder_add(&builder, "(ss)", "ibus", "gotv");
                found = TRUE;
            }
            continue;
        }
        if (!strcmp(kind, "ibus") && !strcmp(name, "gotv")) {
            if (found) { changed = TRUE; continue; }
            found = TRUE;
        }
        g_variant_builder_add(&builder, "(ss)", kind, name);
    }
    if (!found) {
        g_variant_builder_add(&builder, "(ss)", "ibus", "gotv");
        changed = TRUE;
    }
    if (changed) {
        g_settings_set_value(settings, "sources", g_variant_builder_end(&builder));
        g_settings_sync();
        g_spawn_command_line_async("ibus engine gotv", NULL);
    } else g_variant_builder_clear(&builder);
    g_variant_unref(current);
    g_object_unref(settings);
    g_settings_schema_unref(schema);
}

static void cli_choice(const gchar *label, gchar *buffer, gsize size) {
    g_print("%s: ", label);
    if (!fgets(buffer, size, stdin)) buffer[0] = '\0';
    g_strstrip(buffer);
}

static int setup_cli(GtvConfig *config, const gchar *directory) {
    gchar input[256];
    g_print("GoTV Setup (C) — %s, modern=%s, gợi ý từ điển=%s\nEnter để giữ giá trị hiện tại.\n",
        config->mode == GTV_VNI ? "vni" : "telex", config->modern ? "true" : "false", config->suggest_enabled ? "true" : "false");
    cli_choice("Kiểu gõ [telex/vni]", input, sizeof(input));
    if (!g_ascii_strcasecmp(input, "vni")) config->mode = GTV_VNI;
    if (!g_ascii_strcasecmp(input, "telex")) config->mode = GTV_TELEX;
    cli_choice("Modern [true/false]", input, sizeof(input));
    if (!g_ascii_strcasecmp(input, "true")) config->modern = TRUE;
    if (!g_ascii_strcasecmp(input, "false")) config->modern = FALSE;
    cli_choice("Gợi ý từ điển [true/false]", input, sizeof(input));
    if (!g_ascii_strcasecmp(input, "true")) config->suggest_enabled = TRUE;
    if (!g_ascii_strcasecmp(input, "false")) config->suggest_enabled = FALSE;
    GError *error = NULL;
    if (!gtv_config_save(config, directory, &error)) {
        g_printerr("Không lưu được cấu hình: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    g_print("Đã lưu %s/config\n", directory);
    return 0;
}

int main(int argc, char **argv) {
    gboolean tray = FALSE, cli = FALSE;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tray")) tray = TRUE;
        else if (!strcmp(argv[i], "--cli")) cli = TRUE;
        else if (!strcmp(argv[i], "--help")) {
            g_print("GoTV Setup\n  --tray  Chạy indicator\n  --cli   Cấu hình qua terminal\n");
            return 0;
        } else { g_printerr("Tham số không hợp lệ: %s\n", argv[i]); return 1; }
    }
    gboolean graphical = g_getenv("DISPLAY") || g_getenv("WAYLAND_DISPLAY");
    if (tray) {
        if (!graphical) { g_printerr("Tray cần DISPLAY/WAYLAND_DISPLAY\n"); return 1; }
        gchar *lock_path = g_build_filename(g_get_user_runtime_dir(), "gotv-tray.lock", NULL);
        int lock_fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        g_free(lock_path);
        if (lock_fd < 0) { g_printerr("Không mở được khóa tray\n"); return 1; }
        if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) { close(lock_fd); return 0; }
        ensure_input_source();
        int result = tray_run(argc, argv);
        close(lock_fd);
        return result;
    }
    gchar *directory = gtv_user_path(g_get_user_config_dir(), NULL);
    GtvConfig config;
    gtv_config_load(&config, directory);
    int result;
    if (cli || !graphical) result = setup_cli(&config, directory);
    else {
        ensure_input_source();
        gchar *self = g_file_read_link("/proc/self/exe", NULL);
        if (self) {
            gchar *tray_argv[] = {self, "--tray", NULL};
            g_spawn_async(NULL, tray_argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
            g_free(self);
        }
        gchar *path = g_build_filename(directory, "config", NULL);
        result = setup_ui(argc, argv, config.mode == GTV_VNI ? "vni" : "telex",
            config.modern ? "true" : "false", config.spellcheck ? "true" : "false",
            config.suggest_enabled ? "true" : "false", path);
        g_free(path);
    }
    gtv_config_clear(&config);
    g_free(directory);
    return result;
}
