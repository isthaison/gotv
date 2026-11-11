#include "data.h"
#include "../../engine/internal.h"
#include <gtk/gtk.h>

typedef struct {
    GtkWidget *dlg;
    GtkWidget *rb_macro;
    GtkWidget *view;
    GtkListStore *store;
    GtkWidget *key_entry;
    GtkWidget *val_entry;
    gboolean emoji;
    gboolean dirty;
} DataUi;

static void data_refresh(DataUi *u) {
    gtk_list_store_clear(u->store);
    guint n = gtv_table_count(u->emoji);
    for (guint i = 0; i < n; i++) {
        const gchar *k = NULL, *v = NULL;
        if (!gtv_table_get(u->emoji, i, &k, &v)) continue;
        GtkTreeIter it;
        gtk_list_store_append(u->store, &it);
        gtk_list_store_set(u->store, &it, 0, k ? k : "", 1, v ? v : "", -1);
    }
}

static gint data_selected(DataUi *u) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(u->view));
    GtkTreeModel *model = NULL;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return -1;
    GtkTreePath *path = gtk_tree_model_get_path(model, &it);
    gint *idx = path ? gtk_tree_path_get_indices(path) : NULL;
    gint row = idx ? idx[0] : -1;
    if (path) gtk_tree_path_free(path);
    return row;
}

static void data_error(DataUi *u, const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(u->dlg), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static gboolean data_persist(DataUi *u) {
    GError *err = NULL;
    if (!gtv_table_save(u->emoji, &err)) {
        data_error(u, err ? err->message : gtv_i18n("Không lưu được file dữ liệu.", "Could not save data file."));
        g_clear_error(&err);
        return FALSE;
    }
    gtv_tables_reload();
    u->dirty = TRUE;
    return TRUE;
}

static void on_type_toggled(GtkWidget *w, gpointer data) {
    DataUi *u = data;
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) return;
    u->emoji = (w != u->rb_macro);
    gtk_entry_set_text(GTK_ENTRY(u->key_entry), "");
    gtk_entry_set_text(GTK_ENTRY(u->val_entry), "");
    data_refresh(u);
}

static void on_selection_changed(GtkTreeSelection *sel, gpointer data) {
    DataUi *u = data;
    GtkTreeModel *model = NULL;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return;
    GtkTreePath *path = gtk_tree_model_get_path(model, &it);
    gint *idx = path ? gtk_tree_path_get_indices(path) : NULL;
    if (idx) {
        const gchar *k = NULL, *v = NULL;
        if (gtv_table_get(u->emoji, (guint)idx[0], &k, &v)) {
            gtk_entry_set_text(GTK_ENTRY(u->key_entry), k ? k : "");
            gtk_entry_set_text(GTK_ENTRY(u->val_entry), v ? v : "");
        }
    }
    if (path) gtk_tree_path_free(path);
}

static void on_save(GtkWidget *w, gpointer data) {
    DataUi *u = data;
    (void)w;
    gchar *key = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(u->key_entry))));
    gchar *val = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(u->val_entry))));
    if (!gtv_table_set(u->emoji, key, val)) {
        data_error(u, gtv_i18n("Khóa/giá trị không hợp lệ (khóa không chứa dấu cách hay dấu =, giá trị không rỗng).",
                               "Invalid key/value (key cannot contain spaces or '=', value cannot be empty)."));
    } else if (data_persist(u)) {
        data_refresh(u);
    }
    g_free(key);
    g_free(val);
}

static void on_delete(GtkWidget *w, gpointer data) {
    DataUi *u = data;
    (void)w;
    gint row = data_selected(u);
    if (row < 0) return;
    const gchar *k = NULL;
    if (!gtv_table_get(u->emoji, (guint)row, &k, NULL) || !k) return;
    if (gtv_table_remove(u->emoji, k) && data_persist(u)) {
        gtk_entry_set_text(GTK_ENTRY(u->key_entry), "");
        gtk_entry_set_text(GTK_ENTRY(u->val_entry), "");
        data_refresh(u);
    }
}

static void on_folder(GtkWidget *w, gpointer data) {
    DataUi *u = data;
    (void)w;
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    gchar *uri = g_filename_to_uri(dir, NULL, NULL);
    if (uri) {
        gtk_show_uri_on_window(GTK_WINDOW(u->dlg), uri, GDK_CURRENT_TIME, NULL);
        g_free(uri);
    }
    g_free(dir);
}

void data_show(GtkWindow *parent) {
    DataUi u = {0};
    u.dlg = gtk_dialog_new_with_buttons(gtv_i18n("GoTV — Dữ liệu", "GoTV — Data"),
        parent, GTK_DIALOG_MODAL, gtv_i18n("_Đóng", "_Close"), GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(u.dlg), 520, 430);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(u.dlg));
    gtk_box_set_spacing(GTK_BOX(content), 8);

    GtkWidget *typebox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(content), typebox, FALSE, FALSE, 0);
    u.rb_macro = gtk_radio_button_new_with_label(NULL, gtv_i18n("Macro (gõ tắt)", "Macro (shortcuts)"));
    GtkWidget *rb_emoji = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(u.rb_macro), "Emoji");
    g_signal_connect(u.rb_macro, "toggled", G_CALLBACK(on_type_toggled), &u);
    g_signal_connect(rb_emoji, "toggled", G_CALLBACK(on_type_toggled), &u);
    gtk_box_pack_start(GTK_BOX(typebox), u.rb_macro, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(typebox), rb_emoji, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    u.store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    u.view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(u.store));
    g_object_unref(u.store);
    GtkCellRenderer *rend = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(u.view),
        -1, gtv_i18n("Từ gõ", "Shortcut"), rend, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(u.view),
        -1, gtv_i18n("Nội dung", "Expansion"), rend, "text", 1, NULL);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(u.view), TRUE);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(u.view)),
        GTK_SELECTION_SINGLE);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(u.view)), "changed",
        G_CALLBACK(on_selection_changed), &u);
    gtk_container_add(GTK_CONTAINER(scroll), u.view);

    GtkWidget *form = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(form), 6);
    gtk_grid_set_column_spacing(GTK_GRID(form), 8);
    gtk_box_pack_start(GTK_BOX(content), form, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(form), gtk_label_new(gtv_i18n("Từ gõ:", "Shortcut:")), 0, 0, 1, 1);
    u.key_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(u.key_entry), 255);
    gtk_widget_set_hexpand(u.key_entry, TRUE);
    gtk_grid_attach(GTK_GRID(form), u.key_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(form), gtk_label_new(gtv_i18n("Nội dung:", "Expansion:")), 0, 1, 1, 1);
    u.val_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(u.val_entry), 1023);
    gtk_widget_set_hexpand(u.val_entry, TRUE);
    gtk_grid_attach(GTK_GRID(form), u.val_entry, 1, 1, 1, 1);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(content), btns, FALSE, FALSE, 0);
    GtkWidget *b_save = gtk_button_new_with_label(gtv_i18n("Lưu", "Save"));
    GtkWidget *b_del = gtk_button_new_with_label(gtv_i18n("Xóa", "Delete"));
    GtkWidget *b_folder = gtk_button_new_with_label(gtv_i18n("Mở thư mục", "Open folder"));
    g_signal_connect(b_save, "clicked", G_CALLBACK(on_save), &u);
    g_signal_connect(b_del, "clicked", G_CALLBACK(on_delete), &u);
    g_signal_connect(b_folder, "clicked", G_CALLBACK(on_folder), &u);
    gtk_box_pack_start(GTK_BOX(btns), b_save, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), b_del, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), b_folder, FALSE, FALSE, 0);

    GtkWidget *note = gtk_label_new(gtv_i18n("Thay đổi lưu vào file riêng của bạn. Đóng cửa sổ này sẽ restart ibus để engine nhận bảng mới.",
                                             "Changes are saved to your personal file. Closing this window will restart ibus to load the new table."));
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0);
    gtk_box_pack_start(GTK_BOX(content), note, FALSE, FALSE, 0);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u.rb_macro), TRUE);
    data_refresh(&u);
    gtk_widget_show_all(u.dlg);
    gtk_dialog_run(GTK_DIALOG(u.dlg));
    if (u.dirty) {
        int status = system("ibus restart 2>/dev/null &");
        if (status != 0) g_warning("ibus restart failed (status %d)", status);
    }
    gtk_widget_destroy(u.dlg);
}
