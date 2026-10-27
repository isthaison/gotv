/* Inline keyword suggestions for the Windows TSF service.
 *
 * Broker architecture (system-wide, Electron-safe): this DLL never creates
 * windows inside the host process. Candidates (emoji instantly, dictionary
 * after a short debounce, learned data first) are forwarded to the
 * tray process overlay via WM_COPYDATA (windows/suggest_ipc.h); the tray
 * draws them near the caret without stealing focus. Selection keys mirror
 * IBus/Linux and are handled here: Up/Down navigate, 1-5 accept (Telex
 * only), Tab accepts the highlight (exact macro/emoji match wins first),
 * Enter commits, Esc dismisses, Space commits literally.
 *
 * Threading: key events run on the host app thread; only the dictionary
 * query runs on a worker (synchronous engine API + GCancellable with a sleep
 * debounce). Shared state is CS-guarded. Stale results are dropped by
 * generation counter; the tray also drops out-of-order payloads per source.
 * No caret rect -> no overlay (never fall back to a screen corner).
 */
#include "tsf_suggest.h"
#include "tsf_service.h"
extern "C" {
#include "internal.h"
}
#include "../app.h"
#include "../suggest_ipc.h"
#include <new>

#define GTV_SUGGEST_MAX 5
/* Local dictionary lookups are instant; a short coalescing delay is enough. */
#define GTV_SUGGEST_DEBOUNCE_MS 50

typedef struct {
    gchar *text;
    gboolean from_ai;
} SuggestItem;

typedef struct {
    CRITICAL_SECTION cs; /* guards shown, cursor, caret, generation, cancellable, tables */
    GPtrArray *shown;    /* SuggestItem */
    gint cursor;
    gchar *pending_query; /* app thread only */
    gboolean pending_bad;
    guint generation;
    RECT caret;          /* captured on the app thread at trigger time */
    gboolean caret_valid;
    GTV_CARET_SOURCE caret_source; /* probe that produced `caret` */
    RECT last_caret;
    gboolean last_caret_valid;
    GTV_CARET_SOURCE last_caret_source; /* which probe produced last_caret */
    GCancellable *cancellable;
    GHashTable *learned_words;
    GHashTable *learned_fixes;
} SuggestState;

static SuggestState g_suggest;
static LONG g_suggest_init = 0;

static void suggest_init_once(void) {
    if (InterlockedCompareExchange(&g_suggest_init, 1, 0) != 0) return;
    InitializeCriticalSection(&g_suggest.cs);
    /* Items own both their struct and text; free_items() releases them.
     * Do not give the array a free func too, or clearing double-frees
     * every candidate and can terminate the TSF host process. */
    g_suggest.shown = g_ptr_array_new();
}

static void free_items(GPtrArray *arr) {
    if (!arr) return;
    for (guint i = 0; i < arr->len; i++) {
        SuggestItem *it = (SuggestItem *)g_ptr_array_index(arr, i);
        g_free(it->text);
        g_free(it);
    }
    g_ptr_array_set_size(arr, 0);
}

static void learned_ensure_locked(void) {
    if (!g_suggest.learned_words) {
        g_suggest.learned_words = gtv_words_table_new();
        gchar *p = gtv_learned_path("learned-words.txt");
        gtv_words_load(g_suggest.learned_words, p);
        g_free(p);
    }
    if (!g_suggest.learned_fixes) {
        g_suggest.learned_fixes = gtv_fixes_table_new();
        gchar *p = gtv_learned_path("learned-corrections.txt");
        gtv_fixes_load(g_suggest.learned_fixes, p);
        g_free(p);
    }
}

/* Mirror of the tray learning: accepted corrections teach offline data. */
static void learn_accepted_locked(const gchar *typed, const gchar *fix) {
    if (!typed || !*typed || !fix || !*fix) return;
    learned_ensure_locked();
    gboolean dirty_fix = gtv_fixes_learn(g_suggest.learned_fixes, typed, fix);
    gboolean dirty_words = FALSE;
    gchar **words = g_strsplit_set(fix, " \t", -1);
    for (guint i = 0; words[i]; i++) {
        if (gtv_words_learn(g_suggest.learned_words, words[i]))
            dirty_words = TRUE;
    }
    g_strfreev(words);
    if (dirty_fix) {
        gchar *p = gtv_learned_path("learned-corrections.txt");
        gtv_fixes_save(g_suggest.learned_fixes, p);
        g_free(p);
    }
    if (dirty_words) {
        gchar *p = gtv_learned_path("learned-words.txt");
        gtv_words_save(g_suggest.learned_words, p);
        g_free(p);
    }
}

static gboolean suggest_word_known(const gchar *word) {
    if (!word || !*word) return FALSE;
    if (spell_word_valid(word)) return TRUE;
    EnterCriticalSection(&g_suggest.cs);
    learned_ensure_locked();
    gboolean hit = g_hash_table_contains(g_suggest.learned_words, word);
    LeaveCriticalSection(&g_suggest.cs);
    return hit;
}

/* Report a committed candidate to the tray (same channel the key sink
 * uses for typed words, so balloons/learning stay consistent). */
static void notify_tray_word(const char *word) {
    if (!word || !*word) return;
    HWND tray = FindWindowA(GTV_TRAY_WINDOW_CLASS, NULL);
    if (!tray) return;
    COPYDATASTRUCT cds;
    cds.dwData = (ULONG_PTR)GTV_AI_COPYDATA_ID;
    cds.cbData = (DWORD)strlen(word) + 1;
    cds.lpData = (PVOID)word;
    SendMessageTimeoutA(tray, WM_COPYDATA, 0, (LPARAM)&cds,
                        SMTO_ABORTIFHUNG, 500, NULL);
}

/* -- caret rectangle -------------------------------------------------- */

/* A caret rect must have real vertical extent. Width may legitimately be 0:
 * TSF reports a collapsed selection as left == right, and a Win32 caret is
 * routinely zero pixels wide, so requiring right > left here would reject every
 * real caret. msctf documents GetTextExt answering {0,0,0,0} when the window is
 * minimised or the text is not laid out, which the height check does catch. */
static BOOL caret_rect_plausible(const RECT *r) {
    if (!r || r->bottom <= r->top) return FALSE;
    if (r->right < r->left) return FALSE;
    /* Guard against a host that hands back nonsense coordinates. */
    return r->right - r->left <= 8192 && r->bottom - r->top <= 8192;
}

/* Reject any rect that is not where the user is actually typing. The tray
 * overlay draws the rect it is handed verbatim, so an unverified rect shows
 * the popup in the wrong place -- and a cached one keeps showing it there. A
 * rect is usable only if it lands on the focused window and on a real
 * monitor. */
static BOOL caret_rect_usable(const RECT *r) {
    if (!caret_rect_plausible(r)) return FALSE;

    HWND focus = GetForegroundWindow();
    if (!focus || !IsWindow(focus)) return FALSE;

    RECT wr;
    if (!GetWindowRect(focus, &wr)) return FALSE;

    /* Widen a collapsed rect before intersecting: IntersectRect reports "no
     * overlap" for a zero-width rect even when it sits well inside the box,
     * which is exactly the shape of a real caret. */
    RECT probe = *r;
    if (probe.right == probe.left) probe.right = probe.left + 1;
    if (probe.bottom == probe.top) probe.bottom = probe.top + 1;

    /* Slack for rounding: a caret can sit flush against the window border. */
    const LONG slack = 2;
    RECT padded = { wr.left - slack, wr.top - slack,
                    wr.right + slack, wr.bottom + slack };
    RECT hit;
    if (!IntersectRect(&hit, &probe, &padded)) return FALSE;

    /* A caret lives in the client area: one reported inside the title bar or
     * border is stale (Chromium/Electron hosts hand back the caret of a
     * hidden control at the window origin, e.g. {48,0,50,20} while the user
     * types in a composer at the bottom of the window). Reject those so the
     * popup does not pin itself to the window's top-left corner. */
    RECT cr;
    if (GetClientRect(focus, &cr)) {
        POINT tl = { cr.left, cr.top };
        POINT br = { cr.right, cr.bottom };
        ClientToScreen(focus, &tl);
        ClientToScreen(focus, &br);
        /* Chromium/Electron hosts keep a dummy Win32 caret at the client
         * origin (the real caret is painted by the renderer), so the probe
         * answers {0,0,2,20} no matter where the user types. Accepting it
         * parks the popup on the window's top-left corner. */
        if (r->left <= tl.x && r->top <= tl.y) return FALSE;
        RECT client = { tl.x - slack, tl.y - slack, br.x + slack, br.y + slack };
        if (!IntersectRect(&hit, &probe, &client)) return FALSE;
    }

    /* A stale TSF rect from another control (for example, a search box) can
     * still lie inside the foreground application's large client area. When
     * Windows identifies the focused child, require the caret to belong to
     * that control too. */
    GUITHREADINFO gti;
    memset(&gti, 0, sizeof(gti));
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndFocus && IsWindow(gti.hwndFocus) &&
        gti.hwndFocus != focus) {
        RECT focused_client;
        if (GetClientRect(gti.hwndFocus, &focused_client) &&
            focused_client.right > focused_client.left &&
            focused_client.bottom > focused_client.top) {
            POINT focused_tl = { focused_client.left, focused_client.top };
            POINT focused_br = { focused_client.right, focused_client.bottom };
            ClientToScreen(gti.hwndFocus, &focused_tl);
            ClientToScreen(gti.hwndFocus, &focused_br);
            RECT focused_area = { focused_tl.x - slack, focused_tl.y - slack,
                                  focused_br.x + slack, focused_br.y + slack };
            if (!IntersectRect(&hit, &probe, &focused_area)) return FALSE;
        }
    }

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(focus, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi))
        return IntersectRect(&hit, &probe, &mi.rcMonitor);
    return TRUE;
}

static void gtv_suggest_deferred_caret(guint generation, RECT comp, BOOL comp_ok,
                                       RECT sel, BOOL sel_ok);

/* Picks the best of the two TSF rects, then the Win32 caret. Shared by the
 * synchronous probe and the deferred one so both apply the same sanity rules.
 * `source` is diagnostic: it travels to the tray so a misplaced popup can be
 * attributed to a probe instead of guessed at. */
static BOOL choose_caret(RECT sel, BOOL sel_ok, RECT comp, BOOL comp_ok,
                         RECT *out, GTV_CARET_SOURCE *src) {
    if (sel_ok && caret_rect_usable(&sel)) {
        *out = sel;
        *src = GTV_CARET_SRC_SELECTION;
        return TRUE;
    }
    if (comp_ok && caret_rect_usable(&comp)) {
        *out = comp;
        *src = GTV_CARET_SRC_COMPOSITION;
        return TRUE;
    }

    /* Fallback 1: Win32 caret via GetGUIThreadInfo (classic edit controls,
     * notepad, WinForms). Reject dummy {0,0} carets from Chromium/Electron. */
    GUITHREADINFO gti;
    memset(&gti, 0, sizeof(gti));
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti)) {
        RECT r = {0, 0, 0, 0};
        if (gti.hwndCaret && IsWindow(gti.hwndCaret) &&
            (gti.rcCaret.right > gti.rcCaret.left || gti.rcCaret.bottom > gti.rcCaret.top)) {
            if (gti.rcCaret.left > 0 || gti.rcCaret.top > 0) {
                POINT pts[2];
                pts[0].x = gti.rcCaret.left;
                pts[0].y = gti.rcCaret.top;
                pts[1].x = gti.rcCaret.right;
                pts[1].y = gti.rcCaret.bottom;
                if (MapWindowPoints(gti.hwndCaret, NULL, pts, 2) != (LONG)-1) {
                    r.left = pts[0].x;
                    r.top = pts[0].y;
                    r.right = pts[1].x;
                    r.bottom = pts[1].y;
                    if (caret_rect_usable(&r)) {
                        *out = r;
                        *src = GTV_CARET_SRC_WIN32;
                        return TRUE;
                    }
                }
            }
        }
    }
    return FALSE;
}

/* Read-only caret probe session. A non-zero generation marks it deferred:
 * hosts (Chromium/Electron) refuse a synchronous session while the key event
 * that triggered us is still being dispatched, so the rect is published when
 * the session finally runs. */
class CCaretSession : public ITfEditSession {
public:
    CCaretSession(CGtvTextService *service, ITfContext *p, guint gen,
                  RECT *comp, BOOL *comp_ok, RECT *sel, BOOL *sel_ok)
        : m_cRef(1), m_service(service), m_pic(p), m_gen(gen),
          m_comp(comp), m_comp_ok(comp_ok), m_sel(sel), m_sel_ok(sel_ok) {
        if (m_service) m_service->AddRef();
        if (m_pic) m_pic->AddRef();
    }
    virtual ~CCaretSession() {
        if (m_pic) m_pic->Release();
        if (m_service) m_service->Release();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppv = static_cast<ITfEditSession *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() {
        LONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) {
        RECT comp = {0, 0, 0, 0};
        RECT sel = {0, 0, 0, 0};
        BOOL comp_ok = FALSE;
        BOOL sel_ok = FALSE;

        ITfContextView *view = NULL;
        if (FAILED(m_pic->GetActiveView(&view)) || !view) {
            if (m_gen) gtv_suggest_deferred_caret(m_gen, comp, comp_ok, sel, sel_ok);
            return E_FAIL;
        }

        BOOL clipped = FALSE;

        /* 1. Active composition range. Essential for Chromium/Electron/
         * OpenCode/VSCode because querying a 0-length collapsed selection
         * often yields TS_E_NOLAYOUT or E_FAIL before reflow. The
         * composition range has real character bounds; collapse it onto
         * its right edge to approximate the caret. */
        if (m_service && m_service->GetComposition()) {
            ITfRange *compRange = NULL;
            if (SUCCEEDED(m_service->GetComposition()->GetRange(&compRange)) && compRange) {
                if (SUCCEEDED(view->GetTextExt(ec, compRange, &comp, &clipped)) &&
                    caret_rect_plausible(&comp)) {
                    if (comp.right > comp.left) comp.left = comp.right;
                    comp_ok = TRUE;
                }
                compRange->Release();
            }
        }

        /* 2. Selection range. For a collapsed selection this is the real
         * caret, which is strictly better than the composition box, so it
         * is probed independently and the caller prefers it. */
        {
            TF_SELECTION tsel;
            ULONG fetched = 0;
            if (SUCCEEDED(m_pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tsel, &fetched)) &&
                fetched == 1 && tsel.range) {
                if (SUCCEEDED(view->GetTextExt(ec, tsel.range, &sel, &clipped)) &&
                    caret_rect_plausible(&sel))
                    sel_ok = TRUE;
                tsel.range->Release();
            }
        }

        view->Release();

        if (m_gen) {
            gtv_suggest_deferred_caret(m_gen, comp, comp_ok, sel, sel_ok);
        } else {
            if (m_comp) *m_comp = comp;
            if (m_comp_ok) *m_comp_ok = comp_ok;
            if (m_sel) *m_sel = sel;
            if (m_sel_ok) *m_sel_ok = sel_ok;
        }
        return S_OK;
    }

private:
    volatile LONG m_cRef;
    CGtvTextService *m_service;
    ITfContext *m_pic;
    guint m_gen;
    RECT *m_comp;
    BOOL *m_comp_ok;
    RECT *m_sel;
    BOOL *m_sel_ok;
};

/* -- asynchronous layout updates & retries ------------------------------- */

STDMETHODIMP CGtvTextService::OnLayoutChange(ITfContext *pic, TfLayoutCode lcode, ITfContextView *pView) {
    (void)lcode;
    (void)pView;
    if (!pic) return S_OK;

    guint gen = 0;
    EnterCriticalSection(&g_suggest.cs);
    gen = g_suggest.generation;
    LeaveCriticalSection(&g_suggest.cs);

    if (gen && (IsComposing() || gtv_suggest_visible())) {
        RequestDeferredCaret(pic, gen);
    }
    return S_OK;
}

static UINT_PTR s_caret_retry_timer = 0;
static CGtvTextService *s_retry_service = NULL;
static ITfContext *s_retry_pic = NULL;
static guint s_retry_gen = 0;

static void cancel_caret_retry_timer(void) {
    if (s_caret_retry_timer) {
        KillTimer(NULL, s_caret_retry_timer);
        s_caret_retry_timer = 0;
    }
    if (s_retry_service) {
        s_retry_service->Release();
        s_retry_service = NULL;
    }
    if (s_retry_pic) {
        s_retry_pic->Release();
        s_retry_pic = NULL;
    }
    s_retry_gen = 0;
}

static VOID CALLBACK caret_retry_timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    (void)hwnd;
    (void)msg;
    (void)time;
    KillTimer(NULL, id);
    s_caret_retry_timer = 0;

    CGtvTextService *svc = s_retry_service;
    ITfContext *ctx = s_retry_pic;
    guint gen = s_retry_gen;
    s_retry_service = NULL;
    s_retry_pic = NULL;
    s_retry_gen = 0;

    EnterCriticalSection(&g_suggest.cs);
    gboolean needed = (gen == g_suggest.generation) &&
                      (!g_suggest.caret_valid || g_suggest.caret_source == GTV_CARET_SRC_CACHED);
    LeaveCriticalSection(&g_suggest.cs);

    if (needed && svc && ctx) {
        svc->RequestDeferredCaret(ctx, gen);
    }
    if (svc) svc->Release();
    if (ctx) ctx->Release();
}

static void schedule_caret_retry(CGtvTextService *service, ITfContext *pic, guint gen) {
    cancel_caret_retry_timer();
    if (!service || !pic || !gen) return;
    s_retry_service = service;
    s_retry_service->AddRef();
    s_retry_pic = pic;
    s_retry_pic->AddRef();
    s_retry_gen = gen;
    s_caret_retry_timer = SetTimer(NULL, 0, 35, caret_retry_timer_proc);
}

void CGtvTextService::RequestDeferredCaret(ITfContext *pic, guint generation) {
    if (!pic || !generation) return;
    CCaretSession *s = new (std::nothrow) CCaretSession(this, pic, generation,
                                                        NULL, NULL, NULL, NULL);
    if (!s) return;
    /* TF_ES_ASYNCDONTCARE: run as soon as the document lock is free instead of
     * being refused outright during key dispatch (Chromium/Electron). */
    pic->RequestEditSession(m_tfClientId, s, TF_ES_ASYNCDONTCARE | TF_ES_READ, NULL);
    s->Release();
}

BOOL CGtvTextService::GetCaretRect(ITfContext *pic, RECT *rc, GTV_CARET_SOURCE *src) {
    if (!pic || !rc) return FALSE;
    /* Read-only caret probe: open a tiny sync session through the context.
     * Both candidate rects are reported separately so the caller can pick the
     * trustworthy one instead of committing to whichever query answered. */
    RECT comp = {0, 0, 0, 0};
    RECT sel = {0, 0, 0, 0};
    BOOL comp_ok = FALSE;
    BOOL sel_ok = FALSE;

    CCaretSession *s = new (std::nothrow) CCaretSession(this, pic, 0,
                                                        &comp, &comp_ok, &sel, &sel_ok);
    if (s) {
        /* Hosts routinely refuse or defer a read-only session while the key
         * event that triggered us is still being dispatched. Both rects then
         * stay invalid, the usability checks below reject anything stale, and
         * the fallbacks take over -- previously the result was discarded
         * outright and a fabricated rect was drawn instead. */
        pic->RequestEditSession(m_tfClientId, s, TF_ES_SYNC | TF_ES_READ, NULL);
        s->Release();
    }

    RECT out = {0, 0, 0, 0};
    GTV_CARET_SOURCE source = GTV_CARET_SRC_NONE;
    BOOL ok = choose_caret(sel, sel_ok, comp, comp_ok, &out, &source);

    /* Fallback 2: caret cached from the previous keystroke of this
     * composition. Still validated, so a window move cannot pin the popup to
     * a stale spot. */
    if (!ok) {
        EnterCriticalSection(&g_suggest.cs);
        if (g_suggest.last_caret_valid) {
            RECT cached = g_suggest.last_caret;
            ok = caret_rect_usable(&cached);
            if (ok) {
                out = cached;
                source = GTV_CARET_SRC_CACHED;
            }
        }
        LeaveCriticalSection(&g_suggest.cs);
    }

    /* No fabricated fallback: deriving a caret from the mouse pointer or an
     * arbitrary window offset is what put the popup in the wrong place. When
     * every probe fails the overlay is simply not shown (see the header). */

    if (ok) {
        EnterCriticalSection(&g_suggest.cs);
        g_suggest.last_caret = out;
        g_suggest.last_caret_valid = TRUE;
        g_suggest.last_caret_source = source;
        LeaveCriticalSection(&g_suggest.cs);
        *rc = out;
        *src = source;
        return TRUE;
    }
    return FALSE;
}

/* -- tray broker ------------------------------------------------------- */

static void broker_send(const GtvSuggestIpcPayload *payload) {
    HWND tray = FindWindowA(GTV_TRAY_WINDOW_CLASS, NULL);
    if (!tray) return;
    COPYDATASTRUCT cds;
    cds.dwData = (ULONG_PTR)GTV_SUGGEST_COPYDATA_ID;
    cds.cbData = (DWORD)sizeof(*payload);
    cds.lpData = (PVOID)payload;
    SendMessageTimeoutA(tray, WM_COPYDATA, 0, (LPARAM)&cds,
                        SMTO_ABORTIFHUNG, 500, NULL);
}

static void broker_send_hide(guint generation) {
    GtvSuggestIpcPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.version = GTV_SUGGEST_IPC_VERSION;
    payload.command = GTV_SUGGEST_IPC_HIDE;
    payload.source_pid = GetCurrentProcessId();
    payload.generation = generation;
    payload.candidate_count = 0;
    payload.selected = 0;
    broker_send(&payload);
}

static void broker_send_show(guint generation, gchar **candidates, guint count,
                             gint selected, RECT caret, GTV_CARET_SOURCE source) {
    GtvSuggestIpcPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.version = GTV_SUGGEST_IPC_VERSION;
    payload.command = GTV_SUGGEST_IPC_SHOW;
    payload.source_pid = GetCurrentProcessId();
    payload.generation = generation;
    payload.caret_screen = caret;
    payload.caret_source = (DWORD)source;
    guint n = count > GTV_SUGGEST_IPC_MAX_CANDIDATES ? GTV_SUGGEST_IPC_MAX_CANDIDATES : count;
    for (guint i = 0; i < n; i++)
        g_strlcpy(payload.candidates[i], candidates[i] ? candidates[i] : "",
                  GTV_SUGGEST_IPC_CANDIDATE_BYTES);
    payload.candidate_count = n;
    payload.selected = (selected >= 0 && (guint)selected < n) ? (DWORD)selected : 0;
    broker_send(&payload);
}

/* Late caret from a deferred edit session (Chromium/Electron). The user's
 * keystrokes keep the original generation alive; if the session ran for a
 * superseded generation it is dropped. When suggestions are already shown,
 * re-publish them so the tray can move the popup onto the real caret.
 * Publishing happens on a short-lived thread: an edit session must not block
 * on the tray round-trip while it holds the document lock. */
typedef struct {
    guint gen;
    gchar *snap[GTV_SUGGEST_MAX];
    guint n;
    gint cursor;
    RECT caret;
    GTV_CARET_SOURCE src;
} DeferredShow;

static gpointer deferred_show_thread(gpointer data) {
    DeferredShow *ds = (DeferredShow *)data;
    broker_send_show(ds->gen, ds->snap, ds->n, ds->cursor, ds->caret, ds->src);
    for (guint i = 0; i < ds->n; i++) g_free(ds->snap[i]);
    g_free(ds);
    return NULL;
}

static void gtv_suggest_deferred_caret(guint generation, RECT comp, BOOL comp_ok,
                                       RECT sel, BOOL sel_ok) {
    suggest_init_once();
    RECT out;
    GTV_CARET_SOURCE src = GTV_CARET_SRC_NONE;
    if (!choose_caret(sel, sel_ok, comp, comp_ok, &out, &src)) return;

    gchar *snapshot[GTV_SUGGEST_MAX];
    memset(snapshot, 0, sizeof(snapshot));
    guint n = 0;
    gint cursor = 0;
    gboolean current = FALSE;
    EnterCriticalSection(&g_suggest.cs);
    current = (generation == g_suggest.generation);
    if (current) {
        g_suggest.caret = out;
        g_suggest.caret_valid = TRUE;
        g_suggest.caret_source = src;
        g_suggest.last_caret = out;
        g_suggest.last_caret_valid = TRUE;
        g_suggest.last_caret_source = src;
        if (g_suggest.shown && g_suggest.shown->len > 0) {
            cursor = g_suggest.cursor;
            n = g_suggest.shown->len > GTV_SUGGEST_MAX ? GTV_SUGGEST_MAX
                                                       : (guint)g_suggest.shown->len;
            for (guint i = 0; i < n; i++) {
                SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, i);
                snapshot[i] = g_strdup(it->text ? it->text : "");
            }
        }
    }
    LeaveCriticalSection(&g_suggest.cs);
    if (current && n > 0) {
        DeferredShow *ds = g_new0(DeferredShow, 1);
        ds->gen = generation;
        ds->n = n;
        ds->cursor = cursor;
        ds->caret = out;
        ds->src = src;
        for (guint i = 0; i < n; i++) ds->snap[i] = snapshot[i];
        if (!g_thread_new("gtv-caret", deferred_show_thread, ds)) {
            for (guint i = 0; i < n; i++) g_free(ds->snap[i]);
            g_free(ds);
        }
        return;
    }
    for (guint i = 0; i < n; i++) g_free(snapshot[i]);
}

/* -- worker ------------------------------------------------------------ */

typedef struct {
    gchar *query;
    gboolean bad;
    guint generation;
    GCancellable *cancel;
} SuggestJob;

static void suggest_job_free(gpointer data) {
    SuggestJob *job = (SuggestJob *)data;
    if (!job) return;
    g_free(job->query);
    if (job->cancel) g_object_unref(job->cancel);
    g_free(job);
}

static gpointer suggest_worker(gpointer data) {
    SuggestJob *job = (SuggestJob *)data;

    GtvConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    gtv_config_load(&cfg, dir);
    g_free(dir);

    /* Debounce on the worker: short coalescing delay, then query. */
    int debounce = GTV_SUGGEST_DEBOUNCE_MS;    for (int waited = 0; waited < debounce; waited += 25) {
        g_usleep(25 * 1000);
        if (g_cancellable_is_cancelled(job->cancel)) {
            gtv_config_clear(&cfg);
            suggest_job_free(job);
            return NULL;
        }
    }

    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    gboolean used_ai = FALSE;
    if (cfg.suggest_enabled && !g_cancellable_is_cancelled(job->cancel)) {
        GPtrArray *ai = gtv_suggest_combined(&cfg, "", job->query, job->bad);
        if (g_cancellable_is_cancelled(job->cancel)) {
            if (ai) g_ptr_array_unref(ai);
        } else if (ai) {
            for (guint i = 0; i < ai->len && out->len < GTV_SUGGEST_MAX; i++)
                g_ptr_array_add(out, g_strdup((gchar *)ai->pdata[i]));
            g_ptr_array_unref(ai);
            used_ai = out->len > 0;
        }
    }
    /* Offline fallback (mirror IBus): remembered correction in typo mode,
     * learned completions otherwise. */
    if (out->len == 0 && !g_cancellable_is_cancelled(job->cancel)) {
        EnterCriticalSection(&g_suggest.cs);
        learned_ensure_locked();
        if (job->bad) {
            gchar *fix = gtv_fixes_lookup(g_suggest.learned_fixes, job->query);
            if (fix) g_ptr_array_add(out, fix);
        } else {
            gtv_learned_completions(g_suggest.learned_words, job->query, out,
                                    GTV_SUGGEST_MAX);
        }
        LeaveCriticalSection(&g_suggest.cs);
    }
    gtv_config_clear(&cfg);

    /* Publish under the lock, send outside it (the tray round-trip can block). */
    gchar *snapshot[GTV_SUGGEST_MAX];
    guint n = 0;
    RECT caret;
    gboolean have_caret = FALSE;
    gint cursor = 0;
    guint cur_gen = 0;
    gboolean stale = TRUE;
    GTV_CARET_SOURCE caret_source = GTV_CARET_SRC_NONE;
    memset(&caret, 0, sizeof(caret));
    EnterCriticalSection(&g_suggest.cs);
    stale = g_cancellable_is_cancelled(job->cancel) || job->generation != g_suggest.generation;
    cur_gen = g_suggest.generation;
    if (!stale) {
        free_items(g_suggest.shown);
        if (out->len > 0) {
            for (guint i = 0; i < out->len && i < GTV_SUGGEST_MAX; i++) {
                SuggestItem *it = g_new0(SuggestItem, 1);
                it->text = g_strdup((gchar *)out->pdata[i]);
                it->from_ai = used_ai;
                g_ptr_array_add(g_suggest.shown, it);
            }
            g_suggest.cursor = 0;
        }
        cursor = g_suggest.cursor;
        caret = g_suggest.caret;
        have_caret = g_suggest.caret_valid;
        caret_source = g_suggest.caret_source;
        n = g_suggest.shown->len > GTV_SUGGEST_MAX ? GTV_SUGGEST_MAX
                                                  : (guint)g_suggest.shown->len;
        for (guint i = 0; i < n; i++) {
            SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, i);
            snapshot[i] = g_strdup(it->text ? it->text : "");
        }
    }
    LeaveCriticalSection(&g_suggest.cs);
    g_ptr_array_unref(out);
    if (stale) {
        suggest_job_free(job);
        return NULL;
    }
    if (n == 0 || !have_caret) {
        /* Nothing to show (or nowhere to show it): hide explicitly so a
         * previous query's overlay cannot linger. Same generation, so this
         * always wins over older shows. */
        broker_send_hide(cur_gen);
    } else {
        broker_send_show(cur_gen, snapshot, n, cursor, caret, caret_source);
    }
    for (guint i = 0; i < n; i++)
        g_free(snapshot[i]);
    suggest_job_free(job);
    return NULL;
}

/* -- commit helpers ----------------------------------------------------- */

static void engine_set_raw(GtvEngine *eng, const gchar *utf8) {
    gtv_engine_reset(eng);
    if (!utf8) return;
    for (const gchar *p = utf8; *p; p = g_utf8_next_char(p)) {
        gunichar ch = g_utf8_get_char(p);
        g_array_append_val(eng->buffer, ch);
    }
}

static HRESULT commit_candidate(CGtvTextService *service, ITfContext *pic,
                                const gchar *text, gboolean keep_composing) {
    if (!service || !pic || !text) return E_INVALIDARG;
    GtvEngine *eng = service->GetEngine();
    if (!eng) return E_FAIL;
    HRESULT hr;
    if (keep_composing) {
        engine_set_raw(eng, text);
        gchar *buf = gtv_engine_buffer(eng);
        if (buf && *buf) {
            glong wlen = 0;
            wchar_t *w = (wchar_t *)g_utf8_to_utf16(buf, -1, NULL, &wlen, NULL);
            if (w) {
                hr = service->UpdateCompositionText(pic, w, (int)wlen);
                g_free(w);
            } else {
                hr = E_FAIL;
            }
        } else {
            hr = service->EndComposition(pic, FALSE);
        }
        g_free(buf);
    } else {
        glong wlen = 0;
        wchar_t *w = (wchar_t *)g_utf8_to_utf16(text, -1, NULL, &wlen, NULL);
        if (!w) return E_FAIL;
        hr = service->UpdateCompositionText(pic, w, (int)wlen);
        g_free(w);
        if (SUCCEEDED(hr)) hr = service->EndComposition(pic, TRUE);
        gtv_engine_reset(eng);
    }
    return hr;
}

/* -- public API ---------------------------------------------------------- */

/* New generation + cancel in-flight work without notifying the tray (used
 * per keystroke to avoid flicker; the next show/hide supersedes). */
static guint suggest_restart_locked(void) {
    g_suggest.generation++;
    if (g_suggest.cancellable) {
        g_cancellable_cancel(g_suggest.cancellable);
        g_object_unref(g_suggest.cancellable);
        g_suggest.cancellable = NULL;
    }
    return g_suggest.generation;
}

void gtv_suggest_hide(void) {
    suggest_init_once();
    cancel_caret_retry_timer();
    guint gen = 0;
    EnterCriticalSection(&g_suggest.cs);
    gen = suggest_restart_locked();
    free_items(g_suggest.shown);
    g_suggest.cursor = 0;
    g_suggest.caret_valid = FALSE;
    g_suggest.last_caret_valid = FALSE;
    g_free(g_suggest.pending_query);
    g_suggest.pending_query = NULL;
    LeaveCriticalSection(&g_suggest.cs);
    broker_send_hide(gen);
}

gboolean gtv_suggest_visible(void) {
    suggest_init_once();
    EnterCriticalSection(&g_suggest.cs);
    gboolean vis = g_suggest.shown && g_suggest.shown->len > 0;
    LeaveCriticalSection(&g_suggest.cs);
    return vis;
}

gint gtv_suggest_count(void) {
    suggest_init_once();
    EnterCriticalSection(&g_suggest.cs);
    gint n = (g_suggest.shown && g_suggest.shown->len > 0) ? (gint)g_suggest.shown->len : 0;
    LeaveCriticalSection(&g_suggest.cs);
    return n;
}

void gtv_suggest_move_cursor(gint delta) {
    suggest_init_once();
    gchar *snapshot[GTV_SUGGEST_MAX];
    guint n = 0;
    RECT caret;
    gboolean have_caret = FALSE;
    gint cursor = 0;
    guint gen = 0;
    GTV_CARET_SOURCE caret_source = GTV_CARET_SRC_NONE;
    memset(&caret, 0, sizeof(caret));
    EnterCriticalSection(&g_suggest.cs);
    if (g_suggest.shown && g_suggest.shown->len > 0) {
        gint count = (gint)g_suggest.shown->len;
        g_suggest.cursor = (g_suggest.cursor + delta % count + count) % count;
        cursor = g_suggest.cursor;
        caret = g_suggest.caret;
        have_caret = g_suggest.caret_valid;
        caret_source = g_suggest.caret_source;
        gen = g_suggest.generation;
        n = (guint)count > GTV_SUGGEST_MAX ? GTV_SUGGEST_MAX : (guint)count;
        for (guint i = 0; i < n; i++) {
            SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, i);
            snapshot[i] = g_strdup(it->text ? it->text : "");
        }
    }
    LeaveCriticalSection(&g_suggest.cs);
    if (n > 0 && have_caret)
        broker_send_show(gen, snapshot, n, cursor, caret, caret_source);
    for (guint i = 0; i < n; i++)
        g_free(snapshot[i]);
}

static void accept_locked(CGtvTextService *service, ITfContext *pic,
                          const gchar *text, gboolean from_ai,
                          const gchar *typed, gboolean keep_composing) {
    if (!text || !*text) return;
    commit_candidate(service, pic, text, keep_composing);
    if (from_ai && typed && strcmp(typed, text) != 0) {
        EnterCriticalSection(&g_suggest.cs);
        learn_accepted_locked(typed, text);
        LeaveCriticalSection(&g_suggest.cs);
    }
    if (typed) notify_tray_word(text);
    gtv_suggest_hide();
}

void gtv_suggest_accept_cursor(CGtvTextService *service, ITfContext *pic) {
    if (!service) return;
    gchar *text = NULL;
    gboolean from_ai = FALSE;
    gchar *typed = NULL;
    EnterCriticalSection(&g_suggest.cs);
    if (g_suggest.shown && g_suggest.cursor >= 0 &&
        g_suggest.cursor < (gint)g_suggest.shown->len) {
        SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, g_suggest.cursor);
        text = g_strdup(it->text);
        from_ai = it->from_ai;
        typed = g_strdup(g_suggest.pending_query);
    }
    LeaveCriticalSection(&g_suggest.cs);
    if (text) accept_locked(service, pic, text, from_ai, typed, TRUE);
    g_free(text);
    g_free(typed);
}

void gtv_suggest_accept_index(CGtvTextService *service, ITfContext *pic, gint index) {
    if (!service) return;
    gchar *text = NULL;
    gboolean from_ai = FALSE;
    gchar *typed = NULL;
    EnterCriticalSection(&g_suggest.cs);
    if (g_suggest.shown && index >= 0 && index < (gint)g_suggest.shown->len) {
        SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, index);
        text = g_strdup(it->text);
        from_ai = it->from_ai;
        typed = g_strdup(g_suggest.pending_query);
    }
    LeaveCriticalSection(&g_suggest.cs);
    if (text) accept_locked(service, pic, text, from_ai, typed, FALSE);
    g_free(text);
    g_free(typed);
}

gboolean gtv_suggest_has_exact_match(CGtvTextService *service) {
    if (!service || !service->IsComposing()) return FALSE;
    GtvEngine *eng = service->GetEngine();
    if (!eng) return FALSE;
    gchar *buf = gtv_engine_buffer(eng);
    if (!buf || !*buf) {
        g_free(buf);
        return FALSE;
    }
    gchar *hit = expand_macro(buf);
    if (!hit) hit = expand_emoji(buf);
    g_free(buf);
    if (hit) {
        g_free(hit);
        return TRUE;
    }
    return FALSE;
}

void gtv_suggest_on_key(CGtvTextService *service, ITfContext *pic) {
    suggest_init_once();
    /* Cancel anything in flight first (new keystroke restarts the story),
     * then take a fresh generation so stale workers cannot publish. No
     * HIDE is sent here: the next show/hide supersedes, avoiding flicker. */
    EnterCriticalSection(&g_suggest.cs);
    guint gen = suggest_restart_locked();
    LeaveCriticalSection(&g_suggest.cs);
    if (!service || !pic || !service->IsComposing()) {
        gtv_suggest_hide();
        return;
    }
    GtvEngine *eng = service->GetEngine();
    if (!eng) {
        gtv_suggest_hide();
        return;
    }
    gchar *buf = gtv_engine_buffer(eng);
    if (!buf || !*buf) {
        g_free(buf);
        gtv_suggest_hide();
        return;
    }
    glong len = g_utf8_strlen(buf, -1);

    /* Make sure layout changes in Chromium / Electron notify us */
    service->AdviseTextLayoutSink(pic);

    /* Caret is captured on the app thread now; the worker only forwards it.
     * No rect -> no overlay (never fall back to a screen corner). */
    RECT caret;
    GTV_CARET_SOURCE caret_source = GTV_CARET_SRC_NONE;
    gboolean have_caret = (gboolean)service->GetCaretRect(pic, &caret, &caret_source);
    if (!have_caret || caret_source == GTV_CARET_SRC_WIN32 || caret_source == GTV_CARET_SRC_CACHED) {
        /* Some editors expose a stale caret while the TSF text layout is
         * still updating. Retry after dispatch so a usable TSF rect can move
         * the popup to the actual insertion point. */
        service->RequestDeferredCaret(pic, gen);
        schedule_caret_retry(service, pic, gen);
    }

    gunichar first = g_utf8_get_char(buf);
    if ((first == ':' || first == ';' || first == '<' || first == '(') && len >= 2) {
        /* Emoji prefix: instant local suggestions, no AI needed. */
        GPtrArray *sugs = get_emoji_suggestions(buf);
        if (sugs && sugs->len > 0 && have_caret) {
            gchar *snapshot[GTV_SUGGEST_MAX];
            guint n = 0;
            EnterCriticalSection(&g_suggest.cs);
            free_items(g_suggest.shown);
            for (guint i = 0; i < sugs->len && i < GTV_SUGGEST_MAX; i++) {
                SuggestItem *it = g_new0(SuggestItem, 1);
                it->text = g_strdup((gchar *)sugs->pdata[i]);
                it->from_ai = FALSE;
                g_ptr_array_add(g_suggest.shown, it);
            }
            g_suggest.cursor = 0;
            g_free(g_suggest.pending_query);
            g_suggest.pending_query = g_strdup(buf);
            g_suggest.pending_bad = FALSE;
        g_suggest.caret = caret;
        g_suggest.caret_source = caret_source;
        g_suggest.caret_valid = TRUE;
            n = g_suggest.shown->len > GTV_SUGGEST_MAX ? GTV_SUGGEST_MAX
                                                      : (guint)g_suggest.shown->len;
            for (guint i = 0; i < n; i++) {
                SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, i);
                snapshot[i] = g_strdup(it->text ? it->text : "");
            }
            LeaveCriticalSection(&g_suggest.cs);
            broker_send_show(gen, snapshot, n, 0, caret, caret_source);
            for (guint i = 0; i < n; i++)
                g_free(snapshot[i]);
        } else {
            gtv_suggest_hide();
        }
        if (sugs) g_ptr_array_unref(sugs);
        g_free(buf);
        return;
    }

    /* Suggestions path: dictionary suggestions or spellcheck completions */
    {
        GtvConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
        gtv_config_load(&cfg, dir);
        g_free(dir);
        gboolean enabled = cfg.suggest_enabled || cfg.spellcheck;
        gtv_config_clear(&cfg);
        if (!enabled) {
            g_free(buf);
            gtv_suggest_hide();
            return;
        }
    }
    {
        gboolean bad = eng->spellcheck && !suggest_word_known(buf);
        GCancellable *cancel = NULL;
        EnterCriticalSection(&g_suggest.cs);
        g_free(g_suggest.pending_query);
        g_suggest.pending_query = g_strdup(buf);
        g_suggest.pending_bad = bad;
        g_suggest.caret = caret;
        g_suggest.caret_source = caret_source;
        g_suggest.caret_valid = have_caret;
        g_suggest.cancellable = g_cancellable_new();
        if (g_suggest.cancellable)
            cancel = (GCancellable *)g_object_ref(g_suggest.cancellable);
        LeaveCriticalSection(&g_suggest.cs);
        g_free(buf);
        if (!cancel) return;
        SuggestJob *job = g_new0(SuggestJob, 1);
        job->query = g_strdup(g_suggest.pending_query);
        job->bad = bad;
        job->generation = gen;
        job->cancel = cancel;
        GThread *th = g_thread_new("gtv-suggest", suggest_worker, job);
        if (!th) {
            suggest_job_free(job);
        } else {
            g_thread_unref(th);
        }
    }
}
