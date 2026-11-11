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
#include <glib/gstdio.h>

#define WM_GTV_DICT_STATUS (WM_APP + 22)

/* Dictionary panel: version/word-count labels plus a worker that refreshes
 * dict-vi.txt from the GitHub release asset (offline-first: the bundled
 * seed always works, the download only upgrades it). */
static const struct { int ctl_id; const char *dict_id; const char *label_vi; const char *label_en; } dict_pack_ctls[] = {
    { IDC_CHECK_DICT_VI,     "vi",     "Tiếng Việt", "Vietnamese" },
    { IDC_CHECK_DICT_TSQL,   "tsql",   "T-SQL",      "T-SQL" },
    { IDC_CHECK_DICT_C,      "c",      "C / C++",    "C / C++" },
    { IDC_CHECK_DICT_CSHARP, "csharp", "C# (.NET)",  "C# (.NET)" },
    { IDC_CHECK_DICT_EN,     "en",     "Tiếng Anh",  "English" },
};

static gchar *collect_dlg_dicts(HWND hwnd) {
    GString *buf = g_string_new("");
    for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
        if (IsDlgButtonChecked(hwnd, dict_pack_ctls[i].ctl_id) == BST_CHECKED) {
            if (buf->len > 0) g_string_append_c(buf, ',');
            g_string_append(buf, dict_pack_ctls[i].dict_id);
        }
    }
    if (buf->len == 0) g_string_assign(buf, "vi");
    return g_string_free(buf, FALSE);
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
