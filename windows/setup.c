#include "setup.h"
#include "app.h"
#include "data.h"
#include "tray.h"
#include "startup.h"
#include "keyboard.h"
#include "resource.h"
#include "win_utf.h"
#include "internal.h"
#include <glib/gstdio.h>

#define WM_GTV_DICT_STATUS (WM_APP + 22)

/* Dictionary panel: version/word-count labels plus a worker that refreshes
 * dict-vi.txt from the GitHub release asset (offline-first: the bundled
 * seed always works, the download only upgrades it). */
static const struct { int ctl_id; const char *dict_id; const char *label; } dict_pack_ctls[] = {
    { IDC_CHECK_DICT_VI,     "vi",     "Tiếng Việt" },
    { IDC_CHECK_DICT_TSQL,   "tsql",   "T-SQL" },
    { IDC_CHECK_DICT_C,      "c",      "C / C++" },
    { IDC_CHECK_DICT_CSHARP, "csharp", "C# (.NET)" },
    { IDC_CHECK_DICT_EN,     "en",     "Tiếng Anh" },
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

static void dict_ui_refresh(HWND hwnd) {
    guint n = gtv_dict_count();
    const gchar *tag = gtv_dict_tag();
    const gchar *langs = gtv_dict_get_langs();
    gchar *ver = (n > 0)
        ? g_strdup_printf("Từ điển %s · %u từ (%s)", (tag && *tag) ? tag : "cài sẵn", n,
                          (langs && *langs) ? langs : "vi")
        : g_strdup("Chưa có từ điển");
    gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_VER, ver);
    g_free(ver);
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

static INT_PTR CALLBACK SetupDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            /* Every visible string is set here in UTF-16 converted from
             * UTF-8: the .rc file carries ASCII placeholders only, so the
             * dialog renders Vietnamese correctly no matter which codepage
             * the resource compiler assumed. */
            struct { int id; const char *text; } labels[] = {
                { IDC_GROUP_TYPE, "Kiểu gõ" },
                { IDC_RADIO_TELEX, "Telex (gõ lặp: aa -> â, s/f/r/x/j)" },
                { IDC_RADIO_VNI, "VNI (số: 1->sắc, 2->huyền, 6->mũ...)" },
                { IDC_GROUP_OPTS, "Tùy chọn" },
                { IDC_CHECK_MODERN, "Đặt dấu theo chuẩn mới (oà, uỳ thay vì òa, ùy)" },
                { IDC_CHECK_SPELL, "Kiểm tra chính tả && Gợi ý từ thông minh" },
                { IDC_CHECK_STARTUP, "Khởi động cùng Windows" },
                { IDC_GROUP_DICT, "Gợi ý từ điển (offline)" },
                { IDC_CHECK_SUGGEST, "Bật gợi ý từ thông minh" },
                { IDC_LBL_DICT_VER, "Từ điển..." },
                { IDC_BTN_DICT_UPDATE, "Cập nhật" },
                { IDC_LBL_DICT_STATUS, "Trạng thái từ điển..." },
                { IDC_BTN_OK, "Đồng ý" },
                { IDC_BTN_CANCEL, "Hủy" },
                { IDC_BTN_DATA, "Dữ liệu..." },
                { IDC_BTN_KEYBOARD, "Bàn phím..." },
            };
            gtv_win_set_window_text(hwnd, "GoTV - Cài đặt");
            for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
                gtv_win_set_dlg_item_text(hwnd, labels[i].id, labels[i].text);
            }
            for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
                gtv_win_set_dlg_item_text(hwnd, dict_pack_ctls[i].ctl_id, dict_pack_ctls[i].label);
                gboolean on = gtv_dict_lang_enabled(g_app.config.dicts, dict_pack_ctls[i].dict_id);
                CheckDlgButton(hwnd, dict_pack_ctls[i].ctl_id, on ? BST_CHECKED : BST_UNCHECKED);
            }
            pin_dialog_font(hwnd);
            if (g_app.config.mode == GTV_VNI) {
                CheckRadioButton(hwnd, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_VNI);
            } else {
                CheckRadioButton(hwnd, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_TELEX);
            }

            CheckDlgButton(hwnd, IDC_CHECK_MODERN, g_app.config.modern ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_SPELL, g_app.config.spellcheck ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_STARTUP, gtv_startup_get() != GTV_STARTUP_NONE ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_SUGGEST, g_app.config.suggest_enabled ? BST_CHECKED : BST_UNCHECKED);
            dict_ui_refresh(hwnd);
            gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS,
                "Gợi ý offline 100%, không cần mạng.");

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
                g_app.config.modern = (IsDlgButtonChecked(hwnd, IDC_CHECK_MODERN) == BST_CHECKED);
                g_app.config.spellcheck = (IsDlgButtonChecked(hwnd, IDC_CHECK_SPELL) == BST_CHECKED);
                g_app.config.suggest_enabled = (IsDlgButtonChecked(hwnd, IDC_CHECK_SUGGEST) == BST_CHECKED);
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
            } else if (id == IDC_BTN_DICT_UPDATE) {
                gchar *sel = collect_dlg_dicts(hwnd);
                gtv_dict_set_langs(sel);
                g_free(sel);
                HWND btn = GetDlgItem(hwnd, IDC_BTN_DICT_UPDATE);
                if (btn) EnableWindow(btn, FALSE);
                gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS,
                    "Đang kiểm tra từ điển mới trên GitHub...");
                GThread *th = g_thread_new("gtv-dict-update", dict_update_worker, hwnd);
                if (th) {
                    g_thread_unref(th);
                } else {
                    if (btn) EnableWindow(btn, TRUE);
                    gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS,
                        "Không chạy được tiến trình cập nhật.");
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
            if (wParam) {
                const gchar *tag = gtv_dict_tag();
                gchar *msg = g_strdup_printf("Đã cập nhật từ điển %s (%u từ).",
                    (tag && *tag) ? tag : "", gtv_dict_count());
                gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS, msg);
                g_free(msg);
            } else {
                gtv_win_set_dlg_item_text(hwnd, IDC_LBL_DICT_STATUS,
                    "Từ điển đã mới nhất (hoặc không có mạng).");
            }
            return TRUE;
        }

        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;

        case WM_DESTROY: {
            HFONT fixed = (HFONT)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (fixed)
                DeleteObject(fixed);
            break;
        }
    }
    return FALSE;
}

void gtv_setup_show(HWND parent) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    DialogBoxW(hinst, MAKEINTRESOURCEW(IDD_SETUP_DIALOG), parent, SetupDlgProc);
}
