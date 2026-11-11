#include "data.h"
#include "resource.h"
#include "win_utf.h"
#include "internal.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <glib.h>

static gboolean show_emoji = FALSE;

static void list_setup_columns(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_DATA_LIST);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    struct { const char *text; int width; } cols[] = {
        { gtv_i18n("Từ gõ", "Shortcut"), 100 },
        { gtv_i18n("Nội dung", "Expansion"), 140 },
    };
    for (int i = 0; i < 2; i++) {
        gunichar2 *w = g_utf8_to_utf16(cols[i].text, -1, NULL, NULL, NULL);
        LVCOLUMNW col = {0};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)w;
        col.cx = cols[i].width;
        col.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        g_free(w);
    }
}

static void list_refresh(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_DATA_LIST);
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    guint n = gtv_table_count(show_emoji);
    for (guint i = 0; i < n; i++) {
        const gchar *k = NULL, *v = NULL;
        if (!gtv_table_get(show_emoji, i, &k, &v)) continue;
        LVITEMW item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        gunichar2 *wk = g_utf8_to_utf16(k ? k : "", -1, NULL, NULL, NULL);
        gunichar2 *wv = g_utf8_to_utf16(v ? v : "", -1, NULL, NULL, NULL);
        item.pszText = (LPWSTR)(wk ? wk : L"");
        SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.iSubItem = 1;
        item.pszText = (LPWSTR)(wv ? wv : L"");
        SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&item);
        g_free(wk);
        g_free(wv);
    }
}

static int list_selection(HWND hwnd) {
    return (int)SendMessageW(GetDlgItem(hwnd, IDC_DATA_LIST),
                             LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
}

static void fields_clear(HWND hwnd) {
    gtv_win_set_dlg_item_text(hwnd, IDC_DATA_KEY, "");
    gtv_win_set_dlg_item_text(hwnd, IDC_DATA_VALUE, "");
}

static void fields_from_selection(HWND hwnd) {
    int idx = list_selection(hwnd);
    if (idx < 0) return;
    const gchar *k = NULL, *v = NULL;
    if (!gtv_table_get(show_emoji, (guint)idx, &k, &v)) return;
    gtv_win_set_dlg_item_text(hwnd, IDC_DATA_KEY, k ? k : "");
    gtv_win_set_dlg_item_text(hwnd, IDC_DATA_VALUE, v ? v : "");
}

static void warn(HWND hwnd, const gchar *utf8) {
    gunichar2 *w = g_utf8_to_utf16(utf8, -1, NULL, NULL, NULL);
    gunichar2 *t = g_utf8_to_utf16("GoTV", -1, NULL, NULL, NULL);
    if (w && t) MessageBoxW(hwnd, (LPCWSTR)w, (LPCWSTR)t, MB_OK | MB_ICONWARNING);
    g_free(w);
    g_free(t);
}

static void persist_or_warn(HWND hwnd) {
    GError *err = NULL;
    if (!gtv_table_save(show_emoji, &err)) {
        warn(hwnd, err ? err->message : gtv_i18n("Không lưu được file dữ liệu.", "Could not save data file."));
        g_clear_error(&err);
        return;
    }
    gtv_tables_reload();
}

static void on_save(HWND hwnd) {
    gchar *key = gtv_win_get_dlg_item_text(hwnd, IDC_DATA_KEY, 256);
    gchar *val = gtv_win_get_dlg_item_text(hwnd, IDC_DATA_VALUE, 1024);
    if (key) g_strstrip(key);
    if (val) g_strstrip(val);
    if (!gtv_table_set(show_emoji, key ? key : "", val ? val : "")) {
        warn(hwnd, gtv_i18n("Khóa/giá trị không hợp lệ (khóa không chứa dấu cách hay dấu =, giá trị không rỗng).",
                            "Invalid key/value (key cannot contain spaces or '=', value cannot be empty)."));
    } else {
        persist_or_warn(hwnd);
        list_refresh(hwnd);
    }
    g_free(key);
    g_free(val);
}

static void on_delete(HWND hwnd) {
    int idx = list_selection(hwnd);
    if (idx < 0) return;
    const gchar *k = NULL;
    if (!gtv_table_get(show_emoji, (guint)idx, &k, NULL) || !k) return;
    if (gtv_table_remove(show_emoji, k)) {
        persist_or_warn(hwnd);
        list_refresh(hwnd);
        fields_clear(hwnd);
    }
}

static void on_folder(void) {
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    gunichar2 *w = g_utf8_to_utf16(dir ? dir : "", -1, NULL, NULL, NULL);
    if (w) {
        ShellExecuteW(NULL, L"open", (LPCWSTR)w, NULL, NULL, SW_SHOWNORMAL);
        g_free(w);
    }
    g_free(dir);
}

static INT_PTR CALLBACK DataDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            struct { int id; const char *text; } labels[] = {
                { IDC_DATA_MACRO, gtv_i18n("Macro (gõ tắt)", "Macro (shortcuts)") },
                { IDC_DATA_EMOJI, "Emoji" },
                { IDC_LBL_KEY, gtv_i18n("Từ gõ:", "Shortcut:") },
                { IDC_LBL_VALUE, gtv_i18n("Nội dung:", "Expansion:") },
                { IDC_DATA_SAVE, gtv_i18n("Lưu", "Save") },
                { IDC_DATA_DELETE, gtv_i18n("Xóa", "Delete") },
                { IDC_DATA_FOLDER, gtv_i18n("Mở thư mục", "Open folder") },
                { IDC_DATA_CLOSE, gtv_i18n("Đóng", "Close") },
                { IDC_DATA_NOTE, gtv_i18n("Thay đổi lưu vào file riêng của bạn. App đang mở phải khởi động lại mới nhận bảng mới.",
                                          "Changes are saved to your personal file. Open apps must be restarted to reload the new table.") },
            };
            gtv_win_set_window_text(hwnd, gtv_i18n("GoTV - Dữ liệu", "GoTV - Data"));
            for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
                gtv_win_set_dlg_item_text(hwnd, labels[i].id, labels[i].text);
            }
            show_emoji = FALSE;
            CheckRadioButton(hwnd, IDC_DATA_MACRO, IDC_DATA_EMOJI, IDC_DATA_MACRO);
            SendMessageW(GetDlgItem(hwnd, IDC_DATA_KEY), EM_LIMITTEXT, 255, 0);
            SendMessageW(GetDlgItem(hwnd, IDC_DATA_VALUE), EM_LIMITTEXT, 1023, 0);
            list_setup_columns(hwnd);
            list_refresh(hwnd);
            return TRUE;
        }
        case WM_NOTIFY: {
            LPNMHDR hdr = (LPNMHDR)lParam;
            if (hdr && hdr->idFrom == IDC_DATA_LIST && hdr->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW lv = (LPNMLISTVIEW)lParam;
                if (lv->uNewState & LVIS_SELECTED)
                    fields_from_selection(hwnd);
            }
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_DATA_MACRO || id == IDC_DATA_EMOJI) {
                gboolean emoji = (id == IDC_DATA_EMOJI);
                if (emoji != show_emoji) {
                    show_emoji = emoji;
                    list_refresh(hwnd);
                    fields_clear(hwnd);
                }
                return TRUE;
            }
            /* Row selection arrives via WM_NOTIFY/LVN_ITEMCHANGED. */
            if (id == IDC_DATA_SAVE) { on_save(hwnd); return TRUE; }
            if (id == IDC_DATA_DELETE) { on_delete(hwnd); return TRUE; }
            if (id == IDC_DATA_FOLDER) { on_folder(); return TRUE; }
            if (id == IDC_DATA_CLOSE || id == IDCANCEL) {
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

void gtv_data_show(HWND parent) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    DialogBoxW(hinst, MAKEINTRESOURCEW(IDD_DATA_DIALOG), parent, DataDlgProc);
}
