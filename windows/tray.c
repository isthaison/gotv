#include "tray.h"
#include "tray_ai.h"
#include "app.h"
#include "setup.h"
#include "data.h"
#include "startup.h"
#include "keyboard.h"
#include "update.h"
#include "resource.h"
#include "win_utf.h"

static NOTIFYICONDATAW nid = {0};
static HICON icon_current = NULL;

/* Like the Linux indicator (Telex/VNI label): T = Telex, V = VNI.
 * Always "on" — English is a Win+Space keyboard switch, not a tray state. */
static HICON create_tray_text_icon(GtvMode mode) {
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HDC hdcMask = CreateCompatibleDC(hdcScreen);

    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cx, cy);
    HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, NULL);

    HBITMAP hbmOldColor = (HBITMAP)SelectObject(hdcMem, hbmColor);
    HBITMAP hbmOldMask = (HBITMAP)SelectObject(hdcMask, hbmMask);

    RECT rc = {0, 0, cx, cy};

    /* Monochrome 1-bpp mask: 0 (black) = opaque, 1 (white) = transparent */
    HBRUSH hbrBlack = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(hdcMask, &rc, hbrBlack);

    HBRUSH hbr = CreateSolidBrush(RGB(178, 34, 34));
    FillRect(hdcMem, &rc, hbr);
    DeleteObject(hbr);

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    HFONT hFont = CreateFontW(cy - 2, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT hFontOld = (HFONT)SelectObject(hdcMem, hFont);
    DrawTextW(hdcMem, mode == GTV_VNI ? L"V" : L"T",
              -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdcMem, hFontOld);
    DeleteObject(hFont);
    SelectObject(hdcMem, hbmOldColor);
    SelectObject(hdcMask, hbmOldMask);
    DeleteDC(hdcMem);
    DeleteDC(hdcMask);
    ReleaseDC(NULL, hdcScreen);

    ICONINFO ii;
    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    return hIcon;
}

/* Guards g_app.config string fields: the setup dialog swaps them on the UI
 * thread while the AI worker may be copying them. */
static CRITICAL_SECTION config_lock;
static gboolean config_lock_ready = FALSE;
void gtv_config_strings_lock(void) {
    if (!config_lock_ready) {
        InitializeCriticalSection(&config_lock);
        config_lock_ready = TRUE;
    }
    EnterCriticalSection(&config_lock);
}

void gtv_config_strings_unlock(void) {
    LeaveCriticalSection(&config_lock);
}

static void tray_apply_icon_tip(void) {
    if (icon_current) DestroyIcon(icon_current);
    icon_current = create_tray_text_icon(g_app.config.mode);
    if (!icon_current) {
        icon_current = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
    }
    nid.hIcon = icon_current;
    gtv_win_copy_utf8(nid.szTip, G_N_ELEMENTS(nid.szTip),
                      g_app.config.mode == GTV_VNI ? "GoTV — VNI (Click để đổi Telex)" : "GoTV — Telex (Click để đổi VNI)");
}

gboolean gtv_tray_init(HWND hwnd) {
    gtv_tray_ai_init();

    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_CALLBACK;
    tray_apply_icon_tip();
    if (!nid.hIcon) return FALSE;

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        return Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    return TRUE;
}

void gtv_tray_cleanup(void) {
    gtv_tray_ai_cleanup();
    if (config_lock_ready) {
        DeleteCriticalSection(&config_lock);
        config_lock_ready = FALSE;
    }
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (icon_current) DestroyIcon(icon_current);
    icon_current = NULL;
}

void gtv_tray_update_icon(void) {
    nid.uFlags &= (UINT)~NIF_INFO; /* drop stale balloon text on icon updates */
    tray_apply_icon_tip();
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void gtv_tray_show_menu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hmenu = CreatePopupMenu();
    gtv_win_menu_add_utf8(hmenu, "Bảng điều khiển...", 0, ID_TRAY_SETTINGS);
    gtv_win_menu_add_utf8(hmenu, "Quản lý gõ tắt & Emoji...", 0, ID_TRAY_DATA);
    gtv_win_menu_add_utf8(hmenu, "Bàn phím (Win+Space)...", 0, ID_TRAY_KEYBOARDS);
    gtv_win_menu_add_utf8(hmenu, NULL, MF_SEPARATOR, 0);

    UINT telex_flag = (g_app.config.mode == GTV_TELEX) ? MF_CHECKED : MF_UNCHECKED;
    UINT vni_flag = (g_app.config.mode == GTV_VNI) ? MF_CHECKED : MF_UNCHECKED;
    gtv_win_menu_add_utf8(hmenu, "Kiểu gõ Telex", telex_flag, ID_TRAY_MODE_TELEX);
    gtv_win_menu_add_utf8(hmenu, "Kiểu gõ VNI", vni_flag, ID_TRAY_MODE_VNI);

    UINT spell_flag = g_app.config.spellcheck ? MF_CHECKED : MF_UNCHECKED;
    gtv_win_menu_add_utf8(hmenu, "Kiểm tra chính tả", spell_flag, ID_TRAY_SPELLCHECK);

    UINT modern_flag = g_app.config.modern ? MF_CHECKED : MF_UNCHECKED;
    gtv_win_menu_add_utf8(hmenu, "Đặt dấu chuẩn mới", modern_flag, ID_TRAY_MODERN);
    UINT suggest_flag = g_app.config.suggest_enabled ? MF_CHECKED : MF_UNCHECKED;
    gtv_win_menu_add_utf8(hmenu, "Gợi ý từ điển thông minh", suggest_flag, ID_TRAY_SUGGEST);

    /* One startup-mode query per menu open: gtv_startup_get() shells out
     * to schtasks, so calling it twice (once per flag) used to blink two
     * consoles and stall the menu. */
    GtvStartupMode startup_mode = gtv_startup_get();
    UINT start_flag = (startup_mode == GTV_STARTUP_USER) ? MF_CHECKED : MF_UNCHECKED;
    gtv_win_menu_add_utf8(hmenu, "Khởi động cùng Windows", start_flag, ID_TRAY_STARTUP);

#ifndef GTV_STORE_BUILD
    UINT admin_flag = (startup_mode == GTV_STARTUP_ADMIN) ? MF_CHECKED : MF_UNCHECKED;
    gtv_win_menu_add_utf8(hmenu, "Khởi động với quyền admin", admin_flag, ID_TRAY_STARTUP_ADMIN);
#endif

#ifndef GTV_STORE_BUILD
    gtv_win_menu_add_utf8(hmenu, "Kiểm tra cập nhật...", 0, ID_TRAY_UPDATE);
#endif
    gtv_win_menu_add_utf8(hmenu, NULL, MF_SEPARATOR, 0);
    gtv_win_menu_add_utf8(hmenu, "Thoát", 0, ID_TRAY_EXIT);

    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hmenu);

    switch (cmd) {
        case ID_TRAY_SETTINGS:
            gtv_setup_show(hwnd);
            break;
        case ID_TRAY_DATA:
            gtv_data_show(hwnd);
            break;
        case ID_TRAY_KEYBOARDS:
            gtv_keyboard_show(hwnd);
            break;
        case ID_TRAY_MODE_TELEX:
            gtv_app_set_input_method(GTV_TELEX);
            break;
        case ID_TRAY_MODE_VNI:
            gtv_app_set_input_method(GTV_VNI);
            break;
        case ID_TRAY_SPELLCHECK:
            g_app.config.spellcheck = !g_app.config.spellcheck;
            gtv_app_save_config();
            break;
        case ID_TRAY_SUGGEST:
            g_app.config.suggest_enabled = !g_app.config.suggest_enabled;
            gtv_app_save_config();
            break;
        case ID_TRAY_MODERN:
            g_app.config.modern = !g_app.config.modern;
            gtv_app_save_config();
            break;
        case ID_TRAY_STARTUP: {
            GtvStartupMode mode = gtv_startup_get();
            if (mode == GTV_STARTUP_USER) {
                gtv_startup_set_user(FALSE);
            } else if (mode == GTV_STARTUP_ADMIN) {
                /* Dropping the admin task needs elevation too. */
                gtv_startup_request(hwnd, "user");
            } else {
                gtv_startup_set_user(TRUE);
            }
            break;
        }
#ifndef GTV_STORE_BUILD
        case ID_TRAY_STARTUP_ADMIN: {
            GtvStartupMode mode = gtv_startup_get();
            if (mode == GTV_STARTUP_ADMIN)
                gtv_startup_request(hwnd, "admin-off");
            else
                gtv_startup_request(hwnd, "admin");
            break;
        }
        case ID_TRAY_UPDATE:
            gtv_update_check_async(hwnd, TRUE);
            break;
#endif
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
    }
}

/* Tray balloon over the Unicode NOTIFYICONDATAW: UTF-8 goes straight to
 * UTF-16, correct on every system locale (the old ANSI version needed a
 * lossy Windows-1258 conversion). */
static void balloon_show(const gchar *title, const gchar *msg) {
    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    gtv_win_copy_utf8(nid.szInfoTitle, G_N_ELEMENTS(nid.szInfoTitle), title ? title : "GoTV");
    gtv_win_copy_utf8(nid.szInfo, G_N_ELEMENTS(nid.szInfo), msg ? msg : "");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void gtv_tray_balloon(const gchar *title, const gchar *msg) {
    static DWORD last_tick = 0;
    DWORD now = GetTickCount();
    /* One shared slot: a burst of typos must not spam. Update flows
     * use gtv_tray_balloon_force instead so a manual check is never
     * silently swallowed right after an AI balloon. */
    if (last_tick && (now - last_tick) < 10000) return;
    last_tick = now;
    balloon_show(title, msg);
}

void gtv_tray_balloon_force(const gchar *title, const gchar *msg) {
    balloon_show(title, msg);
}
