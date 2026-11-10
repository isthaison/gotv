#include "app.h"
#include "tray.h"
#include "tray_ai.h"
#include "suggest_ipc.h"
#include "suggest_overlay.h"
#include "setup.h"
#include "startup.h"
#include "tsf_install.h"
#include "input_setup.h"
#include "update.h"
#include "keyboard.h"
#include "hotkey.h"
#include "resource.h"
#include "version.h"
#include "internal.h"
#include <windows.h>
#include <commctrl.h>
#include <glib.h>

GtvWindowsApp g_app = {0};

static const char *WINDOW_CLASS_NAME = GTV_TRAY_WINDOW_CLASS;
/* Stable cross-version protocol: old trays must receive GoTV takeover. */
static const char *MUTEX_NAME = "GoTiengViet_Single_Instance_Mutex";
static const WCHAR *TAKEOVER_EVENT_NAME = L"GoTiengViet_Takeover_Event";

/* Updates never replace running files: a new payload only asks the running
 * tray to exit through this event, then takes over the single-instance
 * mutex. No app is ever force-closed for an update. */
static void gtv_takeover_signal(void) {
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, TAKEOVER_EVENT_NAME);
    if (ev) {
        SetEvent(ev);
        CloseHandle(ev);
    }
}

static DWORD WINAPI takeover_watcher(LPVOID param) {
    HWND hwnd = (HWND)param;
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, TAKEOVER_EVENT_NAME);
    if (!ev) return 1;
    ResetEvent(ev);
    for (;;) {
        if (WaitForSingleObject(ev, INFINITE) != WAIT_OBJECT_0) break;
        PostMessageA(hwnd, WM_CLOSE, 0, 0);
    }
    CloseHandle(ev);
    return 0;
}

static UINT s_uTaskbarRestartMsg = 0;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (s_uTaskbarRestartMsg != 0 && msg == s_uTaskbarRestartMsg) {
        gtv_tray_init(hwnd);
        return 0;
    }

    switch (msg) {
        case WM_TRAY_CALLBACK: {
            if (lParam == WM_LBUTTONUP) {
                GtvMode new_mode = (g_app.config.mode == GTV_TELEX) ? GTV_VNI : GTV_TELEX;
                gtv_app_set_input_method(new_mode);
            } else if (lParam == WM_RBUTTONUP) {
                gtv_tray_show_menu(hwnd);
            } else if (lParam == NIN_BALLOONUSERCLICK) {
                if (!gtv_update_balloon_clicked())
                    gtv_tray_apply_pending();
            }
            break;
        }
        case WM_GTV_TOGGLE_INPUT:
        case WM_HOTKEY: {
            gtv_kbd_toggle_active();
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_TRAY_SETTINGS) {
                gtv_setup_show(hwnd);
            }
            break;
        }
        case WM_GTV_AI_RESULT: {
            GtvAiResult *res = (GtvAiResult *)lParam;
            if (res) {
                gtv_tray_suggest_balloon(res->typed, res->fix);
                g_free(res->typed); g_free(res->fix); g_free(res);
            }
            break;
        }
        case WM_GTV_UPDATE_RESULT: {
            GtvUpdateResult *res = (GtvUpdateResult *)lParam;
            gtv_update_on_result(res, (gboolean)wParam);
            break;
        }
        case WM_GTV_UPDATE_DOWNLOADED: {
            gtv_update_on_downloaded((gchar *)lParam);
            break;
        }
        case WM_COPYDATA: {
            PCOPYDATASTRUCT pcds = (PCOPYDATASTRUCT)lParam;
            if (pcds && pcds->dwData == GTV_SUGGEST_COPYDATA_ID)
                return gtv_suggest_overlay_handle_copydata(pcds);
            /* Committed word from gtv_tsf.dll for the AI typo check. */
            if (pcds && pcds->dwData == GTV_AI_COPYDATA_ID && pcds->cbData > 1
                && pcds->cbData <= 256 && pcds->lpData
                && ((const char *)pcds->lpData)[pcds->cbData - 1] == '\0') {
                gchar *word = g_strndup((const char *)pcds->lpData, pcds->cbData - 1);
                if (g_utf8_validate(word, -1, NULL)) gtv_tray_ai_word(word);
                else g_free(word);
            }
            return TRUE;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)nCmdShow;

    /* Elevated startup worker: apply and exit before any window/mutex.
     * Forms: --configure-startup admin|admin-off|user (from gtv_startup_request). */
    /* Elevated one-time TSF worker: full machine registration of the
     * keyboard profiles/categories (HKLM writes need admin).
     * Launched elevated, e.g.: gotv.exe --register-tsf */
    if (lpCmdLine && !strncmp(lpCmdLine, "--register-tsf", 14)) {
        gboolean ok = gtv_tsf_register_elevated();
        return ok ? 0 : 1;
    }
    if (lpCmdLine && !strncmp(lpCmdLine, "--configure-startup", 19)) {
        const char *op = lpCmdLine + 19;
        while (*op == ' ') op++;
        return gtv_startup_apply(op);
    }
    (void)lpCmdLine;

    /* Ensure the thread is attached to the interactive desktop "Default",
     * where Explorer.exe and the system tray notification area reside. */
    {
        HDESK hDesk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
        if (hDesk) {
            SetThreadDesktop(hDesk);
            CloseDesktop(hDesk);
        }
    }

    /* Themed common controls (listview etc.) need explicit init. */
    {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);
    }

    /* 1. Single-instance check (+ takeover for updates) */
    gboolean takeover = lpCmdLine && strstr(lpCmdLine, "--takeover") != NULL;
    gboolean quit_only = lpCmdLine && strstr(lpCmdLine, "--quit") != NULL;
    if (takeover || quit_only) gtv_takeover_signal();
    if (quit_only) return 0;

    /* 0. Stale-entry self-heal: shortcuts/Run/tasks may point at an older
     * payload. When current.txt names a newer version, spawn it (with
     * takeover so a running older tray hands over) and exit. The
     * --forwarded marker guarantees at most one hop (a botched payload
     * whose binary disagrees with current.txt can never fork-bomb). */
    {
        WCHAR wself[MAX_PATH];
        gboolean forwarded = lpCmdLine && strstr(lpCmdLine, "--forwarded") != NULL;
        if (!forwarded && GetModuleFileNameW(NULL, wself, MAX_PATH)) {
            gchar *self = g_utf16_to_utf8((const gunichar2 *)wself, -1, NULL, NULL, NULL);
            gchar *appdir = gtv_app_dir_for_module(self);
            gchar *target = gtv_forward_target(appdir, GTV_VERSION);
            g_free(appdir);
            g_free(self);
            if (target) {
                gunichar2 *wtarget = g_utf8_to_utf16(target, -1, NULL, NULL, NULL);
                gunichar2 *wtail = g_utf8_to_utf16(lpCmdLine ? lpCmdLine : "", -1, NULL, NULL, NULL);
                const gunichar2 *t = wtail;
                while (t && (*t == ' ' || *t == '\t')) t++;
                gboolean has_takeover = lpCmdLine && strstr(lpCmdLine, "--takeover") != NULL;
                size_t cmdlen = (wtarget ? wcslen((const WCHAR *)wtarget) : 0)
                              + (t ? wcslen((const WCHAR *)t) : 0) + 64;
                WCHAR *cmd = g_new0(WCHAR, cmdlen);
                swprintf(cmd, cmdlen, L"\"%ls\" --forwarded%ls%ls%ls",
                         wtarget ? (const WCHAR *)wtarget : L"",
                         has_takeover ? L"" : L" --takeover",
                         (t && *t) ? L" " : L"",
                         t ? (const WCHAR *)t : L"");
                STARTUPINFOW si;
                PROCESS_INFORMATION pi;
                ZeroMemory(&si, sizeof(si));
                si.cb = sizeof(si);
                ZeroMemory(&pi, sizeof(pi));
                if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0,
                                   NULL, NULL, &si, &pi)) {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
                g_free(cmd);
                g_free(wtarget);
                g_free(wtail);
                g_free(target);
                return 0;
            }
        }
    }

    HANDLE hMutex = CreateMutexA(NULL, TRUE, MUTEX_NAME);
    gboolean primary = GetLastError() != ERROR_ALREADY_EXISTS;
    if (takeover && !primary) {
        /* Give the previous payload a moment to honour the signal. */
        for (int i = 0; i < 100 && !primary; i++) {
            Sleep(100);
            if (hMutex) CloseHandle(hMutex);
            hMutex = CreateMutexA(NULL, TRUE, MUTEX_NAME);
            primary = GetLastError() != ERROR_ALREADY_EXISTS;
        }
    }
    if (!primary) {
        HWND existing = FindWindowA(WINDOW_CLASS_NAME, NULL);
        if (existing) {
            SetForegroundWindow(existing);
            PostMessage(existing, WM_COMMAND, MAKEWPARAM(ID_TRAY_SETTINGS, 0), 0);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    /* 1b. Drop superseded ver\ payloads (keeps current + newest rollback)
     * and heal a stale Run value to this exe. Only the instance holding
     * the mutex does this, and only inside versioned layouts. */
    {
        WCHAR wself[MAX_PATH];
        if (GetModuleFileNameW(NULL, wself, MAX_PATH)) {
            gchar *self = g_utf16_to_utf8((const gunichar2 *)wself, -1, NULL, NULL, NULL);
            gchar *appdir = gtv_app_dir_for_module(self);
            gtv_ver_cleanup(appdir);
            gtv_legacy_cleanup(appdir);
            g_free(appdir);
            g_free(self);
        }
    }
    gtv_startup_repoint();

    /* 2. Ensure this installation owns the per-user TSF registration. */
    gtv_tsf_install_ensure_registered();

    /* 2b. Attach GoTV to Vietnamese (Win+Space switcher).
     * Async worker; setup-revision stamped, additive-only. */
#ifndef GTV_STORE_BUILD
    gtv_input_setup_ensure_async();
#endif

    /* 3. Initialize GoTV config */
    gtv_init();

    gchar *config_dir = gtv_user_path(g_get_user_config_dir(), NULL);
    gtv_config_load(&g_app.config, config_dir);
    g_free(config_dir);

    /* 4. Register message window class */
    s_uTaskbarRestartMsg = RegisterWindowMessageA("TaskbarCreated");

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassExA(&wc);

    g_app.hwnd_main = CreateWindowExA(0, WINDOW_CLASS_NAME, "GoTV Hidden Window",
                                       WS_POPUP, 0, 0, 0, 0,
                                       NULL, NULL, hInstance, NULL);

    /* Allow WM_COPYDATA from lower-integrity processes (e.g. sandboxed Chromium/Electron renderers) */
    {
#ifndef MSGFLT_ALLOW
#define MSGFLT_ALLOW 1
#endif
        typedef BOOL (WINAPI *pfnChangeWindowMessageFilterEx)(HWND, UINT, DWORD, PCHANGEFILTERSTRUCT);
        HMODULE hUser32 = GetModuleHandleA("user32.dll");
        pfnChangeWindowMessageFilterEx pChangeFilter =
            hUser32 ? (pfnChangeWindowMessageFilterEx)(void *)GetProcAddress(hUser32, "ChangeWindowMessageFilterEx") : NULL;
        if (pChangeFilter) {
            pChangeFilter(g_app.hwnd_main, WM_COPYDATA, MSGFLT_ALLOW, NULL);
        }
    }

    gtv_suggest_overlay_init(hInstance, g_app.hwnd_main);

    /* Takeover requests from newer payloads arrive on this event. */
    {
        HANDLE hWatcher = CreateThread(NULL, 0, takeover_watcher,
                                       g_app.hwnd_main, 0, NULL);
        if (hWatcher) CloseHandle(hWatcher);
    }

    /* 5. Initialize Tray Icon & Global Hotkey */
    gtv_tray_init(g_app.hwnd_main);
    gtv_hotkey_init(g_app.hwnd_main, g_app.config.switch_key);

    /* Store submissions are updated through their versioned Store listing. */
#ifndef GTV_STORE_BUILD
    /* Daily self-update check against GitHub releases (throttled). */
    gtv_update_check_async(g_app.hwnd_main, FALSE);
#endif

    /* 6. Main Message Loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    /* 7. Cleanup */
    gtv_hotkey_cleanup();
    gtv_suggest_overlay_cleanup();
    gtv_tray_cleanup();
    gtv_config_clear(&g_app.config);

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}
