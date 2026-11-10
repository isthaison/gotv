/* Inline keyword suggestions for the Windows TSF service.
 *
 * Implements the standard Windows TSF Candidate List architecture:
 *   1. ITfCandidateListUIElement + ITfCandidateListUIElementBehavior registered
 *      with the thread manager's ITfUIElementMgr (BeginUIElement /
 *      UpdateUIElement / EndUIElement). UI-less hosts that set pbShow = FALSE
 *      render the candidate list natively through this interface.
 *   2. When pbShow = TRUE (standard desktop hosts: VS Code, SSMS, Chrome,
 *      Edge, Firefox, Notepad, Office), candidates are displayed at the TSF
 *      composition/selection caret rectangle captured directly from the active
 *      ITfContextView (inside the composition edit session or via async
 *      ITfTextLayoutSink::OnLayoutChange).
 */
#include "tsf_suggest.h"
#include "tsf_service.h"
extern "C" {
#include "internal.h"
}
#include "../app.h"
#include "../suggest_ipc.h"
#include <new>
#include <string.h>

#define GTV_SUGGEST_MAX 5

/* Standard TSF Candidate List UIElement definitions (from Windows SDK msctf.h)
 * in case the MinGW header only declares the base ITfUIElement. */
#ifndef TF_CLUIE_DOCUMENTMGR
#define TF_CLUIE_DOCUMENTMGR  0x00000001
#define TF_CLUIE_COUNT        0x00000002
#define TF_CLUIE_SELECTION    0x00000004
#define TF_CLUIE_STRING       0x00000008
#define TF_CLUIE_PAGEINDEX    0x00000010
#define TF_CLUIE_CURRENTPAGE  0x00000020
#endif

#ifndef TF_INVALID_UIELEMENTID
#define TF_INVALID_UIELEMENTID ((DWORD)-1)
#endif

static const IID IID_ITfCandidateListUIElement_Local =
    { 0xea1ea138, 0x19df, 0x11d7, { 0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };
static const IID IID_ITfCandidateListUIElementBehavior_Local =
    { 0x85fad185, 0x58ce, 0x497a, { 0x94, 0x60, 0x35, 0x53, 0x66, 0xb6, 0x4b, 0x9a } };
/* {B7A5C6D1-3F4E-4A8B-9C1D-2E3F4A5B6C7D} */
static const GUID GUID_GtvCandidateListUIElement =
    { 0xb7a5c6d1, 0x3f4e, 0x4a8b, { 0x9c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5b, 0x6c, 0x7d } };

#ifndef __ITfCandidateListUIElement_INTERFACE_DEFINED__
#define __ITfCandidateListUIElement_INTERFACE_DEFINED__
MIDL_INTERFACE("ea1ea138-19df-11d7-a6d2-00065b84435c")
ITfCandidateListUIElement : public ITfUIElement
{
    virtual HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD *pdwFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr **ppdim) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT *puCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSelection(UINT *puIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetString(UINT uIndex, BSTR *pstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPageIndex(UINT *pIndex, UINT uSize, UINT *puPageCnt) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPageIndex(UINT *pIndex, UINT uPageCnt) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT *puPage) = 0;
};
#endif

#ifndef __ITfCandidateListUIElementBehavior_INTERFACE_DEFINED__
#define __ITfCandidateListUIElementBehavior_INTERFACE_DEFINED__
MIDL_INTERFACE("85fad185-58ce-497a-9460-355366b64b9a")
ITfCandidateListUIElementBehavior : public ITfCandidateListUIElement
{
    virtual HRESULT STDMETHODCALLTYPE SetSelection(UINT nIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE Finalize(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Abort(void) = 0;
};
#endif

typedef struct {
    gchar *text;
    gboolean from_ai;
} SuggestItem;

class CGtvCandidateUIElement;

typedef struct {
    CRITICAL_SECTION cs;
    GPtrArray *shown;    /* SuggestItem* */
    gint cursor;
    gchar *pending_query;
    gboolean pending_bad;
    guint generation;
    RECT caret;
    gboolean caret_valid;
    GTV_CARET_SOURCE caret_source;
    RECT last_caret;
    gboolean last_caret_valid;
    GTV_CARET_SOURCE last_caret_source;
    HWND last_focus_hwnd;
    GHashTable *learned_words;
    GHashTable *learned_fixes;
    CGtvCandidateUIElement *ui_elem;
    DWORD ui_elem_id;
    BOOL ui_host_draws;
    CGtvTextService *active_service;
    ITfContext *active_pic;
} SuggestState;

static SuggestState g_suggest;
static LONG g_suggest_init = 0;

static void suggest_init_once(void) {
    if (InterlockedCompareExchange(&g_suggest_init, 1, 0) != 0) return;
    InitializeCriticalSection(&g_suggest.cs);
    g_suggest.shown = g_ptr_array_new();
    g_suggest.ui_elem_id = TF_INVALID_UIELEMENTID;
    g_suggest.ui_host_draws = FALSE;
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

static void learn_accepted_locked(const gchar *typed, const gchar *fix, gboolean was_bad) {
    if (!typed || !*typed || !fix || !*fix) return;
    learned_ensure_locked();
    gboolean dirty_fix = FALSE;
    if (was_bad && !strchr(fix, ' '))
        dirty_fix = gtv_fixes_learn(g_suggest.learned_fixes, typed, fix);
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

/* -- TSF ITfCandidateListUIElement implementation ---------------------- */

class CGtvCandidateUIElement : public ITfCandidateListUIElementBehavior {
public:
    CGtvCandidateUIElement()
        : m_cRef(1), m_bShown(FALSE),
          m_dwFlags(TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
                    TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE) {}
    virtual ~CGtvCandidateUIElement() {}

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) {
        if (!ppvObj) return E_INVALIDARG;
        *ppvObj = NULL;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ITfUIElement)) {
            *ppvObj = static_cast<ITfUIElement *>(this);
        } else if (IsEqualIID(riid, IID_ITfCandidateListUIElement_Local)) {
            *ppvObj = static_cast<ITfCandidateListUIElement *>(this);
        } else if (IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior_Local)) {
            *ppvObj = static_cast<ITfCandidateListUIElementBehavior *>(this);
        }
        if (*ppvObj) {
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef(void) { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release(void) {
        LONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }

    /* ITfUIElement */
    STDMETHODIMP GetDescription(BSTR *pbstrDescription) {
        if (!pbstrDescription) return E_INVALIDARG;
        *pbstrDescription = SysAllocString(L"GoTV Candidate List");
        return *pbstrDescription ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetGUID(GUID *pguid) {
        if (!pguid) return E_INVALIDARG;
        *pguid = GUID_GtvCandidateListUIElement;
        return S_OK;
    }

    STDMETHODIMP Show(WINBOOL bShow) {
        m_bShown = bShow;
        return S_OK;
    }

    STDMETHODIMP IsShown(WINBOOL *pbShow) {
        if (!pbShow) return E_INVALIDARG;
        *pbShow = m_bShown;
        return S_OK;
    }

    /* ITfCandidateListUIElement */
    STDMETHODIMP GetUpdatedFlags(DWORD *pdwFlags) {
        if (!pdwFlags) return E_INVALIDARG;
        *pdwFlags = m_dwFlags;
        return S_OK;
    }

    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr **ppdim) {
        if (!ppdim) return E_INVALIDARG;
        *ppdim = NULL;
        EnterCriticalSection(&g_suggest.cs);
        CGtvTextService *svc = g_suggest.active_service;
        ITfThreadMgr *tm = svc ? svc->GetThreadMgr() : NULL;
        if (tm) tm->AddRef();
        LeaveCriticalSection(&g_suggest.cs);
        if (!tm) return E_FAIL;
        HRESULT hr = tm->GetFocus(ppdim);
        tm->Release();
        return hr;
    }

    STDMETHODIMP GetCount(UINT *puCount) {
        if (!puCount) return E_INVALIDARG;
        EnterCriticalSection(&g_suggest.cs);
        *puCount = g_suggest.shown ? (UINT)g_suggest.shown->len : 0;
        LeaveCriticalSection(&g_suggest.cs);
        return S_OK;
    }

    STDMETHODIMP GetSelection(UINT *puIndex) {
        if (!puIndex) return E_INVALIDARG;
        EnterCriticalSection(&g_suggest.cs);
        *puIndex = (g_suggest.cursor >= 0) ? (UINT)g_suggest.cursor : 0;
        LeaveCriticalSection(&g_suggest.cs);
        return S_OK;
    }

    STDMETHODIMP GetString(UINT uIndex, BSTR *pstr) {
        if (!pstr) return E_INVALIDARG;
        *pstr = NULL;
        gchar *utf8 = NULL;
        EnterCriticalSection(&g_suggest.cs);
        if (g_suggest.shown && uIndex < g_suggest.shown->len) {
            SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, uIndex);
            if (it && it->text) utf8 = g_strdup(it->text);
        }
        LeaveCriticalSection(&g_suggest.cs);
        if (!utf8) return E_INVALIDARG;
        glong wlen = 0;
        gunichar2 *w = g_utf8_to_utf16(utf8, -1, NULL, &wlen, NULL);
        g_free(utf8);
        if (!w) return E_OUTOFMEMORY;
        *pstr = SysAllocStringLen((const OLECHAR *)w, (UINT)wlen);
        g_free(w);
        return *pstr ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetPageIndex(UINT *pIndex, UINT uSize, UINT *puPageCnt) {
        if (!puPageCnt) return E_INVALIDARG;
        *puPageCnt = 1;
        if (pIndex && uSize > 0) pIndex[0] = 0;
        return S_OK;
    }

    STDMETHODIMP SetPageIndex(UINT *pIndex, UINT uPageCnt) {
        (void)pIndex;
        (void)uPageCnt;
        return S_OK;
    }

    STDMETHODIMP GetCurrentPage(UINT *puPage) {
        if (!puPage) return E_INVALIDARG;
        *puPage = 0;
        return S_OK;
    }

    /* ITfCandidateListUIElementBehavior */
    STDMETHODIMP SetSelection(UINT nIndex) {
        EnterCriticalSection(&g_suggest.cs);
        if (g_suggest.shown && nIndex < g_suggest.shown->len) {
            g_suggest.cursor = (gint)nIndex;
        }
        LeaveCriticalSection(&g_suggest.cs);
        return S_OK;
    }

    STDMETHODIMP Finalize(void) {
        EnterCriticalSection(&g_suggest.cs);
        CGtvTextService *svc = g_suggest.active_service;
        ITfContext *pic = g_suggest.active_pic;
        if (svc) svc->AddRef();
        if (pic) pic->AddRef();
        LeaveCriticalSection(&g_suggest.cs);
        if (svc && pic) {
            gtv_suggest_accept_cursor(svc, pic);
        }
        if (pic) pic->Release();
        if (svc) svc->Release();
        return S_OK;
    }

    STDMETHODIMP Abort(void) {
        gtv_suggest_hide();
        return S_OK;
    }

    void SetUpdatedFlags(DWORD flags) { m_dwFlags = flags; }

private:
    volatile LONG m_cRef;
    WINBOOL m_bShown;
    DWORD m_dwFlags;
};

static void ui_element_begin_or_update(CGtvTextService *service, ITfContext *pic, DWORD flags) {
    if (!service) return;
    ITfThreadMgr *tm = service->GetThreadMgr();
    if (!tm) return;

    ITfUIElementMgr *emgr = NULL;
    if (FAILED(tm->QueryInterface(IID_ITfUIElementMgr, (void **)&emgr)) || !emgr)
        return;

    EnterCriticalSection(&g_suggest.cs);
    if (g_suggest.active_service != service) {
        if (g_suggest.active_service) g_suggest.active_service->Release();
        g_suggest.active_service = service;
        g_suggest.active_service->AddRef();
    }
    if (g_suggest.active_pic != pic) {
        if (g_suggest.active_pic) g_suggest.active_pic->Release();
        g_suggest.active_pic = pic;
        if (g_suggest.active_pic) g_suggest.active_pic->AddRef();
    }
    if (!g_suggest.ui_elem) {
        g_suggest.ui_elem = new (std::nothrow) CGtvCandidateUIElement();
    }
    CGtvCandidateUIElement *elem = g_suggest.ui_elem;
    DWORD elem_id = g_suggest.ui_elem_id;
    if (elem) elem->AddRef();
    LeaveCriticalSection(&g_suggest.cs);

    if (elem) {
        elem->SetUpdatedFlags(flags);
        if (elem_id == TF_INVALID_UIELEMENTID) {
            WINBOOL bShow = TRUE;
            DWORD new_id = TF_INVALID_UIELEMENTID;
            if (SUCCEEDED(emgr->BeginUIElement(static_cast<ITfUIElement *>(elem), &bShow, &new_id))) {
                elem->Show(bShow);
                EnterCriticalSection(&g_suggest.cs);
                g_suggest.ui_elem_id = new_id;
                g_suggest.ui_host_draws = !bShow;
                LeaveCriticalSection(&g_suggest.cs);
                emgr->UpdateUIElement(new_id);
            }
        } else {
            emgr->UpdateUIElement(elem_id);
        }
        elem->Release();
    }
    emgr->Release();
}

static void ui_element_end(void) {
    EnterCriticalSection(&g_suggest.cs);
    DWORD elem_id = g_suggest.ui_elem_id;
    g_suggest.ui_elem_id = TF_INVALID_UIELEMENTID;
    g_suggest.ui_host_draws = FALSE;
    CGtvCandidateUIElement *elem = g_suggest.ui_elem;
    CGtvTextService *svc = g_suggest.active_service;
    g_suggest.active_service = NULL;
    if (g_suggest.active_pic) {
        g_suggest.active_pic->Release();
        g_suggest.active_pic = NULL;
    }
    LeaveCriticalSection(&g_suggest.cs);

    if (elem) elem->Show(FALSE);
    if (svc) {
        ITfThreadMgr *tm = svc->GetThreadMgr();
        if (tm && elem_id != TF_INVALID_UIELEMENTID) {
            ITfUIElementMgr *emgr = NULL;
            if (SUCCEEDED(tm->QueryInterface(IID_ITfUIElementMgr, (void **)&emgr)) && emgr) {
                emgr->EndUIElement(elem_id);
                emgr->Release();
            }
        }
        svc->Release();
    }
}

/* -- caret rectangle -------------------------------------------------- */

static BOOL caret_rect_plausible(const RECT *r) {
    if (!r || r->bottom <= r->top) return FALSE;
    if (r->right < r->left) return FALSE;
    /* Reject {0,0,...} origin rects returned when layout is not ready. */
    if (r->left == 0 && r->top == 0) return FALSE;
    return r->right - r->left <= 8192 && r->bottom - r->top <= 8192;
}

/* Validate a TSF GetTextExt rect (from selection or composition).
 * TSF GetTextExt already returns screen coordinates from the active view.
 * Avoid rejecting based on child hwndFocus (which breaks SSMS WPF, VS Code,
 * and multi-process Chromium hosts). */
static BOOL caret_rect_usable_tsf(const RECT *r) {
    if (!caret_rect_plausible(r)) return FALSE;

    RECT probe = *r;
    if (probe.right == probe.left) probe.right = probe.left + 1;
    if (probe.bottom == probe.top) probe.bottom = probe.top + 1;

    HWND focus = GetForegroundWindow();
    if (focus && IsWindow(focus)) {
        RECT wr;
        if (GetWindowRect(focus, &wr)) {
            const LONG slack = 32;
            RECT padded = { wr.left - slack, wr.top - slack,
                            wr.right + slack, wr.bottom + slack };
            RECT hit;
            if (!IntersectRect(&hit, &probe, &padded)) return FALSE;
        }
    }

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromRect(&probe, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        RECT hit;
        return IntersectRect(&hit, &probe, &mi.rcMonitor);
    }
    return TRUE;
}

/* Validate a Win32 GetGUIThreadInfo fallback caret.
 * Chromium/Electron hosts keep a dummy Win32 caret at the client origin
 * {0,0,2,20}, so reject rects pinned to the top-left corner of the client area. */
static BOOL caret_rect_usable_win32(const RECT *r) {
    if (!caret_rect_usable_tsf(r)) return FALSE;

    HWND focus = GetForegroundWindow();
    if (!focus || !IsWindow(focus)) return FALSE;

    RECT cr;
    if (GetClientRect(focus, &cr)) {
        POINT tl = { cr.left, cr.top };
        POINT br = { cr.right, cr.bottom };
        ClientToScreen(focus, &tl);
        ClientToScreen(focus, &br);
        if (r->left <= tl.x + 2 && r->top <= tl.y + 2) return FALSE;
    }
    return TRUE;
}

static void gtv_suggest_deferred_caret(guint generation, RECT comp, BOOL comp_ok,
                                       RECT sel, BOOL sel_ok);

static BOOL choose_caret(RECT sel, BOOL sel_ok, RECT comp, BOOL comp_ok,
                         RECT *out, GTV_CARET_SOURCE *src) {
    if (sel_ok && caret_rect_usable_tsf(&sel)) {
        *out = sel;
        *src = GTV_CARET_SRC_SELECTION;
        return TRUE;
    }
    if (comp_ok && caret_rect_usable_tsf(&comp)) {
        *out = comp;
        *src = GTV_CARET_SRC_COMPOSITION;
        return TRUE;
    }

    /* Fallback 1: Win32 caret via GetGUIThreadInfo (classic edit controls,
     * Notepad, WinForms). */
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
                    if (caret_rect_usable_win32(&r)) {
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

/* Capture caret directly inside CEditSession (DoStartComposition / DoUpdateText)
 * while the write lock TfEditCookie is already held. */
void CGtvTextService::CaptureCaretInSession(TfEditCookie ec, ITfContext *pic,
                                            ITfRange *compRange, ITfRange *selRange) {
    m_sessionCompOk = FALSE;
    m_sessionSelOk = FALSE;
    if (!pic) return;

    ITfContextView *view = NULL;
    if (FAILED(pic->GetActiveView(&view)) || !view) return;

    BOOL clipped = FALSE;
    if (compRange) {
        RECT comp = {0, 0, 0, 0};
        if (SUCCEEDED(view->GetTextExt(ec, compRange, &comp, &clipped)) &&
            caret_rect_plausible(&comp)) {
            if (comp.right > comp.left) comp.left = comp.right;
            m_sessionCompRect = comp;
            m_sessionCompOk = TRUE;
        }
    }
    if (selRange) {
        RECT sel = {0, 0, 0, 0};
        if (SUCCEEDED(view->GetTextExt(ec, selRange, &sel, &clipped)) &&
            caret_rect_plausible(&sel)) {
            m_sessionSelRect = sel;
            m_sessionSelOk = TRUE;
        }
    }
    view->Release();
}

void CGtvTextService::ConsumeSessionCaret(RECT *comp, BOOL *comp_ok,
                                          RECT *sel, BOOL *sel_ok) {
    if (comp) *comp = m_sessionCompRect;
    if (comp_ok) *comp_ok = m_sessionCompOk;
    if (sel) *sel = m_sessionSelRect;
    if (sel_ok) *sel_ok = m_sessionSelOk;
    m_sessionCompOk = FALSE;
    m_sessionSelOk = FALSE;
}

/* Read-only caret probe session. */
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

        /* 1. Active composition range. */
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

        /* 2. Selection range. */
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
static int s_retry_left = 0;

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
    s_retry_left = 0;
}

static VOID CALLBACK caret_retry_timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    (void)hwnd;
    (void)msg;
    (void)time;

    EnterCriticalSection(&g_suggest.cs);
    gboolean needed = (s_retry_gen != 0 && s_retry_gen == g_suggest.generation) &&
                      (!g_suggest.caret_valid ||
                       g_suggest.caret_source == GTV_CARET_SRC_CACHED ||
                       g_suggest.caret_source == GTV_CARET_SRC_WIN32);
    LeaveCriticalSection(&g_suggest.cs);

    if (!needed || s_retry_left <= 0 || !s_retry_service || !s_retry_pic) {
        cancel_caret_retry_timer();
        return;
    }

    s_retry_left--;
    s_retry_service->RequestDeferredCaret(s_retry_pic, s_retry_gen);
    if (s_retry_left <= 0) {
        cancel_caret_retry_timer();
    }
}

static void schedule_caret_retry(CGtvTextService *service, ITfContext *pic, guint gen) {
    cancel_caret_retry_timer();
    if (!service || !pic || !gen) return;
    s_retry_service = service;
    s_retry_service->AddRef();
    s_retry_pic = pic;
    s_retry_pic->AddRef();
    s_retry_gen = gen;
    s_retry_left = 8; /* Up to 8 * 25ms = 200ms for async Chromium/Electron reflow */
    s_caret_retry_timer = SetTimer(NULL, 0, 25, caret_retry_timer_proc);
}

void CGtvTextService::RequestDeferredCaret(ITfContext *pic, guint generation) {
    if (!pic || !generation) return;
    CCaretSession *s = new (std::nothrow) CCaretSession(this, pic, generation,
                                                        NULL, NULL, NULL, NULL);
    if (!s) return;
    /* IMPORTANT: phrSession MUST NOT be NULL, or msctf returns E_INVALIDARG
     * without ever invoking DoEditSession. */
    HRESULT hrSession = E_FAIL;
    pic->RequestEditSession(m_tfClientId, s, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hrSession);
    s->Release();
}

BOOL CGtvTextService::GetCaretRect(ITfContext *pic, RECT *rc, GTV_CARET_SOURCE *src) {
    if (!pic || !rc) return FALSE;

    RECT comp = {0, 0, 0, 0};
    RECT sel = {0, 0, 0, 0};
    BOOL comp_ok = FALSE;
    BOOL sel_ok = FALSE;

    /* First check the rect captured directly inside CEditSession
     * (DoStartComposition / DoUpdateText) while the write lock was held. */
    ConsumeSessionCaret(&comp, &comp_ok, &sel, &sel_ok);

    if (!comp_ok && !sel_ok) {
        CCaretSession *s = new (std::nothrow) CCaretSession(this, pic, 0,
                                                            &comp, &comp_ok, &sel, &sel_ok);
        if (s) {
            HRESULT hrSession = E_FAIL;
            pic->RequestEditSession(m_tfClientId, s, TF_ES_SYNC | TF_ES_READ, &hrSession);
            s->Release();
        }
    }

    RECT out = {0, 0, 0, 0};
    GTV_CARET_SOURCE source = GTV_CARET_SRC_NONE;
    BOOL ok = choose_caret(sel, sel_ok, comp, comp_ok, &out, &source);

    HWND cur_focus = GetForegroundWindow();

    /* Fallback 2: caret cached from the previous keystroke in the same window. */
    if (!ok) {
        EnterCriticalSection(&g_suggest.cs);
        if (g_suggest.last_caret_valid && g_suggest.last_focus_hwnd == cur_focus) {
            RECT cached = g_suggest.last_caret;
            ok = caret_rect_usable_tsf(&cached);
            if (ok) {
                out = cached;
                source = GTV_CARET_SRC_CACHED;
            }
        }
        LeaveCriticalSection(&g_suggest.cs);
    }

    if (ok) {
        EnterCriticalSection(&g_suggest.cs);
        g_suggest.last_caret = out;
        g_suggest.last_caret_valid = TRUE;
        g_suggest.last_caret_source = source;
        g_suggest.last_focus_hwnd = cur_focus;
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
    EnterCriticalSection(&g_suggest.cs);
    BOOL host_draws = g_suggest.ui_host_draws;
    LeaveCriticalSection(&g_suggest.cs);
    if (host_draws) return;

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

/* Late caret from a deferred edit session (Chromium/Electron/WPF). */
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

    HWND cur_focus = GetForegroundWindow();
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
        g_suggest.last_focus_hwnd = cur_focus;
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
        GThread *th = g_thread_new("gtv-caret", deferred_show_thread, ds);
        if (th) {
            g_thread_unref(th);
        } else {
            for (guint i = 0; i < n; i++) g_free(ds->snap[i]);
            g_free(ds);
        }
        return;
    }
    for (guint i = 0; i < n; i++) g_free(snapshot[i]);
}

/* -- candidate lookup -------------------------------------------------- */

static void collect_unique_candidates(GPtrArray *dst, GPtrArray *src, guint max) {
    if (!dst || !src) return;
    for (guint i = 0; i < src->len && dst->len < max; i++) {
        const gchar *cand = (const gchar *)src->pdata[i];
        if (!cand || !*cand) continue;
        gboolean dup = FALSE;
        for (guint k = 0; k < dst->len; k++) {
            if (!strcmp((const gchar *)dst->pdata[k], cand)) {
                dup = TRUE;
                break;
            }
        }
        if (!dup) g_ptr_array_add(dst, g_strdup(cand));
    }
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

static guint suggest_restart_locked(void) {
    g_suggest.generation++;
    return g_suggest.generation;
}

void gtv_suggest_hide(void) {
    suggest_init_once();
    cancel_caret_retry_timer();
    EnterCriticalSection(&g_suggest.cs);
    guint gen = suggest_restart_locked();
    gboolean had = g_suggest.shown && g_suggest.shown->len > 0;
    free_items(g_suggest.shown);
    g_suggest.cursor = 0;
    g_free(g_suggest.pending_query);
    g_suggest.pending_query = NULL;
    g_suggest.pending_bad = FALSE;
    g_suggest.caret_valid = FALSE;
    g_suggest.caret_source = GTV_CARET_SRC_NONE;
    LeaveCriticalSection(&g_suggest.cs);

    ui_element_end();
    if (had)
        broker_send_hide(gen);
}

gboolean gtv_suggest_visible(void) {
    if (!g_suggest_init) return FALSE;
    EnterCriticalSection(&g_suggest.cs);
    gboolean vis = g_suggest.shown && g_suggest.shown->len > 0;
    LeaveCriticalSection(&g_suggest.cs);
    return vis;
}

gint gtv_suggest_count(void) {
    if (!g_suggest_init) return 0;
    EnterCriticalSection(&g_suggest.cs);
    gint n = g_suggest.shown ? (gint)g_suggest.shown->len : 0;
    LeaveCriticalSection(&g_suggest.cs);
    return n;
}

void gtv_suggest_move_cursor(gint delta) {
    if (!g_suggest_init) return;
    gchar *snapshot[GTV_SUGGEST_MAX];
    guint n = 0;
    gint cursor = 0;
    guint gen = 0;
    RECT caret;
    gboolean have_caret = FALSE;
    GTV_CARET_SOURCE caret_source = GTV_CARET_SRC_NONE;
    CGtvTextService *svc = NULL;
    ITfContext *pic = NULL;
    memset(&caret, 0, sizeof(caret));
    EnterCriticalSection(&g_suggest.cs);
    if (g_suggest.shown && g_suggest.shown->len > 0) {
        gint len = (gint)g_suggest.shown->len;
        g_suggest.cursor = (g_suggest.cursor + delta + len) % len;
        cursor = g_suggest.cursor;
        gen = g_suggest.generation;
        caret = g_suggest.caret;
        have_caret = g_suggest.caret_valid;
        caret_source = g_suggest.caret_source;
        svc = g_suggest.active_service;
        pic = g_suggest.active_pic;
        if (svc) svc->AddRef();
        if (pic) pic->AddRef();
        n = (guint)len > GTV_SUGGEST_MAX ? GTV_SUGGEST_MAX : (guint)len;
        for (guint i = 0; i < n; i++) {
            SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, i);
            snapshot[i] = g_strdup(it->text ? it->text : "");
        }
    }
    LeaveCriticalSection(&g_suggest.cs);
    if (svc) {
        ui_element_begin_or_update(svc, pic, TF_CLUIE_SELECTION);
        svc->Release();
    }
    if (pic) pic->Release();
    if (n > 0 && have_caret)
        broker_send_show(gen, snapshot, n, cursor, caret, caret_source);
    for (guint i = 0; i < n; i++)
        g_free(snapshot[i]);
}

static void accept_at(CGtvTextService *service, ITfContext *pic,
                      gchar *cand, gchar *typed, gboolean from_ai, gboolean was_bad) {
    gtv_suggest_hide();
    if (!cand) {
        g_free(typed);
        return;
    }
    const gchar *space = strchr(cand, ' ');
    if (space && *(space + 1)) {
        /* Multi-word phrase: commit the whole phrase + trailing space. */
        gchar *full = g_strdup_printf("%s ", cand);
        commit_candidate(service, pic, full, FALSE);
        g_free(full);
    } else {
        /* Single word (or emoji): replace composition, keep composing so the
         * user can keep editing or hit Space to commit. */
        commit_candidate(service, pic, cand, TRUE);
    }
    if (from_ai && typed && *typed) {
        EnterCriticalSection(&g_suggest.cs);
        learn_accepted_locked(typed, cand, was_bad);
        LeaveCriticalSection(&g_suggest.cs);
    }
    notify_tray_word(cand);
    g_free(cand);
    g_free(typed);
}

void gtv_suggest_accept_cursor(CGtvTextService *service, ITfContext *pic) {
    if (!g_suggest_init) return;
    gint idx = 0;
    EnterCriticalSection(&g_suggest.cs);
    idx = g_suggest.cursor;
    LeaveCriticalSection(&g_suggest.cs);
    gtv_suggest_accept_index(service, pic, idx);
}

void gtv_suggest_accept_index(CGtvTextService *service, ITfContext *pic, gint index) {
    if (!g_suggest_init) return;
    gchar *cand = NULL;
    gchar *typed = NULL;
    gboolean from_ai = FALSE;
    gboolean was_bad = FALSE;
    EnterCriticalSection(&g_suggest.cs);
    if (g_suggest.shown && index >= 0 && index < (gint)g_suggest.shown->len) {
        SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, index);
        cand = g_strdup(it->text);
        from_ai = it->from_ai;
        typed = g_strdup(g_suggest.pending_query);
        was_bad = g_suggest.pending_bad;
    }
    LeaveCriticalSection(&g_suggest.cs);
    accept_at(service, pic, cand, typed, from_ai, was_bad);
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

    /* Ensure layout change notifications in Chromium / Electron / WPF */
    service->AdviseTextLayoutSink(pic);

    /* Capture caret on the app thread (from the active composition edit
     * session first, then read-only probe, then cached fallback). */
    RECT caret;
    GTV_CARET_SOURCE caret_source = GTV_CARET_SRC_NONE;
    gboolean have_caret = (gboolean)service->GetCaretRect(pic, &caret, &caret_source);
    if (!have_caret || caret_source == GTV_CARET_SRC_WIN32 || caret_source == GTV_CARET_SRC_CACHED) {
        service->RequestDeferredCaret(pic, gen);
        schedule_caret_retry(service, pic, gen);
    }

    gunichar first = g_utf8_get_char(buf);
    if ((first == ':' || first == ';' || first == '<' || first == '(') && len >= 2) {
        /* Emoji prefix: instant local suggestions */
        GPtrArray *sugs = get_emoji_suggestions(buf);
        if (sugs && sugs->len > 0) {
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
            g_suggest.caret_valid = have_caret;
            n = g_suggest.shown->len > GTV_SUGGEST_MAX ? GTV_SUGGEST_MAX
                                                      : (guint)g_suggest.shown->len;
            for (guint i = 0; i < n; i++) {
                SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, i);
                snapshot[i] = g_strdup(it->text ? it->text : "");
            }
            LeaveCriticalSection(&g_suggest.cs);

            ui_element_begin_or_update(service, pic,
                                       TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING);
            if (have_caret)
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

    /* Dictionary suggestions + spellcheck completions (synchronous in-memory
     * lookup on the STA thread so ITfUIElementMgr and deferred layout updates
     * always see the current candidate list immediately). */
    GtvConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    gtv_config_load(&cfg, dir);
    g_free(dir);
    gboolean enabled = cfg.suggest_enabled;
    if (!enabled) {
        gtv_config_clear(&cfg);
        g_free(buf);
        gtv_suggest_hide();
        return;
    }

    gboolean bad = FALSE;
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    gboolean used_ai = FALSE;

    if (cfg.suggest_enabled) {
        /* Prefix completions only (works for vi, tsql, c, csharp, en, etc.) */
        GPtrArray *comp_sugs = gtv_suggest_combined(&cfg, "", buf, FALSE);
        if (comp_sugs && comp_sugs->len > 0) {
            collect_unique_candidates(out, comp_sugs, GTV_SUGGEST_MAX);
        }
        if (comp_sugs) g_ptr_array_unref(comp_sugs);
        used_ai = out->len > 0;
    }

    if (out->len == 0 && cfg.suggest_enabled) {
        EnterCriticalSection(&g_suggest.cs);
        learned_ensure_locked();
        gtv_learned_completions(g_suggest.learned_words, buf, out, GTV_SUGGEST_MAX);
        LeaveCriticalSection(&g_suggest.cs);
    }
    gtv_config_clear(&cfg);

    gchar *snapshot[GTV_SUGGEST_MAX];
    guint n = 0;
    EnterCriticalSection(&g_suggest.cs);
    free_items(g_suggest.shown);
    g_free(g_suggest.pending_query);
    g_suggest.pending_query = g_strdup(buf);
    g_suggest.pending_bad = bad;
    g_suggest.caret = caret;
    g_suggest.caret_source = caret_source;
    g_suggest.caret_valid = have_caret;
    if (out->len > 0) {
        for (guint i = 0; i < out->len && i < GTV_SUGGEST_MAX; i++) {
            SuggestItem *it = g_new0(SuggestItem, 1);
            it->text = g_strdup((gchar *)out->pdata[i]);
            it->from_ai = used_ai;
            g_ptr_array_add(g_suggest.shown, it);
        }
        g_suggest.cursor = 0;
        n = g_suggest.shown->len > GTV_SUGGEST_MAX ? GTV_SUGGEST_MAX
                                                  : (guint)g_suggest.shown->len;
        for (guint i = 0; i < n; i++) {
            SuggestItem *it = (SuggestItem *)g_ptr_array_index(g_suggest.shown, i);
            snapshot[i] = g_strdup(it->text ? it->text : "");
        }
    }
    LeaveCriticalSection(&g_suggest.cs);
    g_ptr_array_unref(out);
    g_free(buf);

    if (n == 0) {
        ui_element_end();
        broker_send_hide(gen);
    } else {
        /* Notify TSF UIElementMgr (native Candidate List). If the host is in
         * UI-less mode (pbShow == FALSE), the host renders candidates directly
         * from CGtvCandidateUIElement; otherwise broker_send_show (or the
         * deferred layout callback once Chromium/WPF layout settles) displays
         * the overlay at the caret. */
        ui_element_begin_or_update(service, pic,
                                   TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING);
        if (have_caret) {
            broker_send_show(gen, snapshot, n, 0, caret, caret_source);
        }
    }
    for (guint i = 0; i < n; i++)
        g_free(snapshot[i]);
}
