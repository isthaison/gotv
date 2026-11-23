#include "setup.h"
#include "app.h"
#include "data.h"
#include "tray.h"
#include "startup.h"
#include "keyboard.h"
#include "hotkey.h"
#include "resource.h"
#include "version.h"
#include "win_utf.h"
#include "internal.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
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

static gpointer dict_update_worker(gpointer data) {
    HWND hwnd = data;
    gboolean updated = gtv_dict_update_check(NULL);
    if (IsWindow(hwnd))
        PostMessage(hwnd, WM_GTV_DICT_STATUS, (WPARAM)updated, 0);
    return NULL;
}

static HWND s_hwndSetup = NULL;

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

static guint ad_mask_from_str(const gchar *dicts) {
    guint mask = 0;
    for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
        if (gtv_dict_lang_enabled(dicts, dict_pack_ctls[i].dict_id))
            mask |= (1u << i);
    }
    return mask ? mask : 1u;
}

static gchar *ad_mask_to_str(guint mask) {
    GString *buf = g_string_new("");
    for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
        if (mask & (1u << i)) {
            if (buf->len > 0) g_string_append_c(buf, ',');
            g_string_append(buf, dict_pack_ctls[i].dict_id);
        }
    }
    if (buf->len == 0) g_string_assign(buf, "vi");
    return g_string_free(buf, FALSE);
}

static gchar *ad_format_dicts_label(const gchar *dicts) {
    GString *buf = g_string_new("");
    for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
        if (gtv_dict_lang_enabled(dicts, dict_pack_ctls[i].dict_id)) {
            if (buf->len > 0) g_string_append(buf, ", ");
            g_string_append(buf, gtv_i18n_s(g_app.config.ui_lang,
                                            dict_pack_ctls[i].label_vi,
                                            dict_pack_ctls[i].label_en));
        }
    }
    if (buf->len == 0) g_string_assign(buf, "—");
    return g_string_free(buf, FALSE);
}

/* --------------------------------------------------------------------------
 * Modern dark native Win32 + GDI+ settings window
 * -------------------------------------------------------------------------- */

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
GpStatus WINAPI GdipFillEllipse(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL w, REAL h);

enum {
    NAV_GENERAL = 0,
    NAV_TYPING = 1,
    NAV_DICTS = 2,
    NAV_SHORTCUTS = 3,
    NAV_ABOUT = 4,
    NAV_COUNT = 5
};

enum {
    HIT_NONE = 0,
    HIT_NAV_BASE = 100,       /* + 0..4 */
    HIT_TOGGLE_TELEX = 200,
    HIT_TOGGLE_VNI = 201,
    HIT_TOGGLE_SUGGEST = 202,
    HIT_BTN_DICT_UPDATE = 203,
    HIT_BTN_MACRO_CFG = 204,
    HIT_BTN_EMOJI_CFG = 205,
    HIT_TOGGLE_MODERN = 206,
    HIT_TOGGLE_SPELL = 207,
    HIT_TOGGLE_STARTUP = 208,
    HIT_BTN_LANG_VI = 209,
    HIT_BTN_LANG_EN = 210,
    HIT_TOGGLE_CTRL_SHIFT = 211,
    HIT_TOGGLE_ALT_Z = 212,
    HIT_BTN_KEYBOARD = 213,
    HIT_BTN_APP_DICTS = 214,
    HIT_BTN_OPEN_DIR = 215,
    HIT_BTN_THEME_DARK = 216,
    HIT_BTN_THEME_LIGHT = 217,
    HIT_AD_BACK = 218,
    HIT_AD_PICK_APP = 219,
    HIT_AD_SAVE = 220,
    HIT_AD_DELETE = 221,
    HIT_TOGGLE_DICT_BASE = 300, /* + 0..4 */
    HIT_AD_DICT_BASE = 320,     /* + 0..4 */
    HIT_AD_RULE_BASE = 400      /* + 0..N */
};

typedef struct {
    ARGB sidebar;
    ARGB bg;
    ARGB divider;
    ARGB nav_active;
    ARGB nav_hover;
    ARGB accent;
    ARGB accent_hover;
    ARGB accent_border;
    ARGB card;
    ARGB card_border;
    ARGB toggle_off;
    ARGB toggle_off_hover;
    ARGB toggle_knob;
    ARGB btn_bg;
    ARGB btn_hover;
    ARGB btn_border;
    COLORREF text_primary;
    COLORREF text_secondary;
    COLORREF text_muted;
    COLORREF caption_rgb;
} SetupPalette;

/* Primary accent matches GoTV logo (#E53935 -> #B71C1C) and website (#B91C1C / #FAF7F2). */
static const SetupPalette k_dark_pal = {
    .sidebar = 0xFF131112,
    .bg = 0xFF181617,
    .divider = 0xFF2A2628,
    .nav_active = 0xFF2C2628,
    .nav_hover = 0xFF231F21,
    .accent = 0xFFD32F2F,
    .accent_hover = 0xFFE53935,
    .accent_border = 0xFFEF5350,
    .card = 0xFF211E20,
    .card_border = 0xFF322D30,
    .toggle_off = 0xFF3B3538,
    .toggle_off_hover = 0xFF4A4346,
    .toggle_knob = 0xFFFAF7F2,
    .btn_bg = 0xFF2B2729,
    .btn_hover = 0xFF383235,
    .btn_border = 0xFF453E42,
    .text_primary = RGB(0xF5, 0xF2, 0xF0),
    .text_secondary = RGB(0xAC, 0xA3, 0xA0),
    .text_muted = RGB(0x7E, 0x75, 0x72),
    .caption_rgb = RGB(0x13, 0x11, 0x12),
};

static const SetupPalette k_light_pal = {
    .sidebar = 0xFFF3EFE8,
    .bg = 0xFFFAF7F2,
    .divider = 0xFFE7E5E4,
    .nav_active = 0xFFFEE2E2,
    .nav_hover = 0xFFECE6DC,
    .accent = 0xFFD32F2F,
    .accent_hover = 0xFFB71C1C,
    .accent_border = 0xFFE53935,
    .card = 0xFFFFFFFF,
    .card_border = 0xFFE7E5E4,
    .toggle_off = 0xFFD6D3D1,
    .toggle_off_hover = 0xFFA8A29E,
    .toggle_knob = 0xFFFFFFFF,
    .btn_bg = 0xFFF5F5F4,
    .btn_hover = 0xFFE7E5E4,
    .btn_border = 0xFFD6D3D1,
    .text_primary = RGB(0x2B, 0x23, 0x20),
    .text_secondary = RGB(0x57, 0x53, 0x4E),
    .text_muted = RGB(0x78, 0x71, 0x6C),
    .caption_rgb = RGB(0xF3, 0xEF, 0xE8),
};

static const SetupPalette *current_pal(void) {
    return (g_app.config.ui_theme == GTV_THEME_LIGHT) ? &k_light_pal : &k_dark_pal;
}

typedef struct {
    RECT rc;
    int id;
    gboolean enabled;
} SetupHit;

typedef struct {
    ULONG_PTR gdip_token;
    int active_tab;
    int hover_id;
    gboolean dict_updating;
    gchar *dict_status;
    gboolean app_dicts_mode;
    GPtrArray *ad_rules;
    GPtrArray *ad_apps;
    int ad_sel_app;
    int ad_sel_rule;
    guint ad_mask;
    RECT ad_pick_rc;
    SetupHit hits[64];
    int n_hits;
    HICON app_icon;
} SetupWinState;

static SetupWinState s_sw = {0};

static void ad_load_data(void) {
    if (s_sw.ad_rules) g_ptr_array_unref(s_sw.ad_rules);
    gchar *cfg_dir = gtv_app_config_dir();
    s_sw.ad_rules = gtv_app_dicts_load(cfg_dir);
    g_free(cfg_dir);

    if (s_sw.ad_apps) g_ptr_array_unref(s_sw.ad_apps);
    WinAppEnumCtx ctx;
    ctx.seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ctx.items = g_ptr_array_new_with_free_func(win_app_combo_item_free);
    EnumWindows(ad_enum_win_cb, (LPARAM)&ctx);
    ad_collect_reg_app_paths(&ctx, HKEY_CURRENT_USER);
    ad_collect_reg_app_paths(&ctx, HKEY_LOCAL_MACHINE);
    if (s_sw.ad_rules) {
        for (guint i = 0; i < s_sw.ad_rules->len; i++) {
            const GtvAppDictRule *r = g_ptr_array_index(s_sw.ad_rules, i);
            if (r && r->app_id) win_app_add_item(&ctx, r->app_id, NULL);
        }
    }
    g_ptr_array_sort(ctx.items, win_app_combo_item_cmp);
    s_sw.ad_apps = ctx.items;
    g_hash_table_destroy(ctx.seen);

    s_sw.ad_sel_app = 0;
    s_sw.ad_sel_rule = -1;
    s_sw.ad_mask = ad_mask_from_str(g_app.config.dicts);
}

static void ad_select_app_by_id(const gchar *app_id) {
    if (!s_sw.ad_apps || !app_id || !*app_id) return;
    gchar *norm = gtv_app_dict_normalize_id(app_id);
    if (!norm) return;
    gchar *with_exe = g_str_has_suffix(norm, ".exe") ? g_strdup(norm) : g_strconcat(norm, ".exe", NULL);
    for (guint i = 0; i < s_sw.ad_apps->len; i++) {
        const WinAppComboItem *it = g_ptr_array_index(s_sw.ad_apps, i);
        if (g_strcmp0(it->id, norm) == 0 || g_strcmp0(it->id, with_exe) == 0) {
            s_sw.ad_sel_app = (int)i;
            g_free(norm);
            g_free(with_exe);
            return;
        }
    }
    g_free(norm);
    g_free(with_exe);
}

static void ad_save_rules_now(void) {
    if (!s_sw.ad_rules) return;
    gchar *cfg_dir = gtv_app_config_dir();
    if (cfg_dir) {
        gtv_app_dicts_save(s_sw.ad_rules, cfg_dir, NULL);
        g_free(cfg_dir);
    }
}

static UINT win_dpi(HWND hwnd) {
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn = NULL;
    static gboolean probed = FALSE;
    if (!probed) {
        probed = TRUE;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (u32) fn = (GetDpiForWindowFn)(void *)GetProcAddress(u32, "GetDpiForWindow");
    }
    if (fn && hwnd) {
        UINT d = fn(hwnd);
        if (d >= 72) return d;
    }
    HDC hdc = GetDC(hwnd);
    UINT d = hdc ? (UINT)GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return d ? d : 96;
}

static int sc(int px, UINT dpi) {
    return MulDiv(px, (int)dpi, 96);
}

static void add_round_rect_path(GpPath *path, REAL x, REAL y, REAL w, REAL h, REAL r) {
    REAL d = r * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
    if (d <= 0.5f) {
        GdipAddPathArc(path, x, y, 1, 1, 180, 90);
        GdipAddPathArc(path, x + w - 1, y, 1, 1, 270, 90);
        GdipAddPathArc(path, x + w - 1, y + h - 1, 1, 1, 0, 90);
        GdipAddPathArc(path, x, y + h - 1, 1, 1, 90, 90);
        GdipClosePathFigure(path);
        return;
    }
    GdipAddPathArc(path, x, y, d, d, 180.0f, 90.0f);
    GdipAddPathArc(path, x + w - d, y, d, d, 270.0f, 90.0f);
    GdipAddPathArc(path, x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    GdipAddPathArc(path, x, y + h - d, d, d, 90.0f, 90.0f);
    GdipClosePathFigure(path);
}

static void fill_round_rect(GpGraphics *g, REAL x, REAL y, REAL w, REAL h, REAL r, ARGB fill, ARGB border) {
    GpPath *path = NULL;
    if (GdipCreatePath(0, &path) != 0 || !path) return;
    add_round_rect_path(path, x, y, w, h, r);
    if (fill) {
        GpSolidFill *br = NULL;
        if (GdipCreateSolidFill(fill, &br) == 0 && br) {
            GdipFillPath(g, (GpBrush *)br, path);
            GdipDeleteBrush((GpBrush *)br);
        }
    }
    if (border) {
        GpPen *pen = NULL;
        if (GdipCreatePen1(border, 1.0f, 2, &pen) == 0 && pen) {
            GdipDrawPath(g, pen, path);
            GdipDeletePen(pen);
        }
    }
    GdipDeletePath(path);
}

static void draw_text_u8(HDC hdc, const char *utf8, RECT *rc, COLORREF col, HFONT font, UINT fmt) {
    if (!utf8 || !*utf8) return;
    gunichar2 *w = g_utf8_to_utf16(utf8, -1, NULL, NULL, NULL);
    if (!w) return;
    HFONT oldf = font ? (HFONT)SelectObject(hdc, font) : NULL;
    SetTextColor(hdc, col);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, (LPCWSTR)w, -1, rc, fmt | DT_NOPREFIX);
    if (oldf) SelectObject(hdc, oldf);
    g_free(w);
}

static HFONT make_font(int pt_px, int weight, UINT dpi) {
    LOGFONTW lf = {0};
    lf.lfHeight = -sc(pt_px, dpi);
    lf.lfWeight = weight;
    lf.lfCharSet = VIETNAMESE_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

static void push_hit(RECT rc, int id, gboolean enabled) {
    if (s_sw.n_hits >= (int)G_N_ELEMENTS(s_sw.hits)) return;
    s_sw.hits[s_sw.n_hits++] = (SetupHit){ rc, id, enabled };
}

static void draw_toggle(GpGraphics *g, const SetupPalette *pal, RECT rc, gboolean on, gboolean hovered, gboolean enabled) {
    REAL x = (REAL)rc.left;
    REAL y = (REAL)rc.top;
    REAL w = (REAL)(rc.right - rc.left);
    REAL h = (REAL)(rc.bottom - rc.top);
    REAL r = h / 2.0f;
    ARGB bg = on ? (hovered ? pal->accent_hover : pal->accent)
                 : (hovered ? pal->toggle_off_hover : pal->toggle_off);
    if (!enabled) bg = pal->toggle_off;
    fill_round_rect(g, x, y, w, h, r, bg, 0);

    REAL pad = h * 0.16f;
    REAL kd = h - pad * 2.0f;
    REAL kx = on ? (x + w - pad - kd) : (x + pad);
    GpSolidFill *kbr = NULL;
    if (GdipCreateSolidFill(enabled ? pal->toggle_knob : 0xFFA8A29E, &kbr) == 0 && kbr) {
        GdipFillEllipse(g, (GpBrush *)kbr, kx, y + pad, kd, kd);
        GdipDeleteBrush((GpBrush *)kbr);
    }
}

static void draw_button(GpGraphics *g, HDC hdc, const SetupPalette *pal, RECT rc, const char *label, int id,
                        gboolean active, gboolean enabled, HFONT font, UINT dpi) {
    gboolean hov = enabled && (s_sw.hover_id == id);
    ARGB bg = active ? pal->accent : (hov ? pal->btn_hover : pal->btn_bg);
    ARGB bd = active ? pal->accent_border : pal->btn_border;
    if (!enabled) { bg = pal->btn_bg; bd = pal->card_border; }
    fill_round_rect(g, (REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left),
                    (REAL)(rc.bottom - rc.top), (REAL)sc(6, dpi), bg, bd);
    RECT trc = rc;
    COLORREF tc = !enabled ? pal->text_muted
                : active   ? RGB(0xFF, 0xFF, 0xFF)
                           : pal->text_primary;
    draw_text_u8(hdc, label, &trc, tc, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    push_hit(rc, id, enabled);
}

static RECT draw_card_frame(GpGraphics *g, HDC hdc, const SetupPalette *pal, int x, int y, int w, int h,
                            const char *title, const char *subtitle,
                            HFONT f_title, HFONT f_sub, UINT dpi) {
    fill_round_rect(g, (REAL)x, (REAL)y, (REAL)w, (REAL)h, (REAL)sc(10, dpi), pal->card, pal->card_border);
    int pad = sc(16, dpi);
    if (subtitle && *subtitle) {
        RECT t1 = { x + pad, y + sc(10, dpi), x + w - sc(180, dpi), y + sc(32, dpi) };
        draw_text_u8(hdc, title, &t1, pal->text_primary, f_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT t2 = { x + pad, y + sc(32, dpi), x + w - sc(180, dpi), y + h - sc(8, dpi) };
        draw_text_u8(hdc, subtitle, &t2, pal->text_secondary, f_sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        RECT t1 = { x + pad, y, x + w - sc(180, dpi), y + h };
        draw_text_u8(hdc, title, &t1, pal->text_primary, f_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    RECT card = { x, y, x + w, y + h };
    return card;
}

static const char *tr(const char *vi, const char *en) {
    return gtv_i18n_s(g_app.config.ui_lang, vi, en);
}

static gchar *dict_summary_text(void) {
    guint n = gtv_dict_count();
    const gchar *tag = gtv_dict_tag();
    const gchar *langs = gtv_dict_get_langs();
    const gchar *bundled_str = tr("cài sẵn", "bundled");
    if (n == 0) return g_strdup(tr("Chưa có từ điển", "No dictionary available"));
    return g_strdup_printf(tr("Từ điển %s · %u từ (%s)", "Dictionary %s · %u words (%s)"),
                           (tag && *tag) ? tag : bundled_str, n,
                           (langs && *langs) ? langs : "vi");
}

static void paint_setup_window(HWND hwnd, HDC out_hdc, int cw, int ch) {
    UINT dpi = win_dpi(hwnd);
    HDC mem = CreateCompatibleDC(out_hdc);
    HBITMAP bmp = CreateCompatibleBitmap(out_hdc, cw, ch);
    HBITMAP oldbmp = (HBITMAP)SelectObject(mem, bmp);

    GpGraphics *g = NULL;
    GdipCreateFromHDC(mem, &g);
    GdipSetSmoothingMode(g, 4); /* SmoothingModeAntiAlias */

    HFONT f_brand = make_font(14, FW_SEMIBOLD, dpi);
    HFONT f_h1    = make_font(22, FW_BOLD, dpi);
    HFONT f_nav   = make_font(13, FW_MEDIUM, dpi);
    HFONT f_card  = make_font(13, FW_SEMIBOLD, dpi);
    HFONT f_sub   = make_font(11, FW_NORMAL, dpi);
    HFONT f_btn   = make_font(12, FW_MEDIUM, dpi);

    s_sw.n_hits = 0;

    int sb_w = sc(196, dpi);
    const SetupPalette *pal = current_pal();
    /* Sidebar + Content backgrounds */
    fill_round_rect(g, 0, 0, (REAL)sb_w, (REAL)ch, 0, pal->sidebar, 0);
    fill_round_rect(g, (REAL)sb_w, 0, (REAL)(cw - sb_w), (REAL)ch, 0, pal->bg, 0);
    fill_round_rect(g, (REAL)sb_w, 0, 1.0f, (REAL)ch, 0, pal->divider, 0);

    /* Brand header */
    int hdr_y = sc(16, dpi);
    if (s_sw.app_icon) {
        int isz = sc(22, dpi);
        DrawIconEx(mem, sc(18, dpi), hdr_y + sc(2, dpi), s_sw.app_icon, isz, isz, 0, NULL, DI_NORMAL);
    }
    RECT brc = { sc(48, dpi), hdr_y, sb_w - sc(12, dpi), hdr_y + sc(26, dpi) };
    draw_text_u8(mem, "GoTV", &brc, pal->text_primary, f_brand, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Sidebar items */
    struct { const char *icon; const char *vi; const char *en; } navs[NAV_COUNT] = {
        { "\xE2\x9A\x99", "Tổng quan",  "General" },
        { "\xE2\x8C\xA8", "Kiểu gõ",    "Typing Modes" },
        { "\xF0\x9F\x93\x96", "Từ điển",    "Dictionaries" },
        { "\xE2\x8C\x98", "Phím tắt",   "Shortcuts" },
        { "\xE2\x93\x98", "Thông tin",  "About" },
    };
    int ny = sc(60, dpi);
    int nh = sc(38, dpi);
    int ngap = sc(4, dpi);
    for (int i = 0; i < NAV_COUNT; i++) {
        int id = HIT_NAV_BASE + i;
        RECT nrc = { sc(10, dpi), ny, sb_w - sc(10, dpi), ny + nh };
        gboolean active = (s_sw.active_tab == i);
        gboolean hov = (s_sw.hover_id == id);
        if (active || hov) {
            fill_round_rect(g, (REAL)nrc.left, (REAL)nrc.top,
                            (REAL)(nrc.right - nrc.left), (REAL)(nrc.bottom - nrc.top),
                            (REAL)sc(6, dpi), active ? pal->nav_active : pal->nav_hover, 0);
        }
        if (active) {
            fill_round_rect(g, (REAL)(nrc.left + sc(2, dpi)), (REAL)(nrc.top + sc(9, dpi)),
                            (REAL)sc(3, dpi), (REAL)(nh - sc(18, dpi)),
                            (REAL)sc(2, dpi), pal->accent, 0);
        }
        RECT irc = { nrc.left + sc(12, dpi), nrc.top, nrc.left + sc(34, dpi), nrc.bottom };
        draw_text_u8(mem, navs[i].icon, &irc, active ? RGB(0xE5, 0x39, 0x35) : pal->text_secondary,
                     f_nav, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT trc = { nrc.left + sc(38, dpi), nrc.top, nrc.right - sc(8, dpi), nrc.bottom };
        draw_text_u8(mem, tr(navs[i].vi, navs[i].en), &trc,
                     active ? pal->text_primary : pal->text_secondary,
                     f_nav, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        push_hit(nrc, id, TRUE);
        ny += nh + ngap;
    }

    /* Footer note in sidebar */
    RECT frc = { sc(16, dpi), ch - sc(32, dpi), sb_w - sc(16, dpi), ch - sc(10, dpi) };
    draw_text_u8(mem, "v" GTV_VERSION " · Native", &frc, pal->text_muted, f_sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Right Content Pane */
    int cx = sb_w + sc(28, dpi);
    int cwid = cw - cx - sc(28, dpi);
    int cy = sc(24, dpi);

    const char *page_titles[NAV_COUNT] = {
        tr("Cài đặt GoTV", "GoTV Settings"),
        tr("Kiểu gõ & Chính tả", "Typing Modes"),
        tr("Từ điển & Gợi ý", "Dictionaries"),
        tr("Phím tắt & Bàn phím", "Shortcuts & Keyboards"),
        tr("Thông tin GoTV", "About GoTV"),
    };
    RECT h1rc = { cx, cy, cx + cwid, cy + sc(34, dpi) };
    draw_text_u8(mem, page_titles[s_sw.active_tab], &h1rc, pal->text_primary, f_h1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cy += sc(46, dpi);

    int tw = sc(40, dpi);
    int th = sc(22, dpi);
    int card_h = sc(54, dpi);
    int card_h2 = sc(60, dpi);
    int gap = sc(8, dpi);

    if (s_sw.active_tab == NAV_GENERAL) {
        /* Card 1: Kiểu gõ (Telex / VNI toggles) */
        RECT c1 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Kiểu gõ", "Input method"), NULL, f_card, f_sub, dpi);
        int ty = c1.top + (card_h - th) / 2;
        RECT vni_sw = { c1.right - sc(16, dpi) - tw, ty, c1.right - sc(16, dpi), ty + th };
        RECT vni_lbl = { vni_sw.left - sc(42, dpi), c1.top, vni_sw.left - sc(8, dpi), c1.bottom };
        draw_text_u8(mem, "VNI", &vni_lbl, pal->text_primary, f_nav, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        draw_toggle(g, pal, vni_sw, g_app.config.mode == GTV_VNI, s_sw.hover_id == HIT_TOGGLE_VNI, TRUE);
        push_hit(vni_sw, HIT_TOGGLE_VNI, TRUE);

        RECT tlx_sw = { vni_lbl.left - sc(18, dpi) - tw, ty, vni_lbl.left - sc(18, dpi), ty + th };
        RECT tlx_lbl = { tlx_sw.left - sc(50, dpi), c1.top, tlx_sw.left - sc(8, dpi), c1.bottom };
        draw_text_u8(mem, "Telex", &tlx_lbl, pal->text_primary, f_nav, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        draw_toggle(g, pal, tlx_sw, g_app.config.mode == GTV_TELEX, s_sw.hover_id == HIT_TOGGLE_TELEX, TRUE);
        push_hit(tlx_sw, HIT_TOGGLE_TELEX, TRUE);
        cy += card_h + gap;

        /* Card 2: Từ điển offline */
        gchar *dsub = s_sw.dict_status ? g_strdup(s_sw.dict_status) : dict_summary_text();
        RECT c2 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h2,
                                  tr("Từ điển offline", "Offline dictionary"), dsub, f_card, f_sub, dpi);
        g_free(dsub);
        int bw = sc(126, dpi), bh = sc(28, dpi);
        RECT upd_btn = { c2.right - sc(16, dpi) - bw, c2.top + (card_h2 - bh) / 2, c2.right - sc(16, dpi), c2.top + (card_h2 + bh) / 2 };
        draw_button(g, mem, pal, upd_btn, s_sw.dict_updating ? tr("Đang tải...", "Updating...") : tr("Tải xuống/Cập nhật", "Download/Update"),
                    HIT_BTN_DICT_UPDATE, FALSE, !s_sw.dict_updating, f_btn, dpi);
        RECT sug_sw = { upd_btn.left - sc(14, dpi) - tw, c2.top + (card_h2 - th) / 2, upd_btn.left - sc(14, dpi), c2.top + (card_h2 + th) / 2 };
        draw_toggle(g, pal, sug_sw, g_app.config.suggest_enabled, s_sw.hover_id == HIT_TOGGLE_SUGGEST, TRUE);
        push_hit(sug_sw, HIT_TOGGLE_SUGGEST, TRUE);
        cy += card_h2 + gap;

        /* Card 3: Phím tắt macro */
        RECT c3 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Phím tắt macro", "Macro shortcuts"),
                                  tr("Bấm Tab để bung từ gõ tắt", "Press Tab to expand shortcuts"), f_card, f_sub, dpi);
        int mw = sc(96, dpi);
        RECT mac_btn = { c3.right - sc(16, dpi) - mw, c3.top + (card_h - bh) / 2, c3.right - sc(16, dpi), c3.top + (card_h + bh) / 2 };
        draw_button(g, mem, pal, mac_btn, tr("Cấu hình", "Configure"), HIT_BTN_MACRO_CFG, FALSE, TRUE, f_btn, dpi);
        cy += card_h + gap;

        /* Card 4: Bảng emoji & Ký tự */
        RECT c4 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Bảng emoji & Ký tự", "Emoji & Symbols"),
                                  tr("Quản lý bảng emoji và ký tự đặc biệt", "Manage emoji and symbol shortcuts"), f_card, f_sub, dpi);
        RECT emo_btn = { c4.right - sc(16, dpi) - mw, c4.top + (card_h - bh) / 2, c4.right - sc(16, dpi), c4.top + (card_h + bh) / 2 };
        draw_button(g, mem, pal, emo_btn, tr("Mở bảng...", "Open..."), HIT_BTN_EMOJI_CFG, FALSE, TRUE, f_btn, dpi);
        cy += card_h + gap;

        /* Card 5: Giao diện & Ngôn ngữ */
        RECT c5 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Giao diện & Ngôn ngữ", "Theme & Language"), NULL, f_card, f_sub, dpi);
        int lw = sc(70, dpi), lh = sc(26, dpi), thw = sc(56, dpi);
        int by5 = c5.top + (card_h - lh) / 2;
        RECT en_btn = { c5.right - sc(16, dpi) - lw, by5, c5.right - sc(16, dpi), by5 + lh };
        RECT vi_btn = { en_btn.left - sc(6, dpi) - lw, by5, en_btn.left - sc(6, dpi), by5 + lh };
        RECT lt_btn = { vi_btn.left - sc(14, dpi) - thw, by5, vi_btn.left - sc(14, dpi), by5 + lh };
        RECT dk_btn = { lt_btn.left - sc(6, dpi) - thw, by5, lt_btn.left - sc(6, dpi), by5 + lh };
        draw_button(g, mem, pal, dk_btn, tr("Tối", "Dark"), HIT_BTN_THEME_DARK, g_app.config.ui_theme == GTV_THEME_DARK, TRUE, f_sub, dpi);
        draw_button(g, mem, pal, lt_btn, tr("Sáng", "Light"), HIT_BTN_THEME_LIGHT, g_app.config.ui_theme == GTV_THEME_LIGHT, TRUE, f_sub, dpi);
        draw_button(g, mem, pal, vi_btn, "Tiếng Việt", HIT_BTN_LANG_VI, g_app.config.ui_lang == GTV_LANG_VI, TRUE, f_sub, dpi);
        draw_button(g, mem, pal, en_btn, "English", HIT_BTN_LANG_EN, g_app.config.ui_lang == GTV_LANG_EN, TRUE, f_sub, dpi);
        cy += card_h + gap;

        /* Card 6: Khởi động cùng Windows */
        RECT c6 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Khởi động cùng Windows", "Start with Windows"), NULL, f_card, f_sub, dpi);
        RECT st_sw = { c6.right - sc(16, dpi) - tw, c6.top + (card_h - th) / 2, c6.right - sc(16, dpi), c6.top + (card_h + th) / 2 };
        draw_toggle(g, pal, st_sw, gtv_startup_get() != GTV_STARTUP_NONE, s_sw.hover_id == HIT_TOGGLE_STARTUP, TRUE);
        push_hit(st_sw, HIT_TOGGLE_STARTUP, TRUE);

    } else if (s_sw.active_tab == NAV_TYPING) {
        RECT c1 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h2,
                                  "Telex", "s f r x j, w z, aa ee oo aw ow uw dd, uow → ươ", f_card, f_sub, dpi);
        RECT sw1 = { c1.right - sc(16, dpi) - tw, c1.top + (card_h2 - th) / 2, c1.right - sc(16, dpi), c1.top + (card_h2 + th) / 2 };
        draw_toggle(g, pal, sw1, g_app.config.mode == GTV_TELEX, s_sw.hover_id == HIT_TOGGLE_TELEX, TRUE);
        push_hit(sw1, HIT_TOGGLE_TELEX, TRUE);
        cy += card_h2 + gap;

        RECT c2 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h2,
                                  "VNI", "1 2 3 4 5, 6 7 8 9 0, a6 → â, a8 → ă, o7 → ơ", f_card, f_sub, dpi);
        RECT sw2 = { c2.right - sc(16, dpi) - tw, c2.top + (card_h2 - th) / 2, c2.right - sc(16, dpi), c2.top + (card_h2 + th) / 2 };
        draw_toggle(g, pal, sw2, g_app.config.mode == GTV_VNI, s_sw.hover_id == HIT_TOGGLE_VNI, TRUE);
        push_hit(sw2, HIT_TOGGLE_VNI, TRUE);
        cy += card_h2 + gap;

        RECT c3 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h2,
                                  tr("Đặt dấu kiểu mới", "Modern tone placement"),
                                  tr("hòa, thúy thay vì hoà, thuý (oa, oe, uy)", "hòa, thúy instead of hoà, thuý (oa, oe, uy)"),
                                  f_card, f_sub, dpi);
        RECT sw3 = { c3.right - sc(16, dpi) - tw, c3.top + (card_h2 - th) / 2, c3.right - sc(16, dpi), c3.top + (card_h2 + th) / 2 };
        draw_toggle(g, pal, sw3, g_app.config.modern, s_sw.hover_id == HIT_TOGGLE_MODERN, TRUE);
        push_hit(sw3, HIT_TOGGLE_MODERN, TRUE);
        cy += card_h2 + gap;

        RECT c4 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h2,
                                  tr("Kiểm tra chính tả tiếng Việt", "Vietnamese spell check"),
                                  tr("Phát hiện vần không hợp lệ khi gõ", "Detect invalid Vietnamese syllables while typing"),
                                  f_card, f_sub, dpi);
        RECT sw4 = { c4.right - sc(16, dpi) - tw, c4.top + (card_h2 - th) / 2, c4.right - sc(16, dpi), c4.top + (card_h2 + th) / 2 };
        draw_toggle(g, pal, sw4, g_app.config.spellcheck, s_sw.hover_id == HIT_TOGGLE_SPELL, TRUE);
        push_hit(sw4, HIT_TOGGLE_SPELL, TRUE);

    } else if (s_sw.active_tab == NAV_DICTS && s_sw.app_dicts_mode) {
        int back_w = sc(104, dpi), bh = sc(28, dpi);
        RECT back_rc = { cx, cy - sc(4, dpi), cx + back_w, cy - sc(4, dpi) + bh };
        draw_button(g, mem, pal, back_rc, tr("← Quay lại", "← Back"), HIT_AD_BACK, FALSE, TRUE, f_btn, dpi);
        RECT sub_hdr = { cx + back_w + sc(12, dpi), cy - sc(4, dpi), cx + cwid, cy - sc(4, dpi) + bh };
        draw_text_u8(mem, tr("Từ điển theo từng ứng dụng", "Per-app dictionaries"), &sub_hdr, pal->text_primary, f_card, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cy += bh + sc(10, dpi);

        /* Card 1: App selector + dict pills + Save/Delete */
        int ed_h = sc(146, dpi);
        fill_round_rect(g, (REAL)cx, (REAL)cy, (REAL)cwid, (REAL)ed_h, (REAL)sc(10, dpi), pal->card, pal->card_border);

        const char *app_lbl = tr("(Chưa tìm thấy ứng dụng)", "(No apps found)");
        if (s_sw.ad_apps && s_sw.ad_apps->len > 0) {
            if (s_sw.ad_sel_app < 0 || (guint)s_sw.ad_sel_app >= s_sw.ad_apps->len) s_sw.ad_sel_app = 0;
            const WinAppComboItem *it = g_ptr_array_index(s_sw.ad_apps, (guint)s_sw.ad_sel_app);
            if (it && it->label) app_lbl = it->label;
        }
        gchar *pick_txt = g_strdup_printf("%s  \xE2\x96\xBE", app_lbl);
        RECT pick_rc = { cx + sc(16, dpi), cy + sc(14, dpi), cx + cwid - sc(16, dpi), cy + sc(14, dpi) + sc(32, dpi) };
        s_sw.ad_pick_rc = pick_rc;
        draw_button(g, mem, pal, pick_rc, pick_txt, HIT_AD_PICK_APP, FALSE,
                    (s_sw.ad_apps && s_sw.ad_apps->len > 0), f_nav, dpi);
        g_free(pick_txt);

        /* Dict pills row */
        int px = cx + sc(16, dpi);
        int py = cy + sc(56, dpi);
        int pill_h = sc(28, dpi);
        int pill_gap = sc(8, dpi);
        int pill_w = (cwid - sc(32, dpi) - pill_gap * 4) / 5;
        for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
            RECT pr = { px, py, px + pill_w, py + pill_h };
            gboolean active = (s_sw.ad_mask & (1u << i)) != 0;
            draw_button(g, mem, pal, pr, tr(dict_pack_ctls[i].label_vi, dict_pack_ctls[i].label_en),
                        HIT_AD_DICT_BASE + (int)i, active, TRUE, f_sub, dpi);
            px += pill_w + pill_gap;
        }

        /* Action buttons */
        int save_w = sc(142, dpi), del_w = sc(96, dpi), act_y = cy + sc(98, dpi), act_h = sc(32, dpi);
        RECT save_rc = { cx + sc(16, dpi), act_y, cx + sc(16, dpi) + save_w, act_y + act_h };
        draw_button(g, mem, pal, save_rc, tr("Thêm / Cập nhật", "Add / Update"), HIT_AD_SAVE, TRUE,
                    (s_sw.ad_apps && s_sw.ad_apps->len > 0), f_btn, dpi);
        RECT del_rc = { cx + sc(16, dpi) + save_w + sc(10, dpi), act_y, cx + sc(16, dpi) + save_w + sc(10, dpi) + del_w, act_y + act_h };
        draw_button(g, mem, pal, del_rc, tr("Xóa", "Delete"), HIT_AD_DELETE, FALSE,
                    (s_sw.ad_sel_rule >= 0), f_btn, dpi);
        cy += ed_h + gap;

        /* Card 2: Rules list */
        int list_h = ch - cy - sc(18, dpi);
        if (list_h < sc(120, dpi)) list_h = sc(120, dpi);
        fill_round_rect(g, (REAL)cx, (REAL)cy, (REAL)cwid, (REAL)list_h, (REAL)sc(10, dpi), pal->card, pal->card_border);
        RECT lhdr = { cx + sc(16, dpi), cy + sc(10, dpi), cx + cwid - sc(16, dpi), cy + sc(32, dpi) };
        draw_text_u8(mem, tr("Quy tắc đã lưu (bấm vào dòng để sửa hoặc xóa)", "Saved rules (click a row to edit or delete)"),
                     &lhdr, pal->text_primary, f_card, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int ry = cy + sc(38, dpi);
        int row_h = sc(28, dpi);
        if (!s_sw.ad_rules || s_sw.ad_rules->len == 0) {
            RECT empty_rc = { cx + sc(16, dpi), ry, cx + cwid - sc(16, dpi), ry + row_h };
            draw_text_u8(mem, tr("Chưa có quy tắc riêng. Mọi ứng dụng đang dùng từ điển mặc định.",
                                 "No custom rules yet. All apps use the default dictionary packs."),
                         &empty_rc, pal->text_muted, f_sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            for (guint i = 0; i < s_sw.ad_rules->len && i < 24; i++) {
                if (ry + row_h > cy + list_h - sc(8, dpi)) break;
                const GtvAppDictRule *r = g_ptr_array_index(s_sw.ad_rules, i);
                int hid = HIT_AD_RULE_BASE + (int)i;
                RECT rr = { cx + sc(10, dpi), ry, cx + cwid - sc(10, dpi), ry + row_h };
                if ((int)i == s_sw.ad_sel_rule || s_sw.hover_id == hid) {
                    fill_round_rect(g, (REAL)rr.left, (REAL)rr.top, (REAL)(rr.right - rr.left), (REAL)row_h,
                                    (REAL)sc(6, dpi), ((int)i == s_sw.ad_sel_rule) ? pal->nav_active : pal->nav_hover, 0);
                }
                RECT app_rc = { rr.left + sc(8, dpi), ry, rr.left + sc(190, dpi), ry + row_h };
                draw_text_u8(mem, r->app_id ? r->app_id : "", &app_rc, pal->text_primary, f_nav, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                gchar *dtxt = ad_format_dicts_label(r->dicts);
                RECT dic_rc = { rr.left + sc(196, dpi), ry, rr.right - sc(8, dpi), ry + row_h };
                draw_text_u8(mem, dtxt, &dic_rc, pal->text_secondary, f_sub, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                g_free(dtxt);
                push_hit(rr, hid, TRUE);
                ry += row_h + sc(2, dpi);
            }
        }

    } else if (s_sw.active_tab == NAV_DICTS) {
        RECT c1 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Bật gợi ý từ thông minh", "Enable smart word suggestions"),
                                  tr("Hoạt động 100% offline", "100% offline suggestions"), f_card, f_sub, dpi);
        RECT sw1 = { c1.right - sc(16, dpi) - tw, c1.top + (card_h - th) / 2, c1.right - sc(16, dpi), c1.top + (card_h + th) / 2 };
        draw_toggle(g, pal, sw1, g_app.config.suggest_enabled, s_sw.hover_id == HIT_TOGGLE_SUGGEST, TRUE);
        push_hit(sw1, HIT_TOGGLE_SUGGEST, TRUE);
        cy += card_h + gap;

        int packs_h = sc(164, dpi);
        fill_round_rect(g, (REAL)cx, (REAL)cy, (REAL)cwid, (REAL)packs_h, (REAL)sc(10, dpi), pal->card, pal->card_border);
        RECT p_hdr = { cx + sc(16, dpi), cy + sc(10, dpi), cx + cwid - sc(16, dpi), cy + sc(32, dpi) };
        draw_text_u8(mem, tr("Gói từ điển đã chọn", "Selected dictionary packs"), &p_hdr, pal->text_primary, f_card, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int py = cy + sc(36, dpi);
        int rh = sc(24, dpi);
        for (guint i = 0; i < G_N_ELEMENTS(dict_pack_ctls); i++) {
            const char *lbl = tr(dict_pack_ctls[i].label_vi, dict_pack_ctls[i].label_en);
            RECT lrc = { cx + sc(16, dpi), py, cx + cwid - sc(80, dpi), py + rh };
            draw_text_u8(mem, lbl, &lrc, pal->text_secondary, f_nav, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT psw = { cx + cwid - sc(16, dpi) - tw, py + sc(1, dpi), cx + cwid - sc(16, dpi), py + sc(1, dpi) + th };
            gboolean on = gtv_dict_lang_enabled(g_app.config.dicts, dict_pack_ctls[i].dict_id);
            int hid = HIT_TOGGLE_DICT_BASE + (int)i;
            draw_toggle(g, pal, psw, on, s_sw.hover_id == hid, TRUE);
            push_hit(psw, hid, TRUE);
            py += rh;
        }
        cy += packs_h + gap;

        gchar *dsum = dict_summary_text();
        RECT c3 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h2,
                                  dsum, s_sw.dict_status ? s_sw.dict_status : tr("Cập nhật từ GitHub Release", "Update from GitHub Release"),
                                  f_card, f_sub, dpi);
        g_free(dsum);
        int bw = sc(128, dpi), bh = sc(30, dpi);
        RECT upd_btn = { c3.right - sc(16, dpi) - bw, c3.top + (card_h2 - bh) / 2, c3.right - sc(16, dpi), c3.top + (card_h2 + bh) / 2 };
        draw_button(g, mem, pal, upd_btn, s_sw.dict_updating ? tr("Đang tải...", "Updating...") : tr("Tải xuống/Cập nhật", "Download/Update"),
                    HIT_BTN_DICT_UPDATE, FALSE, !s_sw.dict_updating, f_btn, dpi);
        cy += card_h2 + gap;

        RECT c4 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Từ điển theo từng ứng dụng", "Per-app dictionaries"),
                                  tr("Chỉ định từ điển riêng cho từng phần mềm", "Assign custom packs per executable"), f_card, f_sub, dpi);
        RECT app_btn = { c4.right - sc(16, dpi) - bw, c4.top + (card_h - bh) / 2, c4.right - sc(16, dpi), c4.top + (card_h + bh) / 2 };
        draw_button(g, mem, pal, app_btn, tr("Theo từng ứng dụng...", "Per-app..."), HIT_BTN_APP_DICTS, FALSE, TRUE, f_btn, dpi);

    } else if (s_sw.active_tab == NAV_SHORTCUTS) {
        RECT c1 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Phím chuyển E/V: Ctrl + Shift", "E/V Switch key: Ctrl + Shift"),
                                  tr("Nhấn nhanh để bật/tắt tiếng Việt", "Quick press to toggle Vietnamese"), f_card, f_sub, dpi);
        RECT sw1 = { c1.right - sc(16, dpi) - tw, c1.top + (card_h - th) / 2, c1.right - sc(16, dpi), c1.top + (card_h + th) / 2 };
        draw_toggle(g, pal, sw1, g_app.config.switch_key == GTV_SWITCH_CTRL_SHIFT, s_sw.hover_id == HIT_TOGGLE_CTRL_SHIFT, TRUE);
        push_hit(sw1, HIT_TOGGLE_CTRL_SHIFT, TRUE);
        cy += card_h + gap;

        RECT c2 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Phím chuyển E/V: Alt + Z", "E/V Switch key: Alt + Z"),
                                  tr("Thay thế Ctrl + Shift nếu trùng phím tắt app khác", "Alternative switch key"), f_card, f_sub, dpi);
        RECT sw2 = { c2.right - sc(16, dpi) - tw, c2.top + (card_h - th) / 2, c2.right - sc(16, dpi), c2.top + (card_h + th) / 2 };
        draw_toggle(g, pal, sw2, g_app.config.switch_key == GTV_SWITCH_ALT_Z, s_sw.hover_id == HIT_TOGGLE_ALT_Z, TRUE);
        push_hit(sw2, HIT_TOGGLE_ALT_Z, TRUE);
        cy += card_h + gap;

        int bw = sc(120, dpi), bh = sc(30, dpi);
        RECT c3 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Quản lý bàn phím Windows", "Windows Keyboard Switcher"),
                                  tr("Sắp xếp thứ tự Win+Space và khôi phục mặc định", "Manage Win+Space order and defaults"), f_card, f_sub, dpi);
        RECT kb_btn = { c3.right - sc(16, dpi) - bw, c3.top + (card_h - bh) / 2, c3.right - sc(16, dpi), c3.top + (card_h + bh) / 2 };
        draw_button(g, mem, pal, kb_btn, tr("Bàn phím...", "Keyboards..."), HIT_BTN_KEYBOARD, FALSE, TRUE, f_btn, dpi);
        cy += card_h + gap;

        RECT c4 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Bảng gõ tắt (Macro) & Emoji", "Macro & Emoji Tables"),
                                  tr("Thêm, sửa hoặc xóa từ viết tắt", "Edit shortcut expansions"), f_card, f_sub, dpi);
        RECT da_btn = { c4.right - sc(16, dpi) - bw, c4.top + (card_h - bh) / 2, c4.right - sc(16, dpi), c4.top + (card_h + bh) / 2 };
        draw_button(g, mem, pal, da_btn, tr("Dữ liệu...", "Data..."), HIT_BTN_MACRO_CFG, FALSE, TRUE, f_btn, dpi);

    } else if (s_sw.active_tab == NAV_ABOUT) {
        draw_card_frame(g, mem, pal, cx, cy, cwid, card_h2,
                        "GoTV v" GTV_VERSION,
                        tr("Bộ gõ tiếng Việt thuần C/Win32 TSF · Mã nguồn mở MIT", "Native C/Win32 TSF Vietnamese Input · MIT Open Source"),
                        f_card, f_sub, dpi);
        cy += card_h2 + gap;

        draw_card_frame(g, mem, pal, cx, cy, cwid, card_h2,
                        tr("Mã nguồn & Phát hành", "Repository & Releases"),
                        "https://github.com/isthaison/gotv", f_card, f_sub, dpi);
        cy += card_h2 + gap;

        int bw = sc(120, dpi), bh = sc(30, dpi);
        RECT c3 = draw_card_frame(g, mem, pal, cx, cy, cwid, card_h,
                                  tr("Thư mục cấu hình & dữ liệu", "Config & Data Folder"),
                                  "%LOCALAPPDATA%\\gotv", f_card, f_sub, dpi);
        RECT dir_btn = { c3.right - sc(16, dpi) - bw, c3.top + (card_h - bh) / 2, c3.right - sc(16, dpi), c3.top + (card_h + bh) / 2 };
        draw_button(g, mem, pal, dir_btn, tr("Mở thư mục", "Open folder"), HIT_BTN_OPEN_DIR, FALSE, TRUE, f_btn, dpi);
    }

    DeleteObject(f_brand);
    DeleteObject(f_h1);
    DeleteObject(f_nav);
    DeleteObject(f_card);
    DeleteObject(f_sub);
    DeleteObject(f_btn);

    GdipDeleteGraphics(g);
    BitBlt(out_hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldbmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void apply_dwm_frame(HWND hwnd);

static void save_and_refresh(HWND hwnd) {
    gtv_app_save_config();
    gtv_tray_update_icon();
    InvalidateRect(hwnd, NULL, FALSE);
}

static void handle_hit(HWND hwnd, int id) {
    if (id >= HIT_NAV_BASE && id < HIT_NAV_BASE + NAV_COUNT) {
        s_sw.active_tab = id - HIT_NAV_BASE;
        s_sw.app_dicts_mode = FALSE;
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (id >= HIT_TOGGLE_DICT_BASE && id < HIT_TOGGLE_DICT_BASE + (int)G_N_ELEMENTS(dict_pack_ctls)) {
        int idx = id - HIT_TOGGLE_DICT_BASE;
        const char *did = dict_pack_ctls[idx].dict_id;
        gboolean cur = gtv_dict_lang_enabled(g_app.config.dicts, did);
        gchar *next = gtv_dict_lang_toggle(g_app.config.dicts, did, !cur);
        if (next) {
            g_strlcpy(g_app.config.dicts, next, sizeof(g_app.config.dicts));
            gtv_dict_set_langs(next);
            g_free(next);
            save_and_refresh(hwnd);
        }
        return;
    }
    if (id >= HIT_AD_DICT_BASE && id < HIT_AD_DICT_BASE + (int)G_N_ELEMENTS(dict_pack_ctls)) {
        guint bit = 1u << (guint)(id - HIT_AD_DICT_BASE);
        guint next = s_sw.ad_mask ^ bit;
        if (next != 0) s_sw.ad_mask = next;
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (id >= HIT_AD_RULE_BASE && s_sw.ad_rules && (guint)(id - HIT_AD_RULE_BASE) < s_sw.ad_rules->len) {
        int idx = id - HIT_AD_RULE_BASE;
        s_sw.ad_sel_rule = idx;
        const GtvAppDictRule *r = g_ptr_array_index(s_sw.ad_rules, (guint)idx);
        ad_select_app_by_id(r->app_id);
        s_sw.ad_mask = ad_mask_from_str(r->dicts);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    switch (id) {
        case HIT_TOGGLE_TELEX:
            g_app.config.mode = GTV_TELEX;
            save_and_refresh(hwnd);
            break;
        case HIT_TOGGLE_VNI:
            g_app.config.mode = GTV_VNI;
            save_and_refresh(hwnd);
            break;
        case HIT_TOGGLE_SUGGEST:
            g_app.config.suggest_enabled = !g_app.config.suggest_enabled;
            save_and_refresh(hwnd);
            break;
        case HIT_TOGGLE_MODERN:
            g_app.config.modern = !g_app.config.modern;
            save_and_refresh(hwnd);
            break;
        case HIT_TOGGLE_SPELL:
            g_app.config.spellcheck = !g_app.config.spellcheck;
            save_and_refresh(hwnd);
            break;
        case HIT_TOGGLE_STARTUP: {
            gboolean is_on = (gtv_startup_get() != GTV_STARTUP_NONE);
            if (!is_on) {
                gtv_startup_set_user(TRUE);
            } else if (gtv_startup_get() == GTV_STARTUP_USER) {
                gtv_startup_set_user(FALSE);
            } else if (gtv_startup_get() == GTV_STARTUP_ADMIN) {
                gtv_startup_request(hwnd, "admin-off");
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case HIT_BTN_THEME_DARK:
            g_app.config.ui_theme = GTV_THEME_DARK;
            apply_dwm_frame(hwnd);
            save_and_refresh(hwnd);
            break;
        case HIT_BTN_THEME_LIGHT:
            g_app.config.ui_theme = GTV_THEME_LIGHT;
            apply_dwm_frame(hwnd);
            save_and_refresh(hwnd);
            break;
        case HIT_BTN_LANG_VI:
            g_app.config.ui_lang = GTV_LANG_VI;
            gtv_i18n_set_lang(GTV_LANG_VI);
            gtv_win_set_window_text(hwnd, "GoTV - Cài đặt");
            save_and_refresh(hwnd);
            break;
        case HIT_BTN_LANG_EN:
            g_app.config.ui_lang = GTV_LANG_EN;
            gtv_i18n_set_lang(GTV_LANG_EN);
            gtv_win_set_window_text(hwnd, "GoTV - Settings");
            save_and_refresh(hwnd);
            break;
        case HIT_TOGGLE_CTRL_SHIFT:
            g_app.config.switch_key = GTV_SWITCH_CTRL_SHIFT;
            gtv_hotkey_set_key(GTV_SWITCH_CTRL_SHIFT);
            save_and_refresh(hwnd);
            break;
        case HIT_TOGGLE_ALT_Z:
            g_app.config.switch_key = GTV_SWITCH_ALT_Z;
            gtv_hotkey_set_key(GTV_SWITCH_ALT_Z);
            save_and_refresh(hwnd);
            break;
        case HIT_BTN_DICT_UPDATE: {
            if (s_sw.dict_updating) break;
            s_sw.dict_updating = TRUE;
            g_free(s_sw.dict_status);
            s_sw.dict_status = g_strdup(tr("Đang kiểm tra từ điển mới trên GitHub...", "Checking for new dictionary on GitHub..."));
            InvalidateRect(hwnd, NULL, FALSE);
            GThread *th = g_thread_new("gtv-dict-update", dict_update_worker, hwnd);
            if (th) {
                g_thread_unref(th);
            } else {
                s_sw.dict_updating = FALSE;
                g_free(s_sw.dict_status);
                s_sw.dict_status = g_strdup(tr("Không chạy được tiến trình cập nhật.", "Could not start update worker."));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case HIT_BTN_MACRO_CFG:
        case HIT_BTN_EMOJI_CFG:
            gtv_data_show(hwnd);
            break;
        case HIT_BTN_KEYBOARD:
            gtv_keyboard_show(hwnd);
            break;
        case HIT_BTN_APP_DICTS:
            ad_load_data();
            s_sw.app_dicts_mode = TRUE;
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case HIT_AD_BACK:
            s_sw.app_dicts_mode = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case HIT_AD_PICK_APP: {
            if (!s_sw.ad_apps || s_sw.ad_apps->len == 0) break;
            HMENU hm = CreatePopupMenu();
            if (!hm) break;
            for (guint i = 0; i < s_sw.ad_apps->len; i++) {
                const WinAppComboItem *it = g_ptr_array_index(s_sw.ad_apps, i);
                UINT flags = ((int)i == s_sw.ad_sel_app) ? MF_CHECKED : MF_UNCHECKED;
                gtv_win_menu_add_utf8(hm, it && it->label ? it->label : "", flags, 1000 + (UINT)i);
            }
            POINT pt = { s_sw.ad_pick_rc.left, s_sw.ad_pick_rc.bottom };
            ClientToScreen(hwnd, &pt);
            int chosen = TrackPopupMenu(hm, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hm);
            if (chosen >= 1000 && (guint)(chosen - 1000) < s_sw.ad_apps->len) {
                s_sw.ad_sel_app = chosen - 1000;
                s_sw.ad_sel_rule = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case HIT_AD_SAVE: {
            if (!s_sw.ad_apps || s_sw.ad_sel_app < 0 || (guint)s_sw.ad_sel_app >= s_sw.ad_apps->len) break;
            const WinAppComboItem *it = g_ptr_array_index(s_sw.ad_apps, (guint)s_sw.ad_sel_app);
            gchar *norm = gtv_app_dict_normalize_id(it ? it->id : NULL);
            if (!norm) break;
            gchar *dicts = ad_mask_to_str(s_sw.ad_mask);
            if (!s_sw.ad_rules) s_sw.ad_rules = g_ptr_array_new_with_free_func(gtv_app_dict_rule_free);
            gboolean updated = FALSE;
            for (guint i = 0; i < s_sw.ad_rules->len; i++) {
                GtvAppDictRule *r = g_ptr_array_index(s_sw.ad_rules, i);
                if (g_strcmp0(r->app_id, norm) == 0) {
                    g_free(r->dicts);
                    r->dicts = dicts;
                    s_sw.ad_sel_rule = (int)i;
                    updated = TRUE;
                    break;
                }
            }
            if (!updated) {
                GtvAppDictRule *r = g_new0(GtvAppDictRule, 1);
                r->app_id = g_strdup(norm);
                r->dicts = dicts;
                g_ptr_array_add(s_sw.ad_rules, r);
                s_sw.ad_sel_rule = (int)(s_sw.ad_rules->len - 1);
            }
            g_free(norm);
            ad_save_rules_now();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case HIT_AD_DELETE:
            if (s_sw.ad_rules && s_sw.ad_sel_rule >= 0 && (guint)s_sw.ad_sel_rule < s_sw.ad_rules->len) {
                g_ptr_array_remove_index(s_sw.ad_rules, (guint)s_sw.ad_sel_rule);
                s_sw.ad_sel_rule = -1;
                ad_save_rules_now();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        case HIT_BTN_OPEN_DIR: {
            gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
            if (dir) {
                g_mkdir_with_parents(dir, 0755);
                gunichar2 *wdir = g_utf8_to_utf16(dir, -1, NULL, NULL, NULL);
                if (wdir) {
                    ShellExecuteW(hwnd, L"open", (LPCWSTR)wdir, NULL, NULL, SW_SHOWNORMAL);
                    g_free(wdir);
                }
                g_free(dir);
            }
            break;
        }
        default:
            break;
    }
}

static int find_hit(POINT pt) {
    for (int i = s_sw.n_hits - 1; i >= 0; i--) {
        if (s_sw.hits[i].enabled && PtInRect(&s_sw.hits[i].rc, pt))
            return s_sw.hits[i].id;
    }
    return HIT_NONE;
}

static void apply_dwm_frame(HWND hwnd) {
    const SetupPalette *pal = current_pal();
    BOOL dark = (g_app.config.ui_theme != GTV_THEME_LIGHT);
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DWORD corner = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
    COLORREF cap = pal->caption_rgb;
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &cap, sizeof(cap));
}

static LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            GtvGdipStartupInput in = { 1, NULL, FALSE, FALSE };
            GdiplusStartup(&s_sw.gdip_token, &in, NULL);
            s_sw.app_icon = (HICON)LoadImageW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON),
                                              IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
            apply_dwm_frame(hwnd);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *mmi = (MINMAXINFO *)lParam;
            UINT dpi = win_dpi(hwnd);
            DWORD st = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
            RECT r = { 0, 0, sc(640, dpi), sc(440, dpi) };
            AdjustWindowRectEx(&r, st ? st : WS_OVERLAPPEDWINDOW, FALSE, 0);
            mmi->ptMinTrackSize.x = r.right - r.left;
            mmi->ptMinTrackSize.y = r.bottom - r.top;
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT cr;
            GetClientRect(hwnd, &cr);
            if (cr.right > 0 && cr.bottom > 0)
                paint_setup_window(hwnd, hdc, cr.right, cr.bottom);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int hid = find_hit(pt);
            if (hid != s_sw.hover_id) {
                s_sw.hover_id = hid;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            SetCursor(LoadCursorW(NULL, (LPCWSTR)(hid != HIT_NONE ? IDC_HAND : IDC_ARROW)));
            return 0;
        }
        case WM_MOUSELEAVE:
            if (s_sw.hover_id != HIT_NONE) {
                s_sw.hover_id = HIT_NONE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int hid = find_hit(pt);
            if (hid != HIT_NONE) handle_hit(hwnd, hid);
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_GTV_DICT_STATUS:
            s_sw.dict_updating = FALSE;
            g_free(s_sw.dict_status);
            if (wParam) {
                const gchar *tag = gtv_dict_tag();
                s_sw.dict_status = g_strdup_printf(tr("Đã cập nhật từ điển %s (%u từ).", "Updated dictionary %s (%u words)."),
                    (tag && *tag) ? tag : "", gtv_dict_count());
            } else {
                s_sw.dict_status = g_strdup(tr("Từ điển đã mới nhất (hoặc không có mạng).", "Dictionary is up to date (or offline)."));
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_DESTROY:
            s_hwndSetup = NULL;
            s_sw.app_dicts_mode = FALSE;
            g_clear_pointer(&s_sw.dict_status, g_free);
            g_clear_pointer(&s_sw.ad_rules, g_ptr_array_unref);
            g_clear_pointer(&s_sw.ad_apps, g_ptr_array_unref);
            if (s_sw.app_icon) {
                DestroyIcon(s_sw.app_icon);
                s_sw.app_icon = NULL;
            }
            if (s_sw.gdip_token) {
                GdiplusShutdown(s_sw.gdip_token);
                s_sw.gdip_token = 0;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND s_hwndSplash = NULL;
static ULONG_PTR s_splash_gdip = 0;
static HICON s_splash_icon = NULL;

static void open_setup_window_now(void) {
    if (s_hwndSetup && IsWindow(s_hwndSetup)) {
        if (IsIconic(s_hwndSetup)) ShowWindow(s_hwndSetup, SW_RESTORE);
        SetForegroundWindow(s_hwndSetup);
        return;
    }
    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SetupWndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"GoTVSetupWindow";
    RegisterClassExW(&wc);

    UINT dpi = win_dpi(NULL);
    int w = sc(760, dpi);
    int h = sc(512, dpi);
    RECT desk = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desk, 0);
    int x = desk.left + ((desk.right - desk.left) - w) / 2;
    int y = desk.top + ((desk.bottom - desk.top) - h) / 2;

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT wr = { 0, 0, w, h };
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;

    gunichar2 *wtitle = g_utf8_to_utf16(
        gtv_i18n_s(g_app.config.ui_lang, "GoTV - Cài đặt", "GoTV - Settings"),
        -1, NULL, NULL, NULL);
    s_sw.active_tab = NAV_GENERAL;
    s_sw.hover_id = HIT_NONE;
    s_sw.dict_updating = FALSE;
    s_hwndSetup = CreateWindowExW(0, L"GoTVSetupWindow",
                                  (LPCWSTR)(wtitle ? wtitle : L"GoTV"),
                                  style, x, y, ww, wh,
                                  NULL, NULL, hinst, NULL);
    g_free(wtitle);
    if (s_hwndSetup) {
        ShowWindow(s_hwndSetup, SW_SHOW);
        SetForegroundWindow(s_hwndSetup);
        UpdateWindow(s_hwndSetup);
    }
}

static LRESULT CALLBACK SplashWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            GtvGdipStartupInput in = { 1, NULL, FALSE, FALSE };
            GdiplusStartup(&s_splash_gdip, &in, NULL);
            UINT dpi = win_dpi(hwnd);
            int isz = sc(48, dpi);
            s_splash_icon = (HICON)LoadImageW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON),
                                              IMAGE_ICON, isz, isz, LR_DEFAULTCOLOR);
            SetTimer(hwnd, 1, 650, NULL);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC out = BeginPaint(hwnd, &ps);
            RECT cr;
            GetClientRect(hwnd, &cr);
            int cw = cr.right, ch = cr.bottom;
            if (cw > 0 && ch > 0) {
                UINT dpi = win_dpi(hwnd);
                HDC mem = CreateCompatibleDC(out);
                HBITMAP bmp = CreateCompatibleBitmap(out, cw, ch);
                HBITMAP oldb = (HBITMAP)SelectObject(mem, bmp);
                GpGraphics *g = NULL;
                GdipCreateFromHDC(mem, &g);
                if (g) GdipSetSmoothingMode(g, 4);

                const SetupPalette *pal = current_pal();
                fill_round_rect(g, 0, 0, (REAL)cw, (REAL)ch, 0, pal->bg, pal->card_border);
                if (s_splash_icon) {
                    int isz = sc(48, dpi);
                    DrawIconEx(mem, (cw - isz) / 2, sc(28, dpi), s_splash_icon, isz, isz, 0, NULL, DI_NORMAL);
                }

                HFONT f_title = make_font(20, FW_BOLD, dpi);
                HFONT f_sub = make_font(12, FW_NORMAL, dpi);
                HFONT f_ver = make_font(10, FW_NORMAL, dpi);

                RECT trc = { sc(16, dpi), sc(84, dpi), cw - sc(16, dpi), sc(114, dpi) };
                draw_text_u8(mem, "GoTV", &trc, pal->text_primary, f_title, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                RECT src = { sc(16, dpi), sc(114, dpi), cw - sc(16, dpi), sc(136, dpi) };
                draw_text_u8(mem, tr("Bộ gõ tiếng Việt · Telex / VNI", "Vietnamese Input · Telex / VNI"),
                             &src, pal->text_secondary, f_sub, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                int bw = sc(140, dpi), bh = sc(4, dpi);
                fill_round_rect(g, (REAL)((cw - bw) / 2), (REAL)sc(150, dpi), (REAL)bw, (REAL)bh,
                                (REAL)(bh / 2), pal->accent, 0);

                RECT vrc = { sc(16, dpi), ch - sc(30, dpi), cw - sc(16, dpi), ch - sc(12, dpi) };
                draw_text_u8(mem, "v" GTV_VERSION, &vrc, pal->text_muted, f_ver, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                BitBlt(out, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
                DeleteObject(f_title);
                DeleteObject(f_sub);
                DeleteObject(f_ver);
                if (g) GdipDeleteGraphics(g);
                SelectObject(mem, oldb);
                DeleteObject(bmp);
                DeleteDC(mem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
        case WM_LBUTTONUP:
            KillTimer(hwnd, 1);
            DestroyWindow(hwnd);
            open_setup_window_now();
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE) {
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
                open_setup_window_now();
                return 0;
            }
            break;
        case WM_DESTROY:
            s_hwndSplash = NULL;
            if (s_splash_icon) { DestroyIcon(s_splash_icon); s_splash_icon = NULL; }
            if (s_splash_gdip) { GdiplusShutdown(s_splash_gdip); s_splash_gdip = 0; }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void gtv_setup_show(HWND parent) {
    (void)parent;
    if (s_hwndSetup && IsWindow(s_hwndSetup)) {
        if (IsIconic(s_hwndSetup)) ShowWindow(s_hwndSetup, SW_RESTORE);
        SetForegroundWindow(s_hwndSetup);
        return;
    }
    if (s_hwndSplash && IsWindow(s_hwndSplash)) {
        SetForegroundWindow(s_hwndSplash);
        return;
    }
    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SplashWndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"GoTVSplashWindow";
    RegisterClassExW(&wc);

    UINT dpi = win_dpi(NULL);
    int w = sc(340, dpi), h = sc(200, dpi);
    RECT desk = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desk, 0);
    int x = desk.left + ((desk.right - desk.left) - w) / 2;
    int y = desk.top + ((desk.bottom - desk.top) - h) / 2;

    s_hwndSplash = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"GoTVSplashWindow", L"GoTV",
                                   WS_POPUP, x, y, w, h, NULL, NULL, hinst, NULL);
    if (s_hwndSplash) {
        ShowWindow(s_hwndSplash, SW_SHOW);
        SetForegroundWindow(s_hwndSplash);
        UpdateWindow(s_hwndSplash);
    } else {
        open_setup_window_now();
    }
}
