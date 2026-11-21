#include "data.h"
#include "app.h"
#include "resource.h"
#include "win_utf.h"
#include "internal.h"
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <glib.h>

typedef struct GpGraphics GpGraphics;
typedef struct GpBrush GpBrush;
typedef struct GpSolidFill GpSolidFill;
typedef struct GpPen GpPen;
typedef struct GpPath GpPath;
typedef float REAL;
typedef DWORD ARGB;
typedef int GpStatus;

typedef struct {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GtvGdipStartupInput;

GpStatus WINAPI GdiplusStartup(ULONG_PTR *token, const GtvGdipStartupInput *input, void *output);
void WINAPI GdiplusShutdown(ULONG_PTR token);
GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
GpStatus WINAPI GdipDeleteGraphics(GpGraphics *graphics);
GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *graphics, int smoothingMode);
GpStatus WINAPI GdipCreateSolidFill(ARGB color, GpSolidFill **brush);
GpStatus WINAPI GdipDeleteBrush(GpBrush *brush);
GpStatus WINAPI GdipCreatePen1(ARGB color, REAL width, int unit, GpPen **pen);
GpStatus WINAPI GdipDeletePen(GpPen *pen);
GpStatus WINAPI GdipCreatePath(int brushMode, GpPath **path);
GpStatus WINAPI GdipDeletePath(GpPath *path);
GpStatus WINAPI GdipAddPathArc(GpPath *path, REAL x, REAL y, REAL w, REAL h, REAL startAngle, REAL sweepAngle);
GpStatus WINAPI GdipClosePathFigure(GpPath *path);
GpStatus WINAPI GdipFillPath(GpGraphics *graphics, GpBrush *brush, GpPath *path);
GpStatus WINAPI GdipDrawPath(GpGraphics *graphics, GpPen *pen, GpPath *path);

typedef struct {
    ARGB bg;
    ARGB card;
    ARGB card_border;
    ARGB row_sel;
    ARGB row_hover;
    ARGB accent;
    ARGB accent_hover;
    ARGB accent_border;
    ARGB btn_bg;
    ARGB btn_hover;
    ARGB btn_border;
    COLORREF text_primary;
    COLORREF text_secondary;
    COLORREF text_muted;
    COLORREF edit_bg;
    COLORREF caption_rgb;
} DataPalette;

static const DataPalette k_data_dark = {
    .bg = 0xFF181617,
    .card = 0xFF211E20,
    .card_border = 0xFF322D30,
    .row_sel = 0xFF2C2628,
    .row_hover = 0xFF272224,
    .accent = 0xFFD32F2F,
    .accent_hover = 0xFFE53935,
    .accent_border = 0xFFE53935,
    .btn_bg = 0xFF2B2729,
    .btn_hover = 0xFF383235,
    .btn_border = 0xFF453E42,
    .text_primary = RGB(245, 242, 240),
    .text_secondary = RGB(172, 163, 160),
    .text_muted = RGB(130, 122, 119),
    .edit_bg = RGB(30, 27, 29),
    .caption_rgb = RGB(24, 22, 23),
};

static const DataPalette k_data_light = {
    .bg = 0xFFFAF7F2,
    .card = 0xFFFFFFFF,
    .card_border = 0xFFE7E5E4,
    .row_sel = 0xFFFEE2E2,
    .row_hover = 0xFFF5F1EB,
    .accent = 0xFFD32F2F,
    .accent_hover = 0xFFB71C1C,
    .accent_border = 0xFFB71C1C,
    .btn_bg = 0xFFF5F5F4,
    .btn_hover = 0xFFE7E5E4,
    .btn_border = 0xFFD6D3D1,
    .text_primary = RGB(43, 35, 32),
    .text_secondary = RGB(87, 83, 78),
    .text_muted = RGB(120, 113, 108),
    .edit_bg = RGB(255, 255, 255),
    .caption_rgb = RGB(250, 247, 242),
};

enum {
    HIT_DATA_NONE = 0,
    HIT_DATA_MACRO = 10,
    HIT_DATA_EMOJI = 11,
    HIT_DATA_SAVE = 12,
    HIT_DATA_DELETE = 13,
    HIT_DATA_FOLDER = 14,
    HIT_DATA_CLOSE = 15,
    HIT_DATA_ROW_BASE = 1000
};

#define IDC_EDIT_KEY   501
#define IDC_EDIT_VALUE 502

typedef struct {
    RECT rc;
    int id;
    gboolean enabled;
} DataHit;

static HWND s_hwndData = NULL;
static HWND s_hwndKey = NULL;
static HWND s_hwndVal = NULL;
static HFONT s_hEditFont = NULL;
static HBRUSH s_hEditBrush = NULL;
static ULONG_PTR s_gdipToken = 0;
static gboolean s_showEmoji = FALSE;
static int s_selIdx = -1;
static int s_scrollPos = 0;
static int s_hoverId = HIT_DATA_NONE;
static RECT s_listRect = {0};
static int s_visibleRows = 1;
static DataHit s_hits[64];
static int s_nHits = 0;

static const DataPalette *data_pal(void) {
    return (g_app.config.ui_theme == GTV_THEME_LIGHT) ? &k_data_light : &k_data_dark;
}

static UINT data_dpi(HWND hwnd) {
    typedef UINT (WINAPI *GetDpiFn)(HWND);
    static GetDpiFn fn = NULL;
    static gboolean probed = FALSE;
    if (!probed) {
        probed = TRUE;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (u32) fn = (GetDpiFn)(void *)GetProcAddress(u32, "GetDpiForWindow");
    }
    if (fn && hwnd) {
        UINT d = fn(hwnd);
        if (d >= 72) return d;
    }
    return 96;
}

static int dsc(int px, UINT dpi) { return MulDiv(px, (int)dpi, 96); }

static void d_fill_rr(GpGraphics *g, REAL x, REAL y, REAL w, REAL h, REAL r, ARGB fill, ARGB border) {
    if (w <= 0 || h <= 0) return;
    GpPath *p = NULL;
    GdipCreatePath(0, &p);
    if (!p) return;
    if (r <= 0.5f) {
        GdipAddPathArc(p, x, y, 1, 1, 180, 90);
        GdipAddPathArc(p, x + w - 1, y, 1, 1, 270, 90);
        GdipAddPathArc(p, x + w - 1, y + h - 1, 1, 1, 0, 90);
        GdipAddPathArc(p, x, y + h - 1, 1, 1, 90, 90);
    } else {
        REAL d = r * 2.0f;
        if (d > w) d = w;
        if (d > h) d = h;
        GdipAddPathArc(p, x, y, d, d, 180, 90);
        GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
        GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
        GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    }
    GdipClosePathFigure(p);
    if ((fill >> 24) != 0) {
        GpSolidFill *b = NULL;
        GdipCreateSolidFill(fill, &b);
        if (b) { GdipFillPath(g, (GpBrush *)b, p); GdipDeleteBrush((GpBrush *)b); }
    }
    if ((border >> 24) != 0) {
        GpPen *pen = NULL;
        GdipCreatePen1(border, 1.0f, 2, &pen);
        if (pen) { GdipDrawPath(g, pen, p); GdipDeletePen(pen); }
    }
    GdipDeletePath(p);
}

static void d_text(HDC hdc, const char *utf8, RECT *rc, COLORREF col, HFONT font, UINT fmt) {
    if (!utf8) return;
    gunichar2 *w = g_utf8_to_utf16(utf8, -1, NULL, NULL, NULL);
    if (!w) return;
    HFONT oldf = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, col);
    DrawTextW(hdc, (LPCWSTR)w, -1, rc, fmt);
    SelectObject(hdc, oldf);
    g_free(w);
}

static HFONT d_font(int pt_px, int weight, UINT dpi) {
    return CreateFontW(-dsc(pt_px, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       VIETNAMESE_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void d_push_hit(RECT rc, int id, gboolean enabled) {
    if (s_nHits < (int)G_N_ELEMENTS(s_hits))
        s_hits[s_nHits++] = (DataHit){ rc, id, enabled };
}

static void d_button(GpGraphics *g, HDC hdc, const DataPalette *pal, RECT rc, const char *label,
                     int id, gboolean primary, gboolean enabled, HFONT font, UINT dpi) {
    gboolean hov = enabled && (s_hoverId == id);
    ARGB bg = primary ? (hov ? pal->accent_hover : pal->accent)
                      : (hov ? pal->btn_hover : pal->btn_bg);
    ARGB bd = primary ? pal->accent_border : pal->btn_border;
    COLORREF tc = primary ? RGB(255, 255, 255) : (enabled ? pal->text_primary : pal->text_muted);
    d_fill_rr(g, (REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top),
              (REAL)dsc(6, dpi), bg, bd);
    d_text(hdc, label, &rc, tc, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    d_push_hit(rc, id, enabled);
}

static void set_edit_text(HWND edit, const char *utf8) {
    if (!edit) return;
    gunichar2 *w = g_utf8_to_utf16(utf8 ? utf8 : "", -1, NULL, NULL, NULL);
    SetWindowTextW(edit, w ? (LPCWSTR)w : L"");
    g_free(w);
}

static gchar *get_edit_text(HWND edit, int max_chars) {
    if (!edit) return g_strdup("");
    wchar_t *buf = g_new0(wchar_t, (gsize)max_chars + 1);
    GetWindowTextW(edit, buf, max_chars);
    gchar *u8 = g_utf16_to_utf8((const gunichar2 *)buf, -1, NULL, NULL, NULL);
    g_free(buf);
    return u8 ? u8 : g_strdup("");
}

static void warn_msg(HWND hwnd, const gchar *utf8) {
    gunichar2 *w = g_utf8_to_utf16(utf8, -1, NULL, NULL, NULL);
    MessageBoxW(hwnd, w ? (LPCWSTR)w : L"", L"GoTV", MB_OK | MB_ICONWARNING);
    g_free(w);
}

static void persist_or_warn(HWND hwnd) {
    GError *err = NULL;
    if (!gtv_table_save(s_showEmoji, &err)) {
        warn_msg(hwnd, err ? err->message : gtv_i18n("Không lưu được file dữ liệu.", "Could not save data file."));
        g_clear_error(&err);
        return;
    }
    gtv_tables_reload();
}

static void on_save(HWND hwnd) {
    gchar *key = get_edit_text(s_hwndKey, 256);
    gchar *val = get_edit_text(s_hwndVal, 1024);
    g_strstrip(key);
    g_strstrip(val);
    if (!gtv_table_set(s_showEmoji, key, val)) {
        warn_msg(hwnd, gtv_i18n("Khóa/giá trị không hợp lệ (khóa không chứa dấu cách hay dấu =, giá trị không rỗng).",
                                "Invalid key/value (key cannot contain spaces or '=', value cannot be empty)."));
    } else {
        persist_or_warn(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    }
    g_free(key);
    g_free(val);
}

static void on_delete(HWND hwnd) {
    if (s_selIdx < 0) return;
    const gchar *k = NULL;
    if (!gtv_table_get(s_showEmoji, (guint)s_selIdx, &k, NULL) || !k) return;
    if (gtv_table_remove(s_showEmoji, k)) {
        persist_or_warn(hwnd);
        s_selIdx = -1;
        set_edit_text(s_hwndKey, "");
        set_edit_text(s_hwndVal, "");
        InvalidateRect(hwnd, NULL, FALSE);
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

static void layout_edits(HWND hwnd, int cw, int ch) {
    UINT dpi = data_dpi(hwnd);
    int pad = dsc(16, dpi);
    int lbl_w = dsc(84, dpi);
    int ed_h = dsc(24, dpi);
    int row2_y = ch - dsc(110, dpi);
    int row1_y = row2_y - dsc(32, dpi);
    int ed_x = pad + lbl_w;
    int ed_w = cw - pad - ed_x;
    if (ed_w < dsc(120, dpi)) ed_w = dsc(120, dpi);
    if (s_hwndKey) MoveWindow(s_hwndKey, ed_x, row1_y, ed_w, ed_h, TRUE);
    if (s_hwndVal) MoveWindow(s_hwndVal, ed_x, row2_y, ed_w, ed_h, TRUE);
}

static void paint_data_window(HWND hwnd, HDC out_hdc, int cw, int ch) {
    UINT dpi = data_dpi(hwnd);
    HDC mem = CreateCompatibleDC(out_hdc);
    HBITMAP bmp = CreateCompatibleBitmap(out_hdc, cw, ch);
    HBITMAP oldb = (HBITMAP)SelectObject(mem, bmp);

    GpGraphics *g = NULL;
    GdipCreateFromHDC(mem, &g);
    if (g) GdipSetSmoothingMode(g, 4);

    const DataPalette *pal = data_pal();
    HFONT f_hdr = d_font(14, FW_SEMIBOLD, dpi);
    HFONT f_txt = d_font(12, FW_NORMAL, dpi);
    HFONT f_sub = d_font(11, FW_NORMAL, dpi);
    HFONT f_btn = d_font(12, FW_MEDIUM, dpi);
    s_nHits = 0;

    d_fill_rr(g, 0, 0, (REAL)cw, (REAL)ch, 0, pal->bg, 0);

    int pad = dsc(16, dpi);
    int y = pad;

    /* Segmented toggle: Macro | Emoji */
    int seg_w = dsc(140, dpi), seg_h = dsc(30, dpi);
    RECT r_macro = { pad, y, pad + seg_w, y + seg_h };
    RECT r_emoji = { pad + seg_w + dsc(8, dpi), y, pad + seg_w * 2 + dsc(8, dpi), y + seg_h };
    d_button(g, mem, pal, r_macro, gtv_i18n("Macro (gõ tắt)", "Macro (shortcuts)"),
             HIT_DATA_MACRO, !s_showEmoji, TRUE, f_btn, dpi);
    d_button(g, mem, pal, r_emoji, "Emoji", HIT_DATA_EMOJI, s_showEmoji, TRUE, f_btn, dpi);
    y += seg_h + dsc(12, dpi);

    /* List card */
    int bottom_reserve = dsc(154, dpi);
    int list_h = ch - y - bottom_reserve;
    if (list_h < dsc(120, dpi)) list_h = dsc(120, dpi);
    s_listRect = (RECT){ pad, y, cw - pad, y + list_h };
    d_fill_rr(g, (REAL)pad, (REAL)y, (REAL)(cw - pad * 2), (REAL)list_h, (REAL)dsc(10, dpi), pal->card, pal->card_border);

    int col_w = (cw - pad * 2) / 3;
    int hdr_h = dsc(28, dpi);
    RECT h_k = { pad + dsc(12, dpi), y + dsc(4, dpi), pad + col_w, y + hdr_h };
    RECT h_v = { pad + col_w + dsc(8, dpi), y + dsc(4, dpi), cw - pad - dsc(12, dpi), y + hdr_h };
    d_text(mem, gtv_i18n("Từ gõ", "Shortcut"), &h_k, pal->text_secondary, f_hdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    d_text(mem, gtv_i18n("Nội dung", "Expansion"), &h_v, pal->text_secondary, f_hdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int row_h = dsc(26, dpi);
    int ry = y + hdr_h + dsc(4, dpi);
    int body_h = (y + list_h - dsc(8, dpi)) - ry;
    s_visibleRows = body_h / row_h;
    if (s_visibleRows < 1) s_visibleRows = 1;

    guint total = gtv_table_count(s_showEmoji);
    int max_scroll = (int)total > s_visibleRows ? (int)total - s_visibleRows : 0;
    if (s_scrollPos > max_scroll) s_scrollPos = max_scroll;
    if (s_scrollPos < 0) s_scrollPos = 0;

    for (int r = 0; r < s_visibleRows && (guint)(s_scrollPos + r) < total; r++) {
        guint idx = (guint)(s_scrollPos + r);
        const gchar *k = NULL, *v = NULL;
        if (!gtv_table_get(s_showEmoji, idx, &k, &v)) continue;
        RECT rr = { pad + dsc(6, dpi), ry, cw - pad - dsc(6, dpi), ry + row_h };
        int hid = HIT_DATA_ROW_BASE + (int)idx;
        if ((int)idx == s_selIdx || s_hoverId == hid) {
            d_fill_rr(g, (REAL)rr.left, (REAL)rr.top, (REAL)(rr.right - rr.left), (REAL)row_h,
                      (REAL)dsc(5, dpi), ((int)idx == s_selIdx) ? pal->row_sel : pal->row_hover, 0);
        }
        RECT rk = { rr.left + dsc(8, dpi), ry, pad + col_w, ry + row_h };
        RECT rv = { pad + col_w + dsc(8, dpi), ry, rr.right - dsc(8, dpi), ry + row_h };
        d_text(mem, k ? k : "", &rk, pal->text_primary, f_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        d_text(mem, v ? v : "", &rv, pal->text_secondary, f_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        d_push_hit(rr, hid, TRUE);
        ry += row_h;
    }

    /* Labels for edits */
    int lbl_w = dsc(80, dpi);
    int row2_y = ch - dsc(110, dpi);
    int row1_y = row2_y - dsc(32, dpi);
    RECT lk = { pad, row1_y, pad + lbl_w, row1_y + dsc(24, dpi) };
    RECT lv = { pad, row2_y, pad + lbl_w, row2_y + dsc(24, dpi) };
    d_text(mem, gtv_i18n("Từ gõ:", "Shortcut:"), &lk, pal->text_primary, f_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    d_text(mem, gtv_i18n("Nội dung:", "Expansion:"), &lv, pal->text_primary, f_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Action buttons */
    int btn_y = ch - dsc(74, dpi);
    int btn_h = dsc(30, dpi);
    int bw1 = dsc(92, dpi), bw2 = dsc(82, dpi), bw3 = dsc(124, dpi), bw4 = dsc(84, dpi);
    int gap = dsc(8, dpi);
    int bx = pad;
    d_button(g, mem, pal, (RECT){ bx, btn_y, bx + bw1, btn_y + btn_h }, gtv_i18n("Lưu", "Save"), HIT_DATA_SAVE, TRUE, TRUE, f_btn, dpi);
    bx += bw1 + gap;
    d_button(g, mem, pal, (RECT){ bx, btn_y, bx + bw2, btn_y + btn_h }, gtv_i18n("Xóa", "Delete"), HIT_DATA_DELETE, FALSE, s_selIdx >= 0, f_btn, dpi);
    bx += bw2 + gap;
    d_button(g, mem, pal, (RECT){ bx, btn_y, bx + bw3, btn_y + btn_h }, gtv_i18n("Mở thư mục", "Open folder"), HIT_DATA_FOLDER, FALSE, TRUE, f_btn, dpi);
    d_button(g, mem, pal, (RECT){ cw - pad - bw4, btn_y, cw - pad, btn_y + btn_h }, gtv_i18n("Đóng", "Close"), HIT_DATA_CLOSE, FALSE, TRUE, f_btn, dpi);

    RECT note_rc = { pad, ch - dsc(36, dpi), cw - pad, ch - dsc(8, dpi) };
    d_text(mem, gtv_i18n("Thay đổi lưu vào file riêng của bạn. App đang mở phải khởi động lại mới nhận bảng mới.",
                         "Changes are saved to your personal file. Open apps must be restarted to reload the new table."),
           &note_rc, pal->text_muted, f_sub, DT_LEFT | DT_WORDBREAK);

    BitBlt(out_hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
    DeleteObject(f_hdr);
    DeleteObject(f_txt);
    DeleteObject(f_sub);
    DeleteObject(f_btn);
    if (g) GdipDeleteGraphics(g);
    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static int d_find_hit(POINT pt) {
    for (int i = s_nHits - 1; i >= 0; i--) {
        if (s_hits[i].enabled && PtInRect(&s_hits[i].rc, pt))
            return s_hits[i].id;
    }
    return HIT_DATA_NONE;
}

static LRESULT CALLBACK DataWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            GtvGdipStartupInput in = { 1, NULL, FALSE, FALSE };
            GdiplusStartup(&s_gdipToken, &in, NULL);
            BOOL dark = (g_app.config.ui_theme != GTV_THEME_LIGHT);
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            COLORREF cap = data_pal()->caption_rgb;
            DwmSetWindowAttribute(hwnd, 35, &cap, sizeof(cap));

            UINT dpi = data_dpi(hwnd);
            s_hEditFont = d_font(12, FW_NORMAL, dpi);
            s_hEditBrush = CreateSolidBrush(data_pal()->edit_bg);
            s_hwndKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                        0, 0, 100, 24, hwnd, (HMENU)(INT_PTR)IDC_EDIT_KEY,
                                        GetModuleHandle(NULL), NULL);
            s_hwndVal = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                        0, 0, 100, 24, hwnd, (HMENU)(INT_PTR)IDC_EDIT_VALUE,
                                        GetModuleHandle(NULL), NULL);
            SendMessageW(s_hwndKey, WM_SETFONT, (WPARAM)s_hEditFont, TRUE);
            SendMessageW(s_hwndVal, WM_SETFONT, (WPARAM)s_hEditFont, TRUE);
            SendMessageW(s_hwndKey, EM_LIMITTEXT, 255, 0);
            SendMessageW(s_hwndVal, EM_LIMITTEXT, 1023, 0);
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            const DataPalette *pal = data_pal();
            SetTextColor(hdc, pal->text_primary);
            SetBkColor(hdc, pal->edit_bg);
            return (LRESULT)s_hEditBrush;
        }
        case WM_SIZE: {
            RECT cr;
            GetClientRect(hwnd, &cr);
            layout_edits(hwnd, cr.right, cr.bottom);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO *mmi = (MINMAXINFO *)lParam;
            UINT dpi = data_dpi(hwnd);
            RECT r = { 0, 0, dsc(480, dpi), dsc(420, dpi) };
            AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
            mmi->ptMinTrackSize.x = r.right - r.left;
            mmi->ptMinTrackSize.y = r.bottom - r.top;
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT cr;
            GetClientRect(hwnd, &cr);
            if (cr.right > 0 && cr.bottom > 0)
                paint_data_window(hwnd, hdc, cr.right, cr.bottom);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            guint total = gtv_table_count(s_showEmoji);
            int max_scroll = (int)total > s_visibleRows ? (int)total - s_visibleRows : 0;
            s_scrollPos -= (delta / WHEEL_DELTA) * 3;
            if (s_scrollPos < 0) s_scrollPos = 0;
            if (s_scrollPos > max_scroll) s_scrollPos = max_scroll;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int hid = d_find_hit(pt);
            if (hid != s_hoverId) {
                s_hoverId = hid;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            SetCursor(LoadCursorW(NULL, (LPCWSTR)(hid != HIT_DATA_NONE ? IDC_HAND : IDC_ARROW)));
            return 0;
        }
        case WM_MOUSELEAVE:
            if (s_hoverId != HIT_DATA_NONE) {
                s_hoverId = HIT_DATA_NONE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int hid = d_find_hit(pt);
            if (hid == HIT_DATA_MACRO || hid == HIT_DATA_EMOJI) {
                gboolean emoji = (hid == HIT_DATA_EMOJI);
                if (emoji != s_showEmoji) {
                    s_showEmoji = emoji;
                    s_selIdx = -1;
                    s_scrollPos = 0;
                    set_edit_text(s_hwndKey, "");
                    set_edit_text(s_hwndVal, "");
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (hid == HIT_DATA_SAVE) {
                on_save(hwnd);
            } else if (hid == HIT_DATA_DELETE) {
                on_delete(hwnd);
            } else if (hid == HIT_DATA_FOLDER) {
                on_folder();
            } else if (hid == HIT_DATA_CLOSE) {
                DestroyWindow(hwnd);
            } else if (hid >= HIT_DATA_ROW_BASE) {
                int idx = hid - HIT_DATA_ROW_BASE;
                const gchar *k = NULL, *v = NULL;
                if (gtv_table_get(s_showEmoji, (guint)idx, &k, &v)) {
                    s_selIdx = idx;
                    set_edit_text(s_hwndKey, k ? k : "");
                    set_edit_text(s_hwndVal, v ? v : "");
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            s_hwndData = NULL;
            s_hwndKey = NULL;
            s_hwndVal = NULL;
            if (s_hEditFont) { DeleteObject(s_hEditFont); s_hEditFont = NULL; }
            if (s_hEditBrush) { DeleteObject(s_hEditBrush); s_hEditBrush = NULL; }
            if (s_gdipToken) { GdiplusShutdown(s_gdipToken); s_gdipToken = 0; }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void gtv_data_show(HWND parent) {
    (void)parent;
    if (s_hwndData && IsWindow(s_hwndData)) {
        if (IsIconic(s_hwndData)) ShowWindow(s_hwndData, SW_RESTORE);
        SetForegroundWindow(s_hwndData);
        return;
    }
    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DataWndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"GoTVDataWindow";
    RegisterClassExW(&wc);

    UINT dpi = data_dpi(NULL);
    int w = dsc(560, dpi), h = dsc(520, dpi);
    RECT desk = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desk, 0);
    int x = desk.left + ((desk.right - desk.left) - w) / 2;
    int y = desk.top + ((desk.bottom - desk.top) - h) / 2;

    RECT wr = { 0, 0, w, h };
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);

    s_showEmoji = FALSE;
    s_selIdx = -1;
    s_scrollPos = 0;
    s_hoverId = HIT_DATA_NONE;
    gunichar2 *wtitle = g_utf8_to_utf16(gtv_i18n("GoTV — Dữ liệu", "GoTV — Data"), -1, NULL, NULL, NULL);
    s_hwndData = CreateWindowExW(0, L"GoTVDataWindow", (LPCWSTR)(wtitle ? wtitle : L"GoTV"),
                                 WS_OVERLAPPEDWINDOW, x, y, wr.right - wr.left, wr.bottom - wr.top,
                                 NULL, NULL, hinst, NULL);
    g_free(wtitle);
    if (s_hwndData) {
        ShowWindow(s_hwndData, SW_SHOW);
        SetForegroundWindow(s_hwndData);
        UpdateWindow(s_hwndData);
    }
}
