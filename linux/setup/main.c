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

static GtvUiLang tray_current_lang(void) {
    char *path = gtv_user_path(g_get_user_config_dir(), "config");
    GKeyFile *kf = g_key_file_new();
    GtvUiLang res = GTV_LANG_VI;
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        char *lang = g_key_file_get_string(kf, "general", "lang", NULL);
        if (!lang) lang = g_key_file_get_string(kf, "ui", "lang", NULL);
        if (lang) {
            g_strstrip(lang);
            if (!g_ascii_strcasecmp(lang, "en") || !g_ascii_strcasecmp(lang, "english")) res = GTV_LANG_EN;
            g_free(lang);
        }
    }
    g_key_file_free(kf);
    g_free(path);
    gtv_i18n_set_lang(res);
    return res;
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
        g_key_file_set_string(kf, "input", "spellcheck", "false");
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
    run_shell(gtv_i18n("notify-send 'GoTV' 'Đã chuyển sang Telex (s f r x j)' 2>/dev/null &",
                       "notify-send 'GoTV' 'Switched to Telex (s f r x j)' 2>/dev/null &"));
    tray_refresh_checks();
}
static void tray_on_activate_vni(GtkMenuItem *item, gpointer data){
    (void)data;
    if(!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) return;
    tray_update_config_method("vni");
    run_shell(gtv_i18n("notify-send 'GoTV' 'Đã chuyển sang VNI (1-5, 6-9)' 2>/dev/null &",
                       "notify-send 'GoTV' 'Switched to VNI (1-5, 6-9)' 2>/dev/null &"));
    tray_refresh_checks();
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
        gchar *ibus = g_find_program_in_path("ibus");
        if(ibus){
            run_shell("ibus restart >/dev/null 2>&1 &");
            g_free(ibus);
        }
        data_restart_fcitx5_if_active();
        gchar *dest = g_build_filename(g_get_tmp_dir(), "gotv-update.deb", NULL);
        g_remove(dest);
        g_free(dest);
        d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Đã cài %s. IBus/Fcitx5 đang khởi động lại nếu hoạt động.", u->tag ? u->tag : "");
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
static void tray_on_activate_data(GtkMenuItem *item, gpointer data){
    (void)item; (void)data;
    data_show(NULL);
}
static void tray_on_activate_update(GtkMenuItem *item, gpointer data){
    (void)item; (void)data;
    tray_update_start(TRUE);
}

static void show_startup_splash(gboolean is_light);
static GtkCssProvider *setup_css;
static void apply_linux_theme(gboolean is_light);

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

    (void)tray_current_lang();
    {
        gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
        GtvConfig cfg = {0};
        gtv_config_load(&cfg, dir);
        setup_css = gtk_css_provider_new();
        apply_linux_theme(cfg.ui_theme == GTV_THEME_LIGHT);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(setup_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(setup_css);
        show_startup_splash(cfg.ui_theme == GTV_THEME_LIGHT);
        gtv_config_clear(&cfg);
        g_free(dir);
    }

    GtkWidget *menu = gtk_menu_new();
    GSList *group = NULL;
    tray_item_telex = gtk_radio_menu_item_new_with_label(group, "Telex");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(tray_item_telex));
    tray_item_vni = gtk_radio_menu_item_new_with_label(group, "VNI");

    GtkWidget *tray_item_setup = gtk_menu_item_new_with_label(gtv_i18n("Cài đặt GoTV...", "GoTV Settings..."));
    GtkWidget *tray_item_data = gtk_menu_item_new_with_label(gtv_i18n("Gõ tắt & Emoji...", "Macros & Emoji..."));
    GtkWidget *tray_item_update = gtk_menu_item_new_with_label(gtv_i18n("Kiểm tra cập nhật...", "Check for updates..."));
    GtkWidget *tray_item_quit = gtk_menu_item_new_with_label(gtv_i18n("Thoát", "Quit"));
    g_signal_connect(tray_item_telex, "toggled", G_CALLBACK(tray_on_activate_telex), NULL);
    g_signal_connect(tray_item_vni, "toggled", G_CALLBACK(tray_on_activate_vni), NULL);
    g_signal_connect(tray_item_setup, "activate", G_CALLBACK(tray_on_activate_setup), NULL);
    g_signal_connect(tray_item_data, "activate", G_CALLBACK(tray_on_activate_data), NULL);
    g_signal_connect(tray_item_update, "activate", G_CALLBACK(tray_on_activate_update), NULL);
    g_signal_connect(tray_item_quit, "activate", G_CALLBACK(tray_on_activate_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_telex);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_vni);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_setup);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_update);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), tray_item_quit);
    gtk_widget_show_all(menu);

    app_indicator_set_menu(tray_indicator, GTK_MENU(menu));
    app_indicator_set_secondary_activate_target(tray_indicator, tray_item_setup);
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
static GtkWidget *sw_telex, *sw_vni, *sw_suggest_gen;
static gboolean ui_syncing = FALSE;
static GtkWidget *rb_lang_vi, *rb_lang_en;
static GtkWidget *rb_theme_dark, *rb_theme_light;
static GtkCssProvider *setup_css = NULL;
static GtkWidget *lbl_preview;
static GtkWidget *cb_suggest, *lbl_dict_ver, *lbl_dict_status, *btn_dict_update;
static GtkWidget *cb_dict_packs[8];
static GtkWidget *setup_stack;
static GtkWidget *setup_scroll;
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

static gchar *collect_selected_dicts(void){
    guint count = gtv_dict_catalog_count();
    GString *buf = g_string_new("");
    for(guint i = 0; i < count && i < G_N_ELEMENTS(cb_dict_packs); i++){
        if(!cb_dict_packs[i]) continue;
        if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_dict_packs[i]))){
            const GtvDictInfo *info = gtv_dict_catalog_get(i);
            if(info){
                if(buf->len > 0) g_string_append_c(buf, ',');
                g_string_append(buf, info->id);
            }
        }
    }
    if(buf->len == 0) g_string_assign(buf, "vi");
    return g_string_free(buf, FALSE);
}

static void dict_ui_refresh(void){
    if(!lbl_dict_ver) return;
    guint n = gtv_dict_count();
    const gchar *tag = gtv_dict_tag();
    const gchar *langs = gtv_dict_get_langs();
    GtvUiLang lang = gtv_i18n_get_lang();

    const gchar *disp_langs = langs;
    guint total = gtv_dict_catalog_count();
    gboolean all_selected = (total > 0);
    for (guint i = 0; i < total; i++) {
        const GtvDictInfo *info = gtv_dict_catalog_get(i);
        if (!info || !gtv_dict_lang_enabled(langs, info->id)) {
            all_selected = FALSE;
            break;
        }
    }
    if (all_selected) {
        disp_langs = gtv_i18n_s(lang, "tất cả", "all");
    } else if (!disp_langs || !*disp_langs) {
        disp_langs = "vi";
    }

    const gchar *bundled_str = gtv_i18n_s(lang, "cài sẵn", "bundled");
    gchar *ver = (n > 0)
        ? g_strdup_printf(gtv_i18n_s(lang, "Từ điển %s · %u từ (%s)", "Dictionary %s · %u words (%s)"),
                          (tag && *tag) ? tag : bundled_str, n, disp_langs)
        : g_strdup(gtv_i18n_s(lang, "Chưa có từ điển", "No dictionary available"));
    gtk_label_set_text(GTK_LABEL(lbl_dict_ver), ver);
    g_free(ver);
}
static void on_dict_pack_toggled(GtkWidget *w, gpointer data){
    (void)w; (void)data;
    gchar *sel = collect_selected_dicts();
    gtv_dict_set_langs(sel);
    g_free(sel);
    dict_ui_refresh();
}
static gboolean dict_update_done(gpointer data){
    gboolean updated = GPOINTER_TO_INT(data);
    if(btn_dict_update) gtk_widget_set_sensitive(btn_dict_update, TRUE);
    dict_ui_refresh();
    if(lbl_dict_status){
        if(updated){
            const gchar *tag = gtv_dict_tag();
            gchar *msg = g_strdup_printf("Đã tải/cập nhật từ điển %s (%u từ · %s).",
                (tag && *tag) ? tag : "", gtv_dict_count(), gtv_dict_get_langs());
            gtk_label_set_text(GTK_LABEL(lbl_dict_status), msg);
            g_free(msg);
        } else {
            gtk_label_set_text(GTK_LABEL(lbl_dict_status),
                "Các bộ từ điển đã chọn đều ở bản mới nhất (hoặc không có mạng).");
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
    gchar *sel = collect_selected_dicts();
    gtv_dict_set_langs(sel);
    g_free(sel);
    if(btn_dict_update) gtk_widget_set_sensitive(btn_dict_update, FALSE);
    if(lbl_dict_status)
        gtk_label_set_text(GTK_LABEL(lbl_dict_status),
            "Đang tải / kiểm tra các bộ từ điển đã chọn trên GitHub...");
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
        .suggest_enabled = strcmp(suggest,"true") == 0,
        .ui_lang = (rb_lang_en && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rb_lang_en))) ? GTV_LANG_EN : GTV_LANG_VI,
        .ui_theme = (rb_theme_light && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rb_theme_light))) ? GTV_THEME_LIGHT : GTV_THEME_DARK};
    gchar *sel = collect_selected_dicts();
    g_strlcpy(config.dicts, sel, sizeof(config.dicts));
    g_free(sel);
    gchar *directory = g_path_get_dirname(config_path);
    GError *error = NULL;
    gboolean saved = gtv_config_save(&config,directory,&error);
    g_free(directory);
    if(!saved){
        GtkWidget *failure = gtk_message_dialog_new(win,GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,
            "%s: %s", gtv_i18n("Không lưu được cấu hình", "Could not save configuration"), error ? error->message : "");
        gtk_dialog_run(GTK_DIALOG(failure));gtk_widget_destroy(failure);g_error_free(error);
        return;
    }
    // Fcitx5 picks up the saved settings from the config file on the next key.
    gchar *ibus = g_find_program_in_path("ibus");
    if(ibus){
        run_shell("ibus restart 2>/dev/null &");
        g_free(ibus);
    }
    GtkWidget *dlg = gtk_message_dialog_new(win, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        gtv_i18n("Đã lưu %s, modern=%s, gợi ý từ điển=%s (%s). IBus được restart nếu có; Fcitx5 nhận cấu hình ở lần gõ tiếp theo.",
                 "Saved %s, modern=%s, suggestions=%s (%s). IBus is restarted if installed; Fcitx5 picks up the settings on the next key."),
        method, modern, suggest, config.dicts);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    gtk_main_quit();
}
static void on_cancel(GtkWidget *w, gpointer data){
    gtk_main_quit();
}
typedef struct {
    GtkWidget *list;
    GtkWidget *combo_app;
    GtkWidget *checks[8];
    GPtrArray *rules;
} AppDictDlg;

typedef struct {
    gchar *id;
    gchar *label;
} AppComboItem;

static void app_combo_item_free(gpointer p) {
    AppComboItem *it = p;
    if (!it) return;
    g_free(it->id);
    g_free(it->label);
    g_free(it);
}

static gint app_combo_item_cmp(gconstpointer a, gconstpointer b) {
    const AppComboItem *ia = *(const AppComboItem * const *)a;
    const AppComboItem *ib = *(const AppComboItem * const *)b;
    return g_utf8_collate(ia->label ? ia->label : "", ib->label ? ib->label : "");
}

static gboolean is_generic_launcher(const gchar *id) {
    if (!id || !*id) return TRUE;
    const char * const generic[] = {
        "sh", "bash", "zsh", "env", "flatpak", "snap", "python", "python3", "java", "xdg-open", NULL
    };
    for (int i = 0; generic[i]; i++) {
        if (g_strcmp0(id, generic[i]) == 0) return TRUE;
    }
    return FALSE;
}

static void app_dict_populate_combo(AppDictDlg *d) {
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(d->combo_app));
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray *items = g_ptr_array_new_with_free_func(app_combo_item_free);

    GList *apps = g_app_info_get_all();
    for (GList *l = apps; l; l = l->next) {
        GAppInfo *app = G_APP_INFO(l->data);
        if (!app || !g_app_info_should_show(app)) continue;
        const char *name = g_app_info_get_display_name(app);
        const char *exec = g_app_info_get_executable(app);
        gchar *norm = gtv_app_dict_normalize_id(exec);
        if (is_generic_launcher(norm)) {
            g_free(norm);
            norm = gtv_app_dict_normalize_id(g_app_info_get_id(app));
            if (norm && g_str_has_suffix(norm, ".desktop")) {
                norm[strlen(norm) - 8] = '\0';
            }
        }
        if (!norm || !*norm) {
            g_free(norm);
            continue;
        }
        if (g_hash_table_contains(seen, norm)) {
            g_free(norm);
            continue;
        }
        g_hash_table_insert(seen, g_strdup(norm), GINT_TO_POINTER(1));
        AppComboItem *it = g_new0(AppComboItem, 1);
        it->id = norm;
        it->label = (name && *name && g_ascii_strcasecmp(name, norm) != 0)
            ? g_strdup_printf("%s (%s)", name, norm)
            : g_strdup(norm);
        g_ptr_array_add(items, it);
    }
    g_list_free_full(apps, g_object_unref);

    if (d->rules) {
        for (guint i = 0; i < d->rules->len; i++) {
            GtvAppDictRule *r = d->rules->pdata[i];
            if (!r || !r->app_id || g_hash_table_contains(seen, r->app_id)) continue;
            g_hash_table_insert(seen, g_strdup(r->app_id), GINT_TO_POINTER(1));
            AppComboItem *it = g_new0(AppComboItem, 1);
            it->id = g_strdup(r->app_id);
            it->label = g_strdup(r->app_id);
            g_ptr_array_add(items, it);
        }
    }

    g_ptr_array_sort(items, app_combo_item_cmp);
    for (guint i = 0; i < items->len; i++) {
        AppComboItem *it = items->pdata[i];
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->combo_app), it->id, it->label);
    }
    if (items->len > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(d->combo_app), 0);
    }

    g_ptr_array_unref(items);
    g_hash_table_destroy(seen);
}

static void app_dict_dlg_refresh(AppDictDlg *d) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(d->list));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
    for (guint i = 0; i < d->rules->len; i++) {
        GtvAppDictRule *r = d->rules->pdata[i];
        gchar *txt = g_strdup_printf("%s  →  %s", r->app_id, r->dicts);
        GtkWidget *lbl = gtk_label_new(txt);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_widget_set_margin_start(lbl, 6);
        gtk_widget_set_margin_end(lbl, 6);
        gtk_widget_set_margin_top(lbl, 4);
        gtk_widget_set_margin_bottom(lbl, 4);
        GtkWidget *row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), lbl);
        g_object_set_data(G_OBJECT(row), "rule-index", GUINT_TO_POINTER(i));
        gtk_list_box_insert(GTK_LIST_BOX(d->list), row, -1);
        g_free(txt);
    }
    gtk_widget_show_all(d->list);
}

static void on_app_dict_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    AppDictDlg *d = data;
    if (!row) return;
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "rule-index"));
    if (idx >= d->rules->len) return;
    GtvAppDictRule *r = d->rules->pdata[idx];
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(d->combo_app), r->app_id)) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->combo_app), r->app_id, r->app_id);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(d->combo_app), r->app_id);
    }
    guint n = gtv_dict_catalog_count();
    for (guint i = 0; i < n && i < G_N_ELEMENTS(d->checks); i++) {
        const GtvDictInfo *info = gtv_dict_catalog_get(i);
        if (d->checks[i] && info) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->checks[i]),
                gtv_dict_lang_enabled(r->dicts, info->id));
        }
    }
}

static void on_app_dict_add(GtkButton *btn, gpointer data) {
    (void)btn;
    AppDictDlg *d = data;
    const gchar *active_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(d->combo_app));
    gchar *norm = gtv_app_dict_normalize_id(active_id);
    if (!norm) return;
    GString *sel = g_string_new("");
    guint n = gtv_dict_catalog_count();
    for (guint i = 0; i < n && i < G_N_ELEMENTS(d->checks); i++) {
        const GtvDictInfo *info = gtv_dict_catalog_get(i);
        if (d->checks[i] && info && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->checks[i]))) {
            if (sel->len > 0) g_string_append_c(sel, ',');
            g_string_append(sel, info->id);
        }
    }
    if (sel->len == 0) g_string_assign(sel, "vi");
    gboolean updated = FALSE;
    for (guint i = 0; i < d->rules->len; i++) {
        GtvAppDictRule *r = d->rules->pdata[i];
        if (g_strcmp0(r->app_id, norm) == 0) {
            g_free(r->dicts);
            r->dicts = g_string_free(sel, FALSE);
            updated = TRUE;
            break;
        }
    }
    if (!updated) {
        GtvAppDictRule *r = g_new0(GtvAppDictRule, 1);
        r->app_id = norm;
        r->dicts = g_string_free(sel, FALSE);
        g_ptr_array_add(d->rules, r);
    } else {
        g_free(norm);
    }
    gtv_app_dicts_save(d->rules, NULL, NULL);
    app_dict_dlg_refresh(d);
}

static void on_app_dict_remove(GtkButton *btn, gpointer data) {
    (void)btn;
    AppDictDlg *d = data;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(d->list));
    if (!row) return;
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "rule-index"));
    if (idx < d->rules->len) {
        g_ptr_array_remove_index(d->rules, idx);
        gtv_app_dicts_save(d->rules, NULL, NULL);
        app_dict_dlg_refresh(d);
    }
}

static void on_app_dicts(GtkWidget *w, gpointer data) {
    (void)w;
    GtkWindow *parent = GTK_WINDOW(data);
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        gtv_i18n("Từ điển theo từng ứng dụng", "Per-app dictionaries"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        gtv_i18n("Đóng", "Close"), GTK_RESPONSE_CLOSE, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(dlg), "gtv-win");
    gtk_window_set_default_size(GTK_WINDOW(dlg), 500, 420);
    GtkWidget *close_btn = gtk_dialog_get_widget_for_response(GTK_DIALOG(dlg), GTK_RESPONSE_CLOSE);
    if (close_btn) gtk_style_context_add_class(gtk_widget_get_style_context(close_btn), "gtv-btn");

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    GtkWidget *hint = gtk_label_new(gtv_i18n(
        "Chọn ứng dụng và đánh dấu bộ từ điển riêng (ứng dụng khác dùng mặc định):",
        "Select an app and choose its dictionary packs (others use defaults):"));
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "gtv-card-sub");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    AppDictDlg d = {0};
    d.rules = gtv_app_dicts_load(NULL);

    GtkWidget *list_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(list_card), "gtv-card");
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sc, -1, 165);
    d.list = gtk_list_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(d.list), "gtv-nav-list");
    gtk_container_add(GTK_CONTAINER(sc), d.list);
    gtk_box_pack_start(GTK_BOX(list_card), sc, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), list_card, TRUE, TRUE, 0);
    g_signal_connect(d.list, "row-selected", G_CALLBACK(on_app_dict_row_selected), &d);

    GtkWidget *edit_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(edit_card), "gtv-card");
    gtk_box_pack_start(GTK_BOX(box), edit_card, FALSE, FALSE, 0);

    d.combo_app = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(edit_card), d.combo_app, FALSE, FALSE, 0);
    app_dict_populate_combo(&d);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_box_pack_start(GTK_BOX(edit_card), grid, FALSE, FALSE, 0);
    guint n = gtv_dict_catalog_count();
    for (guint i = 0; i < n && i < G_N_ELEMENTS(d.checks); i++) {
        const GtvDictInfo *info = gtv_dict_catalog_get(i);
        if (!info) continue;
        d.checks[i] = gtk_check_button_new_with_label(info->title);
        if (i == 0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.checks[i]), TRUE);
        gtk_grid_attach(GTK_GRID(grid), d.checks[i], (gint)(i % 3), (gint)(i / 3), 1, 1);
    }

    GtkWidget *act = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(edit_card), act, FALSE, FALSE, 0);
    GtkWidget *btn_add = gtk_button_new_with_label(gtv_i18n("Thêm / Cập nhật", "Add / Update"));
    GtkWidget *btn_del = gtk_button_new_with_label(gtv_i18n("Xóa đã chọn", "Delete selected"));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_add), "suggested-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_del), "gtv-btn");
    gtk_box_pack_start(GTK_BOX(act), btn_add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act), btn_del, FALSE, FALSE, 0);
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_app_dict_add), &d);
    g_signal_connect(btn_del, "clicked", G_CALLBACK(on_app_dict_remove), &d);

    app_dict_dlg_refresh(&d);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    g_ptr_array_unref(d.rules);
}

static void on_data(GtkWidget *w, gpointer data){
    (void)w;
    data_show(GTK_WINDOW(data));
}
static void sync_mode_widgets(gboolean is_vni) {
    if (ui_syncing) return;
    ui_syncing = TRUE;
    if (rb_telex && rb_vni)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(is_vni ? rb_vni : rb_telex), TRUE);
    if (sw_telex) gtk_switch_set_active(GTK_SWITCH(sw_telex), !is_vni);
    if (sw_vni) gtk_switch_set_active(GTK_SWITCH(sw_vni), is_vni);
    ui_syncing = FALSE;
}

static void on_mode_radio_toggled(GtkToggleButton *btn, gpointer data) {
    (void)data;
    if (ui_syncing || !gtk_toggle_button_get_active(btn)) return;
    sync_mode_widgets(GTK_WIDGET(btn) == rb_vni);
}

static void on_telex_switch_notify(GObject *obj, GParamSpec *pspec, gpointer data) {
    (void)pspec; (void)data;
    if (ui_syncing) return;
    gboolean on = gtk_switch_get_active(GTK_SWITCH(obj));
    sync_mode_widgets(!on);
}

static void on_vni_switch_notify(GObject *obj, GParamSpec *pspec, gpointer data) {
    (void)pspec; (void)data;
    if (ui_syncing) return;
    gboolean on = gtk_switch_get_active(GTK_SWITCH(obj));
    sync_mode_widgets(on);
}

static void on_suggest_gen_notify(GObject *obj, GParamSpec *pspec, gpointer data) {
    (void)pspec; (void)data;
    if (ui_syncing || !cb_suggest) return;
    ui_syncing = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_suggest), gtk_switch_get_active(GTK_SWITCH(obj)));
    ui_syncing = FALSE;
}

static void on_suggest_cb_toggled(GtkToggleButton *btn, gpointer data) {
    (void)data;
    if (ui_syncing || !sw_suggest_gen) return;
    ui_syncing = TRUE;
    gtk_switch_set_active(GTK_SWITCH(sw_suggest_gen), gtk_toggle_button_get_active(btn));
    ui_syncing = FALSE;
}

static void on_sidebar_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box; (void)data;
    if (!row || !setup_stack) return;
    const char *name = g_object_get_data(G_OBJECT(row), "page-name");
    if (name) {
        gtk_stack_set_visible_child_name(GTK_STACK(setup_stack), name);
        if (setup_scroll) {
            GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(setup_scroll));
            if (vadj) gtk_adjustment_set_value(vadj, 0.0);
        }
    }
}

static GtkWidget *make_card_box(const char *title, const char *subtitle, GtkWidget *trailing) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "gtv-card");

    GtkWidget *txt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(txt, GTK_ALIGN_CENTER);
    GtkWidget *lbl_t = gtk_label_new(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_t), "gtv-card-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_t), 0.0);
    gtk_box_pack_start(GTK_BOX(txt), lbl_t, FALSE, FALSE, 0);

    if (subtitle && *subtitle) {
        GtkWidget *lbl_s = gtk_label_new(subtitle);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_s), "gtv-card-sub");
        gtk_label_set_xalign(GTK_LABEL(lbl_s), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(lbl_s), TRUE);
        gtk_box_pack_start(GTK_BOX(txt), lbl_s, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(card), txt, TRUE, TRUE, 0);

    if (trailing) {
        gtk_widget_set_valign(trailing, GTK_ALIGN_CENTER);
        gtk_box_pack_end(GTK_BOX(card), trailing, FALSE, FALSE, 0);
    }
    return card;
}

static GtkWidget *make_nav_row(const char *icon_txt, const char *label_txt, const char *page_name) {
    GtkWidget *row = gtk_list_box_row_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(row), "gtv-nav-row");
    g_object_set_data(G_OBJECT(row), "page-name", (gpointer)page_name);

    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hb, 10);
    gtk_widget_set_margin_end(hb, 10);
    gtk_widget_set_margin_top(hb, 8);
    gtk_widget_set_margin_bottom(hb, 8);

    GtkWidget *ic = gtk_label_new(icon_txt);
    gtk_widget_set_size_request(ic, 20, -1);
    GtkWidget *lb = gtk_label_new(label_txt);
    gtk_label_set_xalign(GTK_LABEL(lb), 0.0);
    gtk_box_pack_start(GTK_BOX(hb), ic, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lb, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(row), hb);
    return row;
}

static GtkWidget *make_page_box(const char *heading) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 22);
    GtkWidget *h1 = gtk_label_new(heading);
    gtk_style_context_add_class(gtk_widget_get_style_context(h1), "gtv-h1");
    gtk_label_set_xalign(GTK_LABEL(h1), 0.0);
    gtk_widget_set_margin_bottom(h1, 6);
    gtk_box_pack_start(GTK_BOX(page), h1, FALSE, FALSE, 0);
    return page;
}

static void on_open_config_dir(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    gchar *dir = g_path_get_dirname(config_path);
    if (dir) {
        gchar *cmd = g_strdup_printf("xdg-open '%s' 2>/dev/null &", dir);
        run_shell(cmd);
        g_free(cmd);
        g_free(dir);
    }
}

static void apply_linux_theme(gboolean is_light) {
    if (!setup_css) return;
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", !is_light, NULL);
    }
    const char *dark_css =
        ".gtv-win, window.gtv-win, dialog.gtv-win { background-color: #181617; color: #F5F2F0; }"
        ".gtv-sidebar { background-color: #131112; border-right: 1px solid #2A2628; padding: 12px 8px; }"
        ".gtv-brand-row { padding: 6px 8px 14px 8px; }"
        ".gtv-brand { font-size: 15px; font-weight: 700; color: #F5F2F0; }"
        ".gtv-nav-list { background-color: transparent; border: none; }"
        ".gtv-nav-row { background-color: transparent; border-radius: 6px; color: #ACA3A0; margin: 2px 0; outline-style: none; }"
        ".gtv-nav-row:hover { background-color: #231F21; }"
        ".gtv-nav-row:selected { background-color: #2C2628; color: #FFFFFF; border-left: 3px solid #D32F2F; }"
        ".gtv-h1 { font-size: 20px; font-weight: 700; color: #F5F2F0; }"
        ".gtv-card { background-color: #211E20; border: 1px solid #322D30; border-radius: 10px; padding: 12px 16px; }"
        ".gtv-card-title { font-size: 13px; font-weight: 600; color: #F5F2F0; }"
        ".gtv-card-sub { font-size: 11px; color: #ACA3A0; }"
        ".gtv-btn { background: #2B2729; border: 1px solid #453E42; border-radius: 6px; color: #F5F2F0; padding: 6px 12px; box-shadow: none; text-shadow: none; }"
        ".gtv-btn:hover { background: #383235; }"
        "button.suggested-action { background: #D32F2F; border: 1px solid #E53935; border-radius: 6px; color: #FFFFFF; padding: 6px 14px; box-shadow: none; text-shadow: none; }"
        "button.suggested-action:hover { background: #E53935; }"
        "switch { background-color: #3B3538; border-radius: 999px; border: none; min-height: 22px; min-width: 42px; }"
        "switch:checked { background-color: #D32F2F; }"
        "switch slider { background-color: #FAF7F2; border-radius: 999px; min-height: 18px; min-width: 18px; margin: 2px; }"
        "checkbutton, radiobutton { color: #F5F2F0; }"
        "window.gtv-win entry, dialog.gtv-win entry { background-color: #2B2729; color: #F5F2F0; border: 1px solid #453E42; border-radius: 6px; box-shadow: none; }"
        "window.gtv-win entry:focus, dialog.gtv-win entry:focus { border-color: #D32F2F; }"
        "window.gtv-win entry selection, dialog.gtv-win entry selection { background-color: #7F1D1D; color: #FFFFFF; }"
        "window.gtv-win treeview, dialog.gtv-win treeview { background-color: #211E20; color: #F5F2F0; }"
        "window.gtv-win treeview:selected, dialog.gtv-win treeview:selected { background-color: #3A3033; color: #FFFFFF; }"
        "window.gtv-win treeview header button, dialog.gtv-win treeview header button { background-color: #2B2729; color: #F5F2F0; border-color: #453E42; box-shadow: none; }"
        "window.gtv-win scrolledwindow, window.gtv-win viewport, dialog.gtv-win scrolledwindow, dialog.gtv-win viewport { background-color: #211E20; }"
        ".gtv-splash-bar { background-color: #D32F2F; min-height: 4px; border-radius: 2px; }";
    const char *light_css =
        ".gtv-win, window.gtv-win, dialog.gtv-win { background-color: #FAF7F2; color: #2B2320; }"
        ".gtv-sidebar { background-color: #F3EFE8; border-right: 1px solid #E7E5E4; padding: 12px 8px; }"
        ".gtv-brand-row { padding: 6px 8px 14px 8px; }"
        ".gtv-brand { font-size: 15px; font-weight: 700; color: #2B2320; }"
        ".gtv-nav-list { background-color: transparent; border: none; }"
        ".gtv-nav-row { background-color: transparent; border-radius: 6px; color: #57534E; margin: 2px 0; outline-style: none; }"
        ".gtv-nav-row:hover { background-color: #ECE6DC; }"
        ".gtv-nav-row:selected { background-color: #FEE2E2; color: #B71C1C; border-left: 3px solid #D32F2F; }"
        ".gtv-h1 { font-size: 20px; font-weight: 700; color: #2B2320; }"
        ".gtv-card { background-color: #FFFFFF; border: 1px solid #E7E5E4; border-radius: 10px; padding: 12px 16px; }"
        ".gtv-card-title { font-size: 13px; font-weight: 600; color: #2B2320; }"
        ".gtv-card-sub { font-size: 11px; color: #57534E; }"
        ".gtv-btn { background: #F5F5F4; border: 1px solid #D6D3D1; border-radius: 6px; color: #2B2320; padding: 6px 12px; box-shadow: none; text-shadow: none; }"
        ".gtv-btn:hover { background: #E7E5E4; }"
        "button.suggested-action { background: #D32F2F; border: 1px solid #B71C1C; border-radius: 6px; color: #FFFFFF; padding: 6px 14px; box-shadow: none; text-shadow: none; }"
        "button.suggested-action:hover { background: #B71C1C; }"
        "switch { background-color: #D6D3D1; border-radius: 999px; border: none; min-height: 22px; min-width: 42px; }"
        "switch:checked { background-color: #D32F2F; }"
        "switch slider { background-color: #FFFFFF; border-radius: 999px; min-height: 18px; min-width: 18px; margin: 2px; }"
        "checkbutton, radiobutton { color: #2B2320; }"
        "window.gtv-win entry, dialog.gtv-win entry { background-color: #FFFFFF; color: #2B2320; border: 1px solid #D6D3D1; border-radius: 6px; box-shadow: none; }"
        "window.gtv-win entry:focus, dialog.gtv-win entry:focus { border-color: #D32F2F; }"
        "window.gtv-win entry selection, dialog.gtv-win entry selection { background-color: #FEE2E2; color: #7F1D1D; }"
        "window.gtv-win treeview, dialog.gtv-win treeview { background-color: #FFFFFF; color: #2B2320; }"
        "window.gtv-win treeview:selected, dialog.gtv-win treeview:selected { background-color: #FEE2E2; color: #7F1D1D; }"
        "window.gtv-win treeview header button, dialog.gtv-win treeview header button { background-color: #F5F5F4; color: #2B2320; border-color: #D6D3D1; box-shadow: none; }"
        "window.gtv-win scrolledwindow, window.gtv-win viewport, dialog.gtv-win scrolledwindow, dialog.gtv-win viewport { background-color: #FFFFFF; }"
        ".gtv-splash-bar { background-color: #D32F2F; min-height: 4px; border-radius: 2px; }";
    gtk_css_provider_load_from_data(setup_css, is_light ? light_css : dark_css, -1, NULL);
}

static void on_theme_radio_toggled(GtkToggleButton *btn, gpointer data) {
    (void)data;
    if (!gtk_toggle_button_get_active(btn)) return;
    apply_linux_theme(GTK_WIDGET(btn) == rb_theme_light);
}

static gboolean on_splash_done(gpointer data) {
    GtkWidget *splash = GTK_WIDGET(data);
    if (splash && GTK_IS_WIDGET(splash))
        gtk_widget_destroy(splash);
    return G_SOURCE_REMOVE;
}

static void show_startup_splash(gboolean is_light) {
    apply_linux_theme(is_light);
    GtkWidget *splash = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_style_context_add_class(gtk_widget_get_style_context(splash), "gtv-win");
    gtk_window_set_position(GTK_WINDOW(splash), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(splash), 340, 200);
    GtkWidget *sp_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(sp_box), "gtv-card");
    gtk_container_set_border_width(GTK_CONTAINER(sp_box), 24);
    gtk_container_add(GTK_CONTAINER(splash), sp_box);

    GdkPixbuf *sp_pb = gdk_pixbuf_new_from_file_at_scale("/usr/share/gotv/icons/gotv.png", 48, 48, TRUE, NULL);
    if (!sp_pb)
        sp_pb = gdk_pixbuf_new_from_file_at_scale("/usr/share/gotv/icons/gotv.svg", 48, 48, TRUE, NULL);
    GtkWidget *sp_img = sp_pb
        ? gtk_image_new_from_pixbuf(sp_pb)
        : gtk_image_new_from_icon_name("gotv", GTK_ICON_SIZE_DIALOG);
    if (sp_pb) g_object_unref(sp_pb);
    gtk_box_pack_start(GTK_BOX(sp_box), sp_img, FALSE, FALSE, 0);

    GtkWidget *sp_title = gtk_label_new("GoTV");
    gtk_style_context_add_class(gtk_widget_get_style_context(sp_title), "gtv-h1");
    gtk_box_pack_start(GTK_BOX(sp_box), sp_title, FALSE, FALSE, 0);

    GtkWidget *sp_sub = gtk_label_new(gtv_i18n("Bộ gõ tiếng Việt · Telex / VNI", "Vietnamese Input · Telex / VNI"));
    gtk_style_context_add_class(gtk_widget_get_style_context(sp_sub), "gtv-card-sub");
    gtk_box_pack_start(GTK_BOX(sp_box), sp_sub, FALSE, FALSE, 0);

    GtkWidget *sp_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(sp_bar), "gtv-splash-bar");
    gtk_widget_set_size_request(sp_bar, 140, 4);
    gtk_widget_set_halign(sp_bar, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(sp_box), sp_bar, FALSE, FALSE, 4);

    gtk_widget_show_all(splash);
    g_timeout_add(500, on_splash_done, splash);
}

int setup_ui(int argc, char *argv[], const char *cur_method, const char *cur_modern, const char *cur_spell, const char *cur_suggest, const char *cfg){
    g_set_prgname("gotv");
    g_set_application_name("GoTV");
    config_path = strdup(cfg);
    gtk_init(&argc, &argv);

    gboolean init_light = FALSE;
    {
        GKeyFile *kf = g_key_file_new();
        if (g_key_file_load_from_file(kf, cfg, G_KEY_FILE_NONE, NULL)) {
            gchar *th = g_key_file_get_string(kf, "general", "theme", NULL);
            if (th) {
                g_strstrip(th);
                if (!g_ascii_strcasecmp(th, "light")) init_light = TRUE;
                g_free(th);
            }
        }
        g_key_file_unref(kf);
    }

    setup_css = gtk_css_provider_new();
    apply_linux_theme(init_light);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(setup_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(setup_css);

    GtkIconTheme *theme = gtk_icon_theme_get_default();
    if(theme){
        gtk_icon_theme_append_search_path(theme, "/usr/share/gotv/icons");
        gtk_icon_theme_append_search_path(theme, "/usr/share/icons/hicolor");
    }
    gtk_window_set_default_icon_name("gotv");

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_style_context_add_class(gtk_widget_get_style_context(win), "gtv-win");
    gtk_window_set_title(GTK_WINDOW(win), gtv_i18n("GoTV — Cài đặt", "GoTV — Settings"));
    gtk_window_set_default_size(GTK_WINDOW(win), 780, 580);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(win), "gotv");
    gtk_window_set_icon_from_file(GTK_WINDOW(win), "/usr/share/gotv/icons/gotv.png", NULL);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(win), root);

    /* Left Sidebar */
    GtkWidget *sb_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(sb_box), "gtv-sidebar");
    gtk_widget_set_size_request(sb_box, 196, -1);
    gtk_box_pack_start(GTK_BOX(root), sb_box, FALSE, FALSE, 0);

    GtkWidget *brand_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(brand_box), "gtv-brand-row");
    GdkPixbuf *logo_pb = gdk_pixbuf_new_from_file_at_scale("/usr/share/gotv/icons/gotv.png", 22, 22, TRUE, NULL);
    if (!logo_pb)
        logo_pb = gdk_pixbuf_new_from_file_at_scale("/usr/share/gotv/icons/gotv.svg", 22, 22, TRUE, NULL);
    GtkWidget *brand_icon = logo_pb
        ? gtk_image_new_from_pixbuf(logo_pb)
        : gtk_image_new_from_icon_name("gotv", GTK_ICON_SIZE_LARGE_TOOLBAR);
    if (logo_pb) g_object_unref(logo_pb);
    gtk_box_pack_start(GTK_BOX(brand_box), brand_icon, FALSE, FALSE, 0);

    GtkWidget *brand = gtk_label_new("GoTV");
    gtk_style_context_add_class(gtk_widget_get_style_context(brand), "gtv-brand");
    gtk_label_set_xalign(GTK_LABEL(brand), 0.0);
    gtk_box_pack_start(GTK_BOX(brand_box), brand, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sb_box), brand_box, FALSE, FALSE, 0);

    GtkWidget *sidebar = gtk_list_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "gtv-nav-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebar), GTK_SELECTION_SINGLE);
    GtkWidget *r_gen = make_nav_row("⚙", gtv_i18n("Tổng quan", "General"), "general");
    GtkWidget *r_typ = make_nav_row("⌨", gtv_i18n("Kiểu gõ", "Typing Modes"), "typing");
    GtkWidget *r_dic = make_nav_row("📖", gtv_i18n("Từ điển", "Dictionaries"), "dicts");
    GtkWidget *r_sho = make_nav_row("⌘", gtv_i18n("Phím tắt", "Shortcuts"), "shortcuts");
    GtkWidget *r_abt = make_nav_row("ⓘ", gtv_i18n("Thông tin", "About"), "about");
    gtk_list_box_insert(GTK_LIST_BOX(sidebar), r_gen, -1);
    gtk_list_box_insert(GTK_LIST_BOX(sidebar), r_typ, -1);
    gtk_list_box_insert(GTK_LIST_BOX(sidebar), r_dic, -1);
    gtk_list_box_insert(GTK_LIST_BOX(sidebar), r_sho, -1);
    gtk_list_box_insert(GTK_LIST_BOX(sidebar), r_abt, -1);
    gtk_box_pack_start(GTK_BOX(sb_box), sidebar, TRUE, TRUE, 0);

    /* Right side: Stack + Footer action bar */
    GtkWidget *right_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(root), right_col, TRUE, TRUE, 0);

    setup_stack = gtk_stack_new();
    gtk_stack_set_vhomogeneous(GTK_STACK(setup_stack), FALSE);
    gtk_stack_set_transition_type(GTK_STACK(setup_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    GtkWidget *scroll_main = gtk_scrolled_window_new(NULL, NULL);
    setup_scroll = scroll_main;
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_main), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll_main), setup_stack);
    gtk_box_pack_start(GTK_BOX(right_col), scroll_main, TRUE, TRUE, 0);

    gboolean is_vni = (strcmp(cur_method, "vni") == 0);
    gboolean is_suggest = (strcmp(cur_suggest, "true") == 0);

    /* Page 1: General */
    GtkWidget *pg_gen = make_page_box(gtv_i18n("Cài đặt GoTV", "GoTV Settings"));
    gtk_stack_add_named(GTK_STACK(setup_stack), pg_gen, "general");

    GtkWidget *mode_ctrl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *l_tlx = gtk_label_new("Telex");
    sw_telex = gtk_switch_new();
    gtk_widget_set_valign(sw_telex, GTK_ALIGN_CENTER);
    GtkWidget *l_vni = gtk_label_new("VNI");
    sw_vni = gtk_switch_new();
    gtk_widget_set_valign(sw_vni, GTK_ALIGN_CENTER);
    gtk_switch_set_active(GTK_SWITCH(sw_telex), !is_vni);
    gtk_switch_set_active(GTK_SWITCH(sw_vni), is_vni);
    g_signal_connect(sw_telex, "notify::active", G_CALLBACK(on_telex_switch_notify), NULL);
    g_signal_connect(sw_vni, "notify::active", G_CALLBACK(on_vni_switch_notify), NULL);
    gtk_box_pack_start(GTK_BOX(mode_ctrl), l_tlx, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mode_ctrl), sw_telex, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mode_ctrl), l_vni, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mode_ctrl), sw_vni, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pg_gen), make_card_box(gtv_i18n("Kiểu gõ", "Input method"), NULL, mode_ctrl), FALSE, FALSE, 0);

    GtkWidget *dict_ctrl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    sw_suggest_gen = gtk_switch_new();
    gtk_widget_set_valign(sw_suggest_gen, GTK_ALIGN_CENTER);
    gtk_switch_set_active(GTK_SWITCH(sw_suggest_gen), is_suggest);
    g_signal_connect(sw_suggest_gen, "notify::active", G_CALLBACK(on_suggest_gen_notify), NULL);
    GtkWidget *btn_upd_gen = gtk_button_new_with_label(gtv_i18n("Tải xuống/Cập nhật", "Download/Update"));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_upd_gen), "gtv-btn");
    g_signal_connect(btn_upd_gen, "clicked", G_CALLBACK(on_dict_update), NULL);
    gtk_box_pack_start(GTK_BOX(dict_ctrl), sw_suggest_gen, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dict_ctrl), btn_upd_gen, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pg_gen), make_card_box(gtv_i18n("Từ điển offline", "Offline dictionary"),
        gtv_i18n("Gợi ý từ thông minh 100% offline", "100% offline smart word suggestions"), dict_ctrl), FALSE, FALSE, 0);

    GtkWidget *btn_macro = gtk_button_new_with_label(gtv_i18n("Cấu hình", "Configure"));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_macro), "gtv-btn");
    g_signal_connect(btn_macro, "clicked", G_CALLBACK(on_data), win);
    gtk_box_pack_start(GTK_BOX(pg_gen), make_card_box(gtv_i18n("Phím tắt macro", "Macro shortcuts"),
        gtv_i18n("Bấm Tab khi gõ để bung từ viết tắt", "Press Tab to expand shortcuts"), btn_macro), FALSE, FALSE, 0);

    GtkWidget *btn_emoji = gtk_button_new_with_label(gtv_i18n("Mở bảng...", "Open..."));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_emoji), "gtv-btn");
    g_signal_connect(btn_emoji, "clicked", G_CALLBACK(on_data), win);
    gtk_box_pack_start(GTK_BOX(pg_gen), make_card_box(gtv_i18n("Bảng emoji & Ký tự", "Emoji & Symbols"),
        gtv_i18n("Quản lý bảng emoji và ký tự đặc biệt", "Manage emoji and symbol tables"), btn_emoji), FALSE, FALSE, 0);

    GtkWidget *lang_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    rb_lang_vi = gtk_radio_button_new_with_label(NULL, "Tiếng Việt");
    rb_lang_en = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rb_lang_vi), "English");
    if (gtv_i18n_get_lang() == GTV_LANG_EN)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb_lang_en), TRUE);
    else
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb_lang_vi), TRUE);
    gtk_box_pack_start(GTK_BOX(lang_box), rb_lang_vi, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(lang_box), rb_lang_en, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pg_gen), make_card_box(gtv_i18n("Ngôn ngữ giao diện", "Interface language"), NULL, lang_box), FALSE, FALSE, 0);

    GtkWidget *theme_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    rb_theme_dark = gtk_radio_button_new_with_label(NULL, gtv_i18n("Tối", "Dark"));
    rb_theme_light = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rb_theme_dark), gtv_i18n("Sáng", "Light"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(init_light ? rb_theme_light : rb_theme_dark), TRUE);
    g_signal_connect(rb_theme_dark, "toggled", G_CALLBACK(on_theme_radio_toggled), NULL);
    g_signal_connect(rb_theme_light, "toggled", G_CALLBACK(on_theme_radio_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(theme_box), rb_theme_dark, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(theme_box), rb_theme_light, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pg_gen), make_card_box(gtv_i18n("Giao diện", "Theme"), NULL, theme_box), FALSE, FALSE, 0);

    /* Page 2: Typing Modes */
    GtkWidget *pg_typ = make_page_box(gtv_i18n("Kiểu gõ & Chính tả", "Typing Modes"));
    gtk_stack_add_named(GTK_STACK(setup_stack), pg_typ, "typing");

    GtkWidget *f1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(f1), "gtv-card");
    rb_telex = gtk_radio_button_new_with_label(NULL, "Telex  — s f r x j, w z, aa ee oo aw ow uw dd, uow→ươ");
    rb_vni = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rb_telex), "VNI     — 1 2 3 4 5, 6 7 8 9 0, a6→â, a8→ă, o7→ơ");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(is_vni ? rb_vni : rb_telex), TRUE);
    g_signal_connect(rb_telex, "toggled", G_CALLBACK(on_mode_radio_toggled), NULL);
    g_signal_connect(rb_vni, "toggled", G_CALLBACK(on_mode_radio_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(f1), rb_telex, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(f1), rb_vni, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pg_typ), f1, FALSE, FALSE, 0);

    GtkWidget *f2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(f2), "gtv-card");
    cb_modern = gtk_check_button_new_with_label(gtv_i18n("Kiểu mới (modern) — hòa thay vì hoà cho oa/oe/uy (Bộ GD 2018)",
                                                        "Modern style — hòa instead of hoà for oa/oe/uy"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_modern), strcmp(cur_modern, "true") == 0);
    g_signal_connect(cb_modern, "toggled", G_CALLBACK(on_modern_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(f2), cb_modern, FALSE, FALSE, 0);
    lbl_preview = gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_preview), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl_preview), 0.0);
    update_preview();
    gtk_box_pack_start(GTK_BOX(f2), lbl_preview, FALSE, FALSE, 0);
    cb_spell = gtk_check_button_new_with_label(gtv_i18n("Kiểm tra chính tả (chỉ gợi ý, không chặn)",
                                                       "Spell check (suggestions only, non-blocking)"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_spell), strcmp(cur_spell, "true") == 0);
    gtk_box_pack_start(GTK_BOX(f2), cb_spell, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pg_typ), f2, FALSE, FALSE, 0);

    /* Page 3: Dictionaries */
    GtkWidget *pg_dic = make_page_box(gtv_i18n("Từ điển & Gợi ý", "Dictionaries"));
    gtk_stack_add_named(GTK_STACK(setup_stack), pg_dic, "dicts");

    GtkWidget *box_dict = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(box_dict), "gtv-card");
    cb_suggest = gtk_check_button_new_with_label(gtv_i18n("Bật gợi ý từ thông minh", "Enable smart word suggestions"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_suggest), is_suggest);
    g_signal_connect(cb_suggest, "toggled", G_CALLBACK(on_suggest_toggled), NULL);
    g_signal_connect(cb_suggest, "toggled", G_CALLBACK(on_suggest_cb_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(box_dict), cb_suggest, FALSE, FALSE, 0);

    GtkWidget *grid_packs = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid_packs), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid_packs), 4);
    gtk_box_pack_start(GTK_BOX(box_dict), grid_packs, FALSE, FALSE, 0);
    const gchar *active_langs = gtv_dict_get_langs();
    guint pack_count = gtv_dict_catalog_count();
    for(guint i = 0; i < pack_count && i < G_N_ELEMENTS(cb_dict_packs); i++){
        const GtvDictInfo *info = gtv_dict_catalog_get(i);
        if(!info) continue;
        guint pwords = gtv_dict_pack_count(info->id);
        const char *title_p = info->title;
        if (!strcmp(info->id, "vi")) title_p = gtv_i18n("Tiếng Việt", "Vietnamese");
        else if (!strcmp(info->id, "en")) title_p = gtv_i18n("Tiếng Anh", "English");
        gchar *label = g_strdup_printf("%s (%u)", title_p, pwords);
        cb_dict_packs[i] = gtk_check_button_new_with_label(label);
        g_free(label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_dict_packs[i]),
            gtv_dict_lang_enabled(active_langs, info->id));
        g_signal_connect(cb_dict_packs[i], "toggled", G_CALLBACK(on_dict_pack_toggled), NULL);
        gtk_grid_attach(GTK_GRID(grid_packs), cb_dict_packs[i], (gint)(i % 3), (gint)(i / 3), 1, 1);
    }

    lbl_dict_ver = gtk_label_new(gtv_i18n("Từ điển...", "Dictionary..."));
    gtk_label_set_xalign(GTK_LABEL(lbl_dict_ver), 0.0);
    gtk_box_pack_start(GTK_BOX(box_dict), lbl_dict_ver, FALSE, FALSE, 0);
    btn_dict_update = gtk_button_new_with_label(gtv_i18n("Tải xuống/Cập nhật", "Download/Update"));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_dict_update), "gtv-btn");
    g_signal_connect(btn_dict_update, "clicked", G_CALLBACK(on_dict_update), NULL);
    gtk_box_pack_start(GTK_BOX(box_dict), btn_dict_update, FALSE, FALSE, 0);
    GtkWidget *btn_app_dicts = gtk_button_new_with_label(gtv_i18n("Theo từng ứng dụng...", "Per-app dictionaries..."));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_app_dicts), "gtv-btn");
    g_signal_connect(btn_app_dicts, "clicked", G_CALLBACK(on_app_dicts), win);
    gtk_box_pack_start(GTK_BOX(box_dict), btn_app_dicts, FALSE, FALSE, 0);
    lbl_dict_status = gtk_label_new(gtv_i18n("Gợi ý offline 100%, không cần mạng.", "100% offline suggestions, no internet required."));
    gtk_label_set_line_wrap(GTK_LABEL(lbl_dict_status), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl_dict_status), 0.0);
    gtk_box_pack_start(GTK_BOX(box_dict), lbl_dict_status, FALSE, FALSE, 0);
    dict_ui_refresh();
    gtk_box_pack_start(GTK_BOX(pg_dic), box_dict, FALSE, FALSE, 0);

    /* Page 4: Shortcuts */
    GtkWidget *pg_sho = make_page_box(gtv_i18n("Phím tắt & Dữ liệu", "Shortcuts & Data"));
    gtk_stack_add_named(GTK_STACK(setup_stack), pg_sho, "shortcuts");
    GtkWidget *btn_data_s = gtk_button_new_with_label(gtv_i18n("Cấu hình", "Configure"));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_data_s), "gtv-btn");
    g_signal_connect(btn_data_s, "clicked", G_CALLBACK(on_data), win);
    gtk_box_pack_start(GTK_BOX(pg_sho), make_card_box(gtv_i18n("Bảng gõ tắt (Macro) & Emoji", "Macro & Emoji Tables"),
        gtv_i18n("Bấm Tab khi gõ để bung từ viết tắt", "Press Tab to expand shortcuts"), btn_data_s), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pg_sho), make_card_box(gtv_i18n("Xóa dấu nhanh", "Quick tone removal"),
        gtv_i18n("Gõ lại phím dấu để xóa: as→á, á s→a, uw→ư, ư w→u", "Retype tone key: as→á, á s→a"), NULL), FALSE, FALSE, 0);

    /* Page 5: About */
    GtkWidget *pg_abt = make_page_box(gtv_i18n("Thông tin GoTV", "About GoTV"));
    gtk_stack_add_named(GTK_STACK(setup_stack), pg_abt, "about");
    gtk_box_pack_start(GTK_BOX(pg_abt), make_card_box("GoTV — Linux IBus",
        gtv_i18n("Thuần C + GTK3, không thư viện ngoài · Mã nguồn mở MIT · github.com/isthaison/gotv",
                 "Pure C + GTK3, zero bloat · MIT Open Source · github.com/isthaison/gotv"), NULL), FALSE, FALSE, 0);
    GtkWidget *btn_dir = gtk_button_new_with_label(gtv_i18n("Mở thư mục", "Open folder"));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_dir), "gtv-btn");
    g_signal_connect(btn_dir, "clicked", G_CALLBACK(on_open_config_dir), NULL);
    gtk_box_pack_start(GTK_BOX(pg_abt), make_card_box(gtv_i18n("Thư mục cấu hình", "Config directory"),
        "~/.config/gotv/config", btn_dir), FALSE, FALSE, 0);

    g_signal_connect(sidebar, "row-selected", G_CALLBACK(on_sidebar_row_selected), NULL);
    gtk_list_box_select_row(GTK_LIST_BOX(sidebar), GTK_LIST_BOX_ROW(r_gen));

    /* Bottom action bar */
    GtkWidget *bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(bbox, GTK_ALIGN_END);
    gtk_widget_set_margin_end(bbox, 22);
    gtk_widget_set_margin_bottom(bbox, 16);
    gtk_widget_set_margin_top(bbox, 8);
    gtk_box_pack_end(GTK_BOX(right_col), bbox, FALSE, FALSE, 0);

    GtkWidget *btn_cancel = gtk_button_new_with_label(gtv_i18n("Hủy", "Cancel"));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_cancel), "gtv-btn");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_cancel), NULL);
    gtk_box_pack_start(GTK_BOX(bbox), btn_cancel, FALSE, FALSE, 0);

    GtkWidget *btn_ok = gtk_button_new_with_label(gtv_i18n("Lưu & Đóng", "Save & Close"));
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_ok), "suggested-action");
    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_save), win);
    gtk_box_pack_start(GTK_BOX(bbox), btn_ok, FALSE, FALSE, 0);

    gtk_widget_show_all(win);
    gtk_widget_grab_focus(btn_ok);
    if (setup_scroll) {
        GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(setup_scroll));
        if (vadj) gtk_adjustment_set_value(vadj, 0.0);
    }
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
    cli_choice("Bộ từ điển [vi,tsql,c,csharp,en]", input, sizeof(input));
    if (*input) {
        gtv_dict_set_langs(input);
        g_strlcpy(config->dicts, gtv_dict_get_langs(), sizeof(config->dicts));
    }
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
