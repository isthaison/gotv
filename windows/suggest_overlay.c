#include "suggest_overlay.h"
#include "engine.h"
#include "suggest_ipc.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define GTV_SUGGEST_OVERLAY_CLASS L"GoTVSuggestionOverlay"

typedef struct {
    HWND hwnd;
    HFONT font;
    UINT font_dpi; /* DPI s_overlay.font was built for, 0 = none */
    UINT dpi;      /* DPI of the monitor the overlay currently sits on */
    DWORD source_pid;
    DWORD generation;
    DWORD count;
    DWORD selected;
    WCHAR candidates[GTV_SUGGEST_IPC_MAX_CANDIDATES]
                    [GTV_SUGGEST_IPC_CANDIDATE_BYTES];
} GtvSuggestOverlay;

static GtvSuggestOverlay s_overlay;

/* Misplaced caret is invisible from the outside, so leave a short trail in
 * %APPDATA%\gotv\suggest.log. Enough to tell a bad rect arriving from
 * the text service apart from bad placement done here. Bounded size. */
static void overlay_log(const gchar *fmt, ...) {
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    g_mkdir_with_parents(dir, 0755);
    gchar *path = g_build_filename(dir, "suggest.log", NULL);
    g_free(dir);

    FILE *probe = g_fopen(path, "r");
    if (probe) {
        fseek(probe, 0, SEEK_END);
        if (ftell(probe) > 65536) {
            fclose(probe);
            g_remove(path);
            probe = NULL;
        } else fclose(probe);
    }

    FILE *f = g_fopen(path, "a");
    g_free(path);
    if (!f) return;
    GDateTime *now = g_date_time_new_now_local();
    gchar *ts = now ? g_date_time_format(now, "%Y-%m-%d %H:%M:%S") : g_strdup("?");
    fprintf(f, "[%s] ", ts);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
    g_free(ts);
    if (now) g_date_time_unref(now);
}

/* Real DPI of the monitor the caret is on. The tray process is per-monitor
 * aware, so this is not virtualised: the popup is built at the same scale as
 * the host app instead of the system one. GetDpiForWindow is resolved at
 * runtime (Windows 10 1607+); GetDeviceCaps is the fallback. */
static UINT overlay_dpi(void) {
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn get_dpi_for_window = NULL;
    static int probed = 0;
    if (!probed) {
        probed = 1;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        FARPROC proc = user32
            ? GetProcAddress(user32, "GetDpiForWindow")
            : NULL;
        /* GetProcAddress has no typed variant; go through a void* to keep
         * -Wall from flagging the function-pointer cast. */
        *(void **)&get_dpi_for_window = (void *)proc;
    }

    UINT dpi = 0;
    HWND focus = GetForegroundWindow();
    if (get_dpi_for_window && focus && IsWindow(focus))
        dpi = get_dpi_for_window(focus);
    if (dpi < 48) {
        HDC dc = GetDC(NULL);
        dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc) ReleaseDC(NULL, dc);
    }
    return dpi >= 48 ? dpi : 96;
}

static HFONT overlay_font(UINT dpi) {
    if (s_overlay.font && s_overlay.font_dpi == dpi)
        return s_overlay.font;
    if (s_overlay.font) {
        DeleteObject(s_overlay.font);
        s_overlay.font = NULL;
        s_overlay.font_dpi = 0;
    }
    s_overlay.font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                 L"Segoe UI");
    s_overlay.font_dpi = s_overlay.font ? dpi : 0;
    return s_overlay.font;
}

static int overlay_row_height(HDC dc) {
    TEXTMETRICW metrics;
    HFONT font = overlay_font(s_overlay.dpi);
    HFONT old = font ? (HFONT)SelectObject(dc, font) : NULL;
    int height = GetTextMetricsW(dc, &metrics) ? metrics.tmHeight + 8 : 24;
    if (old) SelectObject(dc, old);
    return height;
}

static void overlay_measure(int *width, int *height) {
    HDC dc = GetDC(s_overlay.hwnd);
    int max_width = MulDiv(140, (int)s_overlay.dpi, 96);
    int row_height = MulDiv(24, (int)s_overlay.dpi, 96);
    if (dc) {
        row_height = overlay_row_height(dc);
        HFONT font = overlay_font(s_overlay.dpi);
        HFONT old = font ? (HFONT)SelectObject(dc, font) : NULL;
        for (DWORD i = 0; i < s_overlay.count; i++) {
            SIZE size;
            if (GetTextExtentPoint32W(dc, s_overlay.candidates[i],
                                      (int)wcslen(s_overlay.candidates[i]), &size) &&
                size.cx + MulDiv(36, (int)s_overlay.dpi, 96) > max_width)
                max_width = size.cx + MulDiv(36, (int)s_overlay.dpi, 96);
        }
        if (old) SelectObject(dc, old);
        ReleaseDC(s_overlay.hwnd, dc);
    }
    int max_px = MulDiv(340, (int)s_overlay.dpi, 96);
    *width = max_width > max_px ? max_px : max_width;
    *height = (int)s_overlay.count * row_height + MulDiv(4, (int)s_overlay.dpi, 96);
}

/* Position every show, not only the anomalies. A popup that is inside the
     * focused window can still be visually wrong, and logging only failures
     * hid exactly that case for three releases. Throttled to one line per
     * second: this runs on the tray UI thread once per keystroke. */
static void overlay_log_show(DWORD caret_source, RECT caret, RECT fr,
                             int x, int y, int width, int height) {
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (last && now - last < 1000) return;
    last = now;
    const char *names[] = { "none", "selection", "composition", "win32", "cached" };
    const char *src = (caret_source < 5) ? names[caret_source] : "?";
    overlay_log("show src=%s caret=%ld,%ld,%ld,%ld focus=%ld,%ld,%ld,%ld "
                "popup=%d,%d %dx%d dpi=%u",
                src,
                (long)caret.left, (long)caret.top,
                (long)caret.right, (long)caret.bottom,
                (long)fr.left, (long)fr.top,
                (long)fr.right, (long)fr.bottom, x, y, width, height,
                (unsigned)s_overlay.dpi);
}

static void overlay_show(RECT caret, DWORD caret_source) {
    UINT dpi = overlay_dpi();
    if (dpi != s_overlay.dpi) {
        overlay_log("dpi %u -> %u", (unsigned)s_overlay.dpi, (unsigned)dpi);
        s_overlay.dpi = dpi;
    }

    int width, height;
    overlay_measure(&width, &height);

    HMONITOR monitor = MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info;
    info.cbSize = sizeof(info);
    BOOL have_work = FALSE;
    RECT work;
    /* Only clamp when the work area is known. Defaulting it to the caret rect
     * made the clamp below compare the popup against itself, which pinned it
     * to the caret's own top-left corner. */
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        work = info.rcWork;
        have_work = TRUE;
    }

    int x = caret.left;
    int y = caret.bottom + 2;
    if (have_work) {
        /* Flip above the caret, but only when the popup genuinely fits there:
         * clamping to work.top afterwards used to undo the flip and leave the
         * popup far away from the caret. */
        if (y + height > work.bottom) {
            int above = caret.top - height - 2;
            if (above >= work.top)
                y = above;
            else if (work.bottom - height >= work.top)
                y = work.bottom - height;
        }
        if (x + width > work.right) x = work.right - width;
        if (x < work.left) x = work.left;
        if (y < work.top) y = work.top;
        if (y + height > work.bottom) y = work.bottom - height;
    }

    /* Log where the popup went and why. `src` says which probe produced the
     * rect, `focus` is the window the user is actually typing in: together
     * they separate "the host answered with the wrong box" from "we placed it
     * wrong", which is the one distinction a silent popup cannot make. */
    HWND focus = GetForegroundWindow();
    RECT fr;
    if (focus && GetWindowRect(focus, &fr)) {
        overlay_log_show(caret_source, caret, fr, x, y, width, height);

        /* A rect that misses the focused window entirely is produced upstream
         * in the text service -- the clearest sign of a wrong position. */
        RECT probe = caret;
        if (probe.right == probe.left) probe.right = probe.left + 1;
        if (probe.bottom == probe.top) probe.bottom = probe.top + 1;
        RECT hit;
        if (!IntersectRect(&hit, &probe, &fr)) {
            overlay_log("ANOMALY caret outside focus window "
                        "%ld,%ld,%ld,%ld -> popup at %d,%d dpi %u",
                        (long)caret.left, (long)caret.top,
                        (long)caret.right, (long)caret.bottom, x, y,
                        (unsigned)s_overlay.dpi);
        }
    }

    SetWindowPos(s_overlay.hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(s_overlay.hwnd, NULL, TRUE);
}

static void overlay_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, (HBRUSH)(COLOR_WINDOW + 1));
    FrameRect(dc, &client, (HBRUSH)(COLOR_WINDOWFRAME + 1));

    int row_height = overlay_row_height(dc);
    HFONT font = overlay_font(s_overlay.dpi);
    HFONT old = font ? (HFONT)SelectObject(dc, font) : NULL;
    SetBkMode(dc, TRANSPARENT);
    int gutter = MulDiv(2, (int)s_overlay.dpi, 96);
    int indent = MulDiv(6, (int)s_overlay.dpi, 96);
    for (DWORD i = 0; i < s_overlay.count; i++) {
        WCHAR label[GTV_SUGGEST_IPC_CANDIDATE_BYTES + 8];
        RECT row = { gutter, gutter + (int)i * row_height, client.right - gutter,
                     gutter + ((int)i + 1) * row_height };
        if (i == s_overlay.selected) {
            FillRect(dc, &row, (HBRUSH)(COLOR_HIGHLIGHT + 1));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }
        swprintf(label, sizeof(label) / sizeof(label[0]), L"%ls",
                 s_overlay.candidates[i]);
        label[(sizeof(label) / sizeof(label[0])) - 1] = L'\0';
        row.left += indent;
        DrawTextW(dc, label, -1, &row,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (old) SelectObject(dc, old);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK overlay_wndproc(HWND hwnd, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    (void)lparam;
    switch (message) {
        case WM_PAINT:
            overlay_paint(hwnd);
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

BOOL gtv_suggest_overlay_init(HINSTANCE instance, HWND owner) {
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = overlay_wndproc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = GTV_SUGGEST_OVERLAY_CLASS;
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;

    s_overlay.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                     GTV_SUGGEST_OVERLAY_CLASS, L"GoTV",
                                     WS_POPUP, 0, 0, 1, 1, owner, NULL, instance, NULL);
    if (s_overlay.hwnd) s_overlay.dpi = overlay_dpi();
    return s_overlay.hwnd != NULL;
}

void gtv_suggest_overlay_cleanup(void) {
    if (s_overlay.hwnd && IsWindow(s_overlay.hwnd)) DestroyWindow(s_overlay.hwnd);
    s_overlay.hwnd = NULL;
    if (s_overlay.font) DeleteObject(s_overlay.font);
    s_overlay.font = NULL;
    s_overlay.font_dpi = 0;
    s_overlay.dpi = 0;
    s_overlay.source_pid = 0;
    s_overlay.generation = 0;
    s_overlay.count = 0;
    s_overlay.selected = 0;
}

static BOOL overlay_decode_candidate(const char *input, WCHAR *output) {
    const char *nul = (const char *)memchr(input, '\0', GTV_SUGGEST_IPC_CANDIDATE_BYTES);
    if (!nul || nul == input) return FALSE;
    int bytes = (int)(nul - input) + 1;
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, bytes,
                                    output, GTV_SUGGEST_IPC_CANDIDATE_BYTES);
    return chars > 0;
}

BOOL gtv_suggest_overlay_handle_copydata(const COPYDATASTRUCT *copydata) {
    if (!s_overlay.hwnd || !copydata || copydata->dwData != GTV_SUGGEST_COPYDATA_ID ||
        copydata->cbData != sizeof(GtvSuggestIpcPayload) || !copydata->lpData)
        return FALSE;

    const GtvSuggestIpcPayload *payload = (const GtvSuggestIpcPayload *)copydata->lpData;
    if (payload->version != GTV_SUGGEST_IPC_VERSION || !payload->source_pid ||
        !payload->generation || payload->candidate_count > GTV_SUGGEST_IPC_MAX_CANDIDATES)
        return FALSE;

    if (payload->command == GTV_SUGGEST_IPC_HIDE) {
        if (payload->candidate_count != 0 || payload->source_pid != s_overlay.source_pid ||
            payload->generation < s_overlay.generation)
            return FALSE;
        s_overlay.generation = payload->generation;
        s_overlay.count = 0;
        ShowWindow(s_overlay.hwnd, SW_HIDE);
        return TRUE;
    }
    if (payload->command != GTV_SUGGEST_IPC_SHOW || payload->candidate_count == 0)
        return FALSE;
    if (payload->source_pid == s_overlay.source_pid &&
        payload->generation < s_overlay.generation)
        return FALSE;

    WCHAR candidates[GTV_SUGGEST_IPC_MAX_CANDIDATES][GTV_SUGGEST_IPC_CANDIDATE_BYTES];
    for (DWORD i = 0; i < payload->candidate_count; i++) {
        if (!overlay_decode_candidate(payload->candidates[i], candidates[i])) return FALSE;
    }

    s_overlay.source_pid = payload->source_pid;
    s_overlay.generation = payload->generation;
    s_overlay.count = payload->candidate_count;
    s_overlay.selected = payload->selected < payload->candidate_count
                         ? payload->selected : 0;
    memcpy(s_overlay.candidates, candidates, sizeof(candidates));
    overlay_show(payload->caret_screen, payload->caret_source);
    return TRUE;
}
