#include "setup.h"
#include "app.h"
#include "data.h"
#include "tray.h"
#include "startup.h"
#include "keyboard.h"
#include "hotkey.h"
#include "resource.h"
#include "win_utf.h"
#include "internal.h"
#include <commctrl.h>
#include <glib/gstdio.h>

#define WM_GTV_DICT_STATUS (WM_APP + 22)

/* Dictionary panel: version/word-count labels plus a worker that refreshes
 * dict-vi.txt from the GitHub release asset (offline-first: the bundled
 * seed always works, the download only upgrades it). */
typedef struct { int ctl_id; const char *dict_id; const char *label_vi; const char *label_en; } DictPackCtl;

static const DictPackCtl dict_pack_ctls[] = {
    { IDC_CHECK_DICT_VI,     "vi",     "Tiếng Việt", "Vietnamese" },
    { IDC_CHECK_DICT_TSQL,   "tsql",   "T-SQL",      "T-SQL" },
    { IDC_CHECK_DICT_C,      "c",      "C / C++",    "C / C++" },
    { IDC_CHECK_DICT_CSHARP, "csharp", "C# (.NET)",  "C# (.NET)" },
    { IDC_CHECK_DICT_EN,     "en",     "Tiếng Anh",  "English" },
};

static gchar *collect_dicts_from(HWND hwnd, const DictPackCtl *ctls, guint count) {
    GString *buf = g_string_new("");
    for (guint i = 0; i < count; i++) {
        if (IsDlgButtonChecked(hwnd, ctls[i].ctl_id) == BST_CHECKED) {
            if (buf->len > 0) g_string_append_c(buf, ',');
            g_string_append(buf, ctls[i].dict_id);
        }
    }
    if (buf->len == 0) g_string_assign(buf, "vi");
    return g_string_free(buf, FALSE);
}

static gchar *collect_dlg_dicts(HWND hwnd) {
    return collect_dicts_from(hwnd, dict_pack_ctls, G_N_ELEMENTS(dict_pack_ctls));
}

static GtvUiLang get_dlg_lang(HWND hwnd) {
    return (IsDlgButtonChecked(hwnd, IDC_RADIO_LANG_EN) == BST_CHECKED) ? GTV_LANG_EN : GTV_LANG_VI;
}

static void dict_ui_refresh_lang(HWND hwnd, GtvUiLang lang) {
    guint n = gtv_dict_count();
    const gchar *tag = gtv_dict_tag();
    const gchar *langs = gtv_dict_get_langs();
    const gchar *bundled_str = gtv_i18n_s(lang, "cài sẵn", "bundled");

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

    gchar *ver = (n > 0)
        ? g_strdup_printf(gtv_i18n_s(lang, "Từ điển %s · %u từ (%s)", "Dictionary %s · %u words (%s)"),
                          (tag && *tag) ? tag : bundled_str, n, disp_langs)
        : g_strdup(gtv_i18n_s(lang, "Chưa có từ điển", "No dictionary available"));
    gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_VER, ver);
    g_free(ver);
}

static void dict_ui_refresh(HWND hwnd) {
    dict_ui_refresh_lang(hwnd, get_dlg_lang(hwnd));
}

static void setup_apply_language(HWND hwnd, GtvUiLang lang) {
    struct { int id; const char *vi; const char *en; } labels[] = {
        { IDC_GROUP_TYPE, "Kiểu gõ", "Input method" },
        { IDC_RADIO_TELEX, "Telex (gõ lặp: aa -> â, s/f/r/x/j)", "Telex (repeat: aa -> â, s/f/r/x/j)" },
        { IDC_RADIO_VNI, "VNI (số: 1->sắc, 2->huyền, 6->mũ...)", "VNI (number: 1->acute, 2->grave, 6->hat...)" },
        { IDC_GROUP_SWITCH, "Phím chuyển đổi (E/V)", "Switch key (E/V)" },
        { IDC_RADIO_CTRL_SHIFT, "Ctrl + Shift", "Ctrl + Shift" },
        { IDC_RADIO_ALT_Z, "Alt + Z", "Alt + Z" },
        { IDC_GROUP_OPTS, "Tùy chọn", "Options" },
        { IDC_CHECK_MODERN, "Đặt dấu theo chuẩn mới (hòa, thúy thay vì hoà, thuý)", "Modern tone placement (hòa, thúy instead of hoà, thuý)" },
        { IDC_CHECK_SPELL, "Kiểm tra chính tả tiếng Việt", "Vietnamese spell check" },
        { IDC_CHECK_STARTUP, "Khởi động cùng Windows", "Start with Windows" },
        { IDC_GROUP_DICT, "Gợi ý từ điển (offline)", "Dictionary suggestions (offline)" },
        { IDC_CHECK_SUGGEST, "Bật gợi ý từ thông minh", "Enable smart word suggestions" },
        { IDC_LBL_DICT_VER, "Từ điển...", "Dictionary..." },
        { IDC_BTN_DICT_UPDATE, "Cập nhật", "Update" },
        { IDC_BTN_APP_DICTS, "Theo từng ứng dụng...", "Per-app..." },
        { IDC_GROUP_LANG, "Ngôn ngữ giao diện", "Interface language" },
        { IDC_RADIO_LANG_VI, "Tiếng Việt", "Tiếng Việt" },
        { IDC_RADIO_LANG_EN, "English", "English" },
        { IDC_BTN_OK, "Đồng ý", "OK" },
        { IDC_BTN_CANCEL, "Hủy", "Cancel" },
        { IDC_BTN_DATA, "Dữ liệu...", "Data..." },
        { IDC_BTN_KEYBOARD, "Bàn phím...", "Keyboards..." },
    };
    gtv_win_set_window_text(hwnd, gtv_i18n_s(lang, "GoTV - Cài đặt", "GoTV - Settings"));
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
        gtv_win_set_dlg_item_text(hwnd, labels[i].id, gtv_i18n_s(lang, labels[i].vi, labels[i].en));
    }
    for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
        gtv_win_set_dlg_item_text(hwnd, dict_pack_ctls[i].ctl_id,
            gtv_i18n_s(lang, dict_pack_ctls[i].label_vi, dict_pack_ctls[i].label_en));
    }
    dict_ui_refresh_lang(hwnd, lang);
    gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS,
        gtv_i18n_s(lang, "Gợi ý offline 100%, không cần mạng.", "100% offline suggestions, no internet required."));
}

static gpointer dict_update_worker(gpointer data) {
    HWND hwnd = data;
    gboolean updated = gtv_dict_update_check(NULL);
    if (IsWindow(hwnd))
        PostMessage(hwnd, WM_GTV_DICT_STATUS, (WPARAM)updated, 0);
    return NULL;
}

static BOOL CALLBACK SetChildFont(HWND child, LPARAM param) {
    SendMessageW(child, WM_SETFONT, (WPARAM)param, MAKELPARAM(TRUE, 0));
    return TRUE;
}

/* Pin a Vietnamese-capable dialog font at runtime. The template facename
 * depends on the resource compiler, and with DEFAULT_CHARSET the mapper
 * may fall back to a Latin-1-only face (U+0100+ renders as ?/tofu). Forcing
 * Segoe UI + VIETNAMESE_CHARSET keeps template size/weight and guarantees
 * ể/ặ/ố/... render on any system locale. Handle is freed on WM_DESTROY. */
static void pin_dialog_font(HWND hwnd) {
    HFONT current = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    LOGFONTW lf;
    if (!current || !GetObjectW(current, sizeof(lf), &lf))
        return;
    lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);
    lf.lfCharSet = VIETNAMESE_CHARSET;
    HFONT fixed = CreateFontIndirectW(&lf);
    if (!fixed)
        return;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)fixed);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)fixed, MAKELPARAM(TRUE, 0));
    EnumChildWindows(hwnd, SetChildFont, (LPARAM)fixed);
}

static HWND s_hwndSetup = NULL;
static GPtrArray *s_app_rules = NULL;
static GPtrArray *s_combo_app_ids = NULL;

typedef struct {
    gchar *id;
    gchar *label;
} WinAppComboItem;

static void win_app_combo_item_free(gpointer p) {
    WinAppComboItem *it = p;
    if (!it) return;
    g_free(it->id);
    g_free(it->label);
    g_free(it);
}

static gint win_app_combo_item_cmp(gconstpointer a, gconstpointer b) {
    const WinAppComboItem *ia = *(const WinAppComboItem * const *)a;
    const WinAppComboItem *ib = *(const WinAppComboItem * const *)b;
    return g_ascii_strcasecmp(ia->id ? ia->id : "", ib->id ? ib->id : "");
}

typedef struct {
    GHashTable *seen;
    GPtrArray *items;
} WinAppEnumCtx;

static gboolean is_ignored_win_exe(const gchar *id) {
    if (!id || !*id) return TRUE;
    const char * const ignored[] = {
        "explorer.exe", "applicationframehost.exe", "textinputhost.exe",
        "shellexperiencehost.exe", "searchhost.exe", "startmenuexperiencehost.exe",
        "systemsettings.exe", "gotv.exe", "setup.exe", "update.exe", NULL
    };
    for (int i = 0; ignored[i]; i++) {
        if (g_strcmp0(id, ignored[i]) == 0) return TRUE;
    }
    if (g_str_has_prefix(id, "unins")) return TRUE;
    return FALSE;
}

static void win_app_add_item(WinAppEnumCtx *ctx, const gchar *raw_id, const gchar *title) {
    gchar *norm = gtv_app_dict_normalize_id(raw_id);
    if (!norm || !*norm || is_ignored_win_exe(norm)) {
        g_free(norm);
        return;
    }
    if (!g_str_has_suffix(norm, ".exe")) {
        gchar *with_exe = g_strconcat(norm, ".exe", NULL);
        g_free(norm);
        norm = with_exe;
    }
    if (g_hash_table_contains(ctx->seen, norm)) {
        g_free(norm);
        return;
    }
    g_hash_table_insert(ctx->seen, g_strdup(norm), GINT_TO_POINTER(1));
    WinAppComboItem *it = g_new0(WinAppComboItem, 1);
    it->id = norm;
    if (title && *title) {
        gchar *short_title = g_utf8_substring(title, 0, MIN(g_utf8_strlen(title, -1), 42));
        it->label = g_strdup_printf("%s — %s", norm, short_title);
        g_free(short_title);
    } else {
        it->label = g_strdup(norm);
    }
    g_ptr_array_add(ctx->items, it);
}

static BOOL CALLBACK ad_enum_win_cb(HWND w, LPARAM lParam) {
    WinAppEnumCtx *ctx = (WinAppEnumCtx *)lParam;
    if (!IsWindowVisible(w) || GetWindowTextLengthW(w) <= 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (!pid) return TRUE;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return TRUE;
    WCHAR exePath[MAX_PATH];
    DWORD sz = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &sz) && sz > 0) {
        WCHAR wtitle[128] = {0};
        GetWindowTextW(w, wtitle, 127);
        gchar *u8_path = g_utf16_to_utf8((const gunichar2 *)exePath, -1, NULL, NULL, NULL);
        gchar *u8_title = g_utf16_to_utf8((const gunichar2 *)wtitle, -1, NULL, NULL, NULL);
        if (u8_path) win_app_add_item(ctx, u8_path, u8_title);
        g_free(u8_path);
        g_free(u8_title);
    }
    CloseHandle(hProc);
    return TRUE;
}

static void ad_collect_reg_app_paths(WinAppEnumCtx *ctx, HKEY root) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return;
    WCHAR subName[256];
    for (DWORD idx = 0; ; idx++) {
        DWORD len = G_N_ELEMENTS(subName);
        if (RegEnumKeyExW(hKey, idx, subName, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        gchar *u8 = g_utf16_to_utf8((const gunichar2 *)subName, -1, NULL, NULL, NULL);
        if (u8) {
            if (g_str_has_suffix(u8, ".exe") || g_str_has_suffix(u8, ".EXE"))
                win_app_add_item(ctx, u8, NULL);
            g_free(u8);
        }
    }
    RegCloseKey(hKey);
}

static void ad_select_combo_app(HWND hwnd, const gchar *app_id) {
    HWND combo = GetDlgItem(hwnd, IDC_AD_APP);
    if (!combo || !s_combo_app_ids || !app_id || !*app_id) return;
    gchar *norm = gtv_app_dict_normalize_id(app_id);
    if (!norm) return;
    gchar *with_exe = g_str_has_suffix(norm, ".exe") ? g_strdup(norm) : g_strconcat(norm, ".exe", NULL);
    for (guint i = 0; i < s_combo_app_ids->len; i++) {
        const gchar *cid = g_ptr_array_index(s_combo_app_ids, i);
        if (g_strcmp0(cid, norm) == 0 || g_strcmp0(cid, with_exe) == 0) {
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)i, 0);
            g_free(norm);
            g_free(with_exe);
            return;
        }
    }
    /* Not in list yet: append it dynamically */
    gunichar2 *w = g_utf8_to_utf16(norm, -1, NULL, NULL, NULL);
    if (w) {
        LRESULT idx = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)w);
        if (idx >= 0) {
            g_ptr_array_add(s_combo_app_ids, g_strdup(norm));
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)idx, 0);
        }
        g_free(w);
    }
    g_free(norm);
    g_free(with_exe);
}

static void ad_populate_combo(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_AD_APP);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (s_combo_app_ids) g_ptr_array_unref(s_combo_app_ids);
    s_combo_app_ids = g_ptr_array_new_with_free_func(g_free);

    WinAppEnumCtx ctx;
    ctx.seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ctx.items = g_ptr_array_new_with_free_func(win_app_combo_item_free);

    EnumWindows(ad_enum_win_cb, (LPARAM)&ctx);
    ad_collect_reg_app_paths(&ctx, HKEY_CURRENT_USER);
    ad_collect_reg_app_paths(&ctx, HKEY_LOCAL_MACHINE);

    if (s_app_rules) {
        for (guint i = 0; i < s_app_rules->len; i++) {
            const GtvAppDictRule *r = g_ptr_array_index(s_app_rules, i);
            if (r && r->app_id) win_app_add_item(&ctx, r->app_id, NULL);
        }
    }

    g_ptr_array_sort(ctx.items, win_app_combo_item_cmp);
    for (guint i = 0; i < ctx.items->len; i++) {
        WinAppComboItem *it = g_ptr_array_index(ctx.items, i);
        gunichar2 *w = g_utf8_to_utf16(it->label, -1, NULL, NULL, NULL);
        if (w) {
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)w);
            g_ptr_array_add(s_combo_app_ids, g_strdup(it->id));
            g_free(w);
        }
    }
    if (s_combo_app_ids->len > 0) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }

    g_ptr_array_unref(ctx.items);
    g_hash_table_destroy(ctx.seen);
}

static const DictPackCtl app_dict_ctls[] = {
    { IDC_AD_VI,     "vi",     "Tiếng Việt", "Vietnamese" },
    { IDC_AD_TSQL,   "tsql",   "T-SQL",      "T-SQL" },
    { IDC_AD_C,      "c",      "C / C++",    "C / C++" },
    { IDC_AD_CSHARP, "csharp", "C# (.NET)",  "C# (.NET)" },
    { IDC_AD_EN,     "en",     "Tiếng Anh",  "English" },
};

static void ad_warn(HWND hwnd, const char *utf8) {
    gunichar2 *w = g_utf8_to_utf16(utf8 ? utf8 : "", -1, NULL, NULL, NULL);
    gunichar2 *t = g_utf8_to_utf16("GoTV", -1, NULL, NULL, NULL);
    if (w && t) MessageBoxW(hwnd, (LPCWSTR)w, (LPCWSTR)t, MB_OK | MB_ICONWARNING);
    g_free(w);
    g_free(t);
}

static void ad_setup_columns(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_AD_LIST);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    struct { const char *text; int width; } cols[] = {
        { "Ứng dụng", 125 },
        { "Từ điển", 140 },
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

static void ad_refresh_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_AD_LIST);
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    if (!s_app_rules) return;
    for (guint i = 0; i < s_app_rules->len; i++) {
        const GtvAppDictRule *r = g_ptr_array_index(s_app_rules, i);
        LVITEMW item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        gunichar2 *wa = g_utf8_to_utf16(r->app_id ? r->app_id : "", -1, NULL, NULL, NULL);
        gunichar2 *wd = g_utf8_to_utf16(r->dicts ? r->dicts : "", -1, NULL, NULL, NULL);
        item.pszText = (LPWSTR)(wa ? wa : L"");
        SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.iSubItem = 1;
        item.pszText = (LPWSTR)(wd ? wd : L"");
        SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&item);
        g_free(wa);
        g_free(wd);
    }
}

static int ad_selection(HWND hwnd) {
    return (int)SendMessageW(GetDlgItem(hwnd, IDC_AD_LIST),
                             LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
}

static void ad_load_selection(HWND hwnd) {
    int idx = ad_selection(hwnd);
    if (idx < 0 || !s_app_rules || (guint)idx >= s_app_rules->len) return;
    const GtvAppDictRule *r = g_ptr_array_index(s_app_rules, (guint)idx);
    ad_select_combo_app(hwnd, r->app_id);
    for (guint i = 0; i < G_N_ELEMENTS(app_dict_ctls); i++) {
        gboolean on = gtv_dict_lang_enabled(r->dicts, app_dict_ctls[i].dict_id);
        CheckDlgButton(hwnd, app_dict_ctls[i].ctl_id, on ? BST_CHECKED : BST_UNCHECKED);
    }
}

static void ad_save_rules(HWND hwnd) {
    gchar *cfg_dir = gtv_app_config_dir();
    if (cfg_dir) {
        GError *err = NULL;
        if (!gtv_app_dicts_save(s_app_rules, cfg_dir, &err)) {
            ad_warn(hwnd, err ? err->message : "Không lưu được cấu hình.");
            g_clear_error(&err);
        }
        g_free(cfg_dir);
    }
}

static INT_PTR CALLBACK AppDictsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            struct { int id; const char *text; } labels[] = {
                { IDC_AD_LBL_APP, "Ứng dụng:" },
                { IDC_AD_LBL_DICTS, "Từ điển:" },
                { IDC_AD_SAVE, "Thêm / Cập nhật" },
                { IDC_AD_DELETE, "Xóa" },
                { IDC_AD_CLOSE, "Đóng" },
                { IDC_AD_NOTE, "Chọn ứng dụng từ danh sách các ứng dụng đang chạy hoặc đã cài trên máy. Ứng dụng không có quy tắc riêng sẽ dùng danh sách mặc định." },
            };
            gtv_win_set_window_text(hwnd, "Từ điển theo từng ứng dụng");
            for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
                gtv_win_set_dlg_item_text(hwnd, labels[i].id, labels[i].text);
            for (guint i = 0; i < G_N_ELEMENTS(app_dict_ctls); i++) {
                gtv_win_set_dlg_item_text(hwnd, app_dict_ctls[i].ctl_id,
                    gtv_i18n(app_dict_ctls[i].label_vi, app_dict_ctls[i].label_en));
                gboolean on = gtv_dict_lang_enabled(g_app.config.dicts, app_dict_ctls[i].dict_id);
                CheckDlgButton(hwnd, app_dict_ctls[i].ctl_id, on ? BST_CHECKED : BST_UNCHECKED);
            }
            pin_dialog_font(hwnd);
            gchar *cfg_dir = gtv_app_config_dir();
            s_app_rules = gtv_app_dicts_load(cfg_dir);
            g_free(cfg_dir);
            ad_populate_combo(hwnd);
            ad_setup_columns(hwnd);
            ad_refresh_list(hwnd);
            RECT rc, rcOwner;
            GetWindowRect(hwnd, &rc);
            GetWindowRect(GetDesktopWindow(), &rcOwner);
            SetWindowPos(hwnd, HWND_TOP,
                         (rcOwner.right - (rc.right - rc.left)) / 2,
                         (rcOwner.bottom - (rc.bottom - rc.top)) / 2,
                         0, 0, SWP_NOSIZE);
            return TRUE;
        }
        case WM_NOTIFY: {
            LPNMHDR hdr = (LPNMHDR)lParam;
            if (hdr && hdr->idFrom == IDC_AD_LIST && hdr->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW lv = (LPNMLISTVIEW)lParam;
                if (lv->uNewState & LVIS_SELECTED)
                    ad_load_selection(hwnd);
            }
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_AD_SAVE) {
                LRESULT sel = SendMessageW(GetDlgItem(hwnd, IDC_AD_APP), CB_GETCURSEL, 0, 0);
                const gchar *sel_id = (sel >= 0 && s_combo_app_ids && (guint)sel < s_combo_app_ids->len)
                    ? g_ptr_array_index(s_combo_app_ids, (guint)sel) : NULL;
                gchar *norm = gtv_app_dict_normalize_id(sel_id);
                if (!norm) {
                    ad_warn(hwnd, "Vui lòng chọn một ứng dụng từ danh sách.");
                    return TRUE;
                }
                gchar *dicts = collect_dicts_from(hwnd, app_dict_ctls, G_N_ELEMENTS(app_dict_ctls));
                gboolean updated = FALSE;
                for (guint i = 0; i < s_app_rules->len; i++) {
                    GtvAppDictRule *r = g_ptr_array_index(s_app_rules, i);
                    if (g_strcmp0(r->app_id, norm) == 0) {
                        g_free(r->dicts);
                        r->dicts = dicts;
                        updated = TRUE;
                        break;
                    }
                }
                if (!updated) {
                    GtvAppDictRule *r = g_new0(GtvAppDictRule, 1);
                    r->app_id = g_strdup(norm);
                    r->dicts = dicts;
                    g_ptr_array_add(s_app_rules, r);
                }
                g_free(norm);
                ad_save_rules(hwnd);
                ad_refresh_list(hwnd);
                return TRUE;
            } else if (id == IDC_AD_DELETE) {
                int idx = ad_selection(hwnd);
                if (idx >= 0 && s_app_rules && (guint)idx < s_app_rules->len) {
                    g_ptr_array_remove_index(s_app_rules, (guint)idx);
                    ad_save_rules(hwnd);
                    ad_refresh_list(hwnd);
                }
                return TRUE;
            } else if (id == IDC_AD_CLOSE || id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        case WM_DESTROY: {
            if (s_app_rules) {
                g_ptr_array_unref(s_app_rules);
                s_app_rules = NULL;
            }
            if (s_combo_app_ids) {
                g_ptr_array_unref(s_combo_app_ids);
                s_combo_app_ids = NULL;
            }
            HFONT fixed = (HFONT)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (fixed)
                DeleteObject(fixed);
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK SetupDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            s_hwndSetup = hwnd;
            for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
                gboolean on = gtv_dict_lang_enabled(g_app.config.dicts, dict_pack_ctls[i].dict_id);
                CheckDlgButton(hwnd, dict_pack_ctls[i].ctl_id, on ? BST_CHECKED : BST_UNCHECKED);
            }
            pin_dialog_font(hwnd);
            if (g_app.config.mode == GTV_VNI) {
                CheckRadioButton(hwnd, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_VNI);
            } else {
                CheckRadioButton(hwnd, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_TELEX);
            }

            if (g_app.config.switch_key == GTV_SWITCH_ALT_Z) {
                CheckRadioButton(hwnd, IDC_RADIO_CTRL_SHIFT, IDC_RADIO_ALT_Z, IDC_RADIO_ALT_Z);
            } else {
                CheckRadioButton(hwnd, IDC_RADIO_CTRL_SHIFT, IDC_RADIO_ALT_Z, IDC_RADIO_CTRL_SHIFT);
            }

            if (g_app.config.ui_lang == GTV_LANG_EN) {
                CheckRadioButton(hwnd, IDC_RADIO_LANG_VI, IDC_RADIO_LANG_EN, IDC_RADIO_LANG_EN);
            } else {
                CheckRadioButton(hwnd, IDC_RADIO_LANG_VI, IDC_RADIO_LANG_EN, IDC_RADIO_LANG_VI);
            }

            CheckDlgButton(hwnd, IDC_CHECK_MODERN, g_app.config.modern ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_SPELL, g_app.config.spellcheck ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_STARTUP, gtv_startup_get() != GTV_STARTUP_NONE ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_SUGGEST, g_app.config.suggest_enabled ? BST_CHECKED : BST_UNCHECKED);

            setup_apply_language(hwnd, g_app.config.ui_lang);

            /* Center dialog on screen */
            RECT rc, rcOwner;
            GetWindowRect(hwnd, &rc);
            GetWindowRect(GetDesktopWindow(), &rcOwner);
            SetWindowPos(hwnd, HWND_TOP,
                         (rcOwner.right - (rc.right - rc.left)) / 2,
                         (rcOwner.bottom - (rc.bottom - rc.top)) / 2,
                         0, 0, SWP_NOSIZE);
            return TRUE;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_BTN_OK) {
                if (IsDlgButtonChecked(hwnd, IDC_RADIO_VNI) == BST_CHECKED) {
                    g_app.config.mode = GTV_VNI;
                } else {
                    g_app.config.mode = GTV_TELEX;
                }
                GtvSwitchKey new_sk = (IsDlgButtonChecked(hwnd, IDC_RADIO_ALT_Z) == BST_CHECKED)
                    ? GTV_SWITCH_ALT_Z
                    : GTV_SWITCH_CTRL_SHIFT;
                if (g_app.config.switch_key != new_sk) {
                    g_app.config.switch_key = new_sk;
                    gtv_hotkey_set_key(new_sk);
                }
                g_app.config.modern = (IsDlgButtonChecked(hwnd, IDC_CHECK_MODERN) == BST_CHECKED);
                g_app.config.spellcheck = (IsDlgButtonChecked(hwnd, IDC_CHECK_SPELL) == BST_CHECKED);
                g_app.config.suggest_enabled = (IsDlgButtonChecked(hwnd, IDC_CHECK_SUGGEST) == BST_CHECKED);
                g_app.config.ui_lang = get_dlg_lang(hwnd);
                gtv_i18n_set_lang(g_app.config.ui_lang);
                gchar *sel = collect_dlg_dicts(hwnd);
                g_strlcpy(g_app.config.dicts, sel, sizeof(g_app.config.dicts));
                gtv_dict_set_langs(sel);
                g_free(sel);

                gboolean want_startup = (IsDlgButtonChecked(hwnd, IDC_CHECK_STARTUP) == BST_CHECKED);
                if (want_startup) {
                    if (gtv_startup_get() == GTV_STARTUP_NONE)
                        gtv_startup_set_user(TRUE);
                } else if (gtv_startup_get() == GTV_STARTUP_USER) {
                    gtv_startup_set_user(FALSE);
                } else if (gtv_startup_get() == GTV_STARTUP_ADMIN) {
                    /* Removing the admin task needs elevation: UAC appears
                     * from Đồng ý. Cancelling it keeps the task. */
                    gtv_startup_request(hwnd, "admin-off");
                }

                gtv_app_save_config();
                gtv_tray_update_icon();

                EndDialog(hwnd, IDOK);
                return TRUE;
            } else if (id == IDC_CHECK_DICT_VI || id == IDC_CHECK_DICT_TSQL ||
                       id == IDC_CHECK_DICT_C || id == IDC_CHECK_DICT_CSHARP ||
                       id == IDC_CHECK_DICT_EN) {
                gchar *sel = collect_dlg_dicts(hwnd);
                gtv_dict_set_langs(sel);
                g_free(sel);
                dict_ui_refresh(hwnd);
                return TRUE;
            } else if (id == IDC_RADIO_LANG_VI || id == IDC_RADIO_LANG_EN) {
                GtvUiLang new_lang = (id == IDC_RADIO_LANG_EN) ? GTV_LANG_EN : GTV_LANG_VI;
                setup_apply_language(hwnd, new_lang);
                return TRUE;
            } else if (id == IDC_BTN_DICT_UPDATE) {
                gchar *sel = collect_dlg_dicts(hwnd);
                gtv_dict_set_langs(sel);
                g_free(sel);
                HWND btn = GetDlgItem(hwnd, IDC_BTN_DICT_UPDATE);
                if (btn) EnableWindow(btn, FALSE);
                GtvUiLang cur_lang = get_dlg_lang(hwnd);
                gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS,
                    gtv_i18n_s(cur_lang, "Đang kiểm tra từ điển mới trên GitHub...", "Checking for new dictionary on GitHub..."));
                GThread *th = g_thread_new("gtv-dict-update", dict_update_worker, hwnd);
                if (th) {
                    g_thread_unref(th);
                } else {
                    if (btn) EnableWindow(btn, TRUE);
                    gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS,
                        gtv_i18n_s(cur_lang, "Không chạy được tiến trình cập nhật.", "Could not start update worker."));
                }
                return TRUE;
            } else if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            } else if (id == IDC_BTN_DATA) {
                gtv_data_show(hwnd);
                return TRUE;
            } else if (id == IDC_BTN_KEYBOARD) {
                gtv_keyboard_show(hwnd);
                return TRUE;
            } else if (id == IDC_BTN_APP_DICTS) {
                DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_APP_DICTS_DIALOG),
                           hwnd, AppDictsDlgProc);
                return TRUE;
            }
            break;
        }

        case WM_GTV_DICT_STATUS: {
            HWND btn = GetDlgItem(hwnd, IDC_BTN_DICT_UPDATE);
            if (btn) EnableWindow(btn, TRUE);
            dict_ui_refresh(hwnd);
            GtvUiLang cur_lang = get_dlg_lang(hwnd);
            if (wParam) {
                const gchar *tag = gtv_dict_tag();
                gchar *msg = g_strdup_printf(gtv_i18n_s(cur_lang, "Đã cập nhật từ điển %s (%u từ).", "Updated dictionary %s (%u words)."),
                    (tag && *tag) ? tag : "", gtv_dict_count());
                gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS, msg);
                g_free(msg);
            } else {
                gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS,
                    gtv_i18n_s(cur_lang, "Từ điển đã mới nhất (hoặc không có mạng).", "Dictionary is up to date (or offline)."));
            }
            return TRUE;
        }

        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;

        case WM_DESTROY: {
            s_hwndSetup = NULL;
            HFONT fixed = (HFONT)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (fixed)
                DeleteObject(fixed);
            break;
        }
    }
    return FALSE;
}

void gtv_setup_show(HWND parent) {
    if (s_hwndSetup && IsWindow(s_hwndSetup)) {
        SetForegroundWindow(s_hwndSetup);
        return;
    }
    HINSTANCE hinst = GetModuleHandle(NULL);
    DialogBoxW(hinst, MAKEINTRESOURCEW(IDD_SETUP_DIALOG), parent, SetupDlgProc);
}
