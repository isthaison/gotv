#include "tsf_service.h"
#include "tsf_suggest.h"

CGtvTextService::CGtvTextService() :
    m_cRef(1),
    m_pThreadMgr(NULL),
    m_tfClientId(0),
    m_dwThreadMgrEventSinkCookie(TF_INVALID_COOKIE),
    m_dwKeyEventSinkCookie(TF_INVALID_COOKIE),
    m_dwTextLayoutSinkCookie(TF_INVALID_COOKIE),
    m_pLayoutContext(NULL),
    m_pComposition(NULL),
    m_pEngine(NULL)
{
    ReloadConfig();
    DllAddRef();
}

void CGtvTextService::ReloadConfig()
{
    GtvConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    gchar *config_dir = gtv_user_path(g_get_user_config_dir(), NULL);
    gtv_config_load(&cfg, config_dir);
    g_free(config_dir);

    if (m_pEngine) {
        m_pEngine->mode = cfg.mode;
        m_pEngine->modern = cfg.modern;
        m_pEngine->spellcheck = cfg.spellcheck;
    } else {
        m_pEngine = gtv_engine_new(&cfg);
    }
    gtv_config_clear(&cfg);
}

CGtvTextService::~CGtvTextService()
{
    UnadviseTextLayoutSink();
    if (m_pEngine) {
        gtv_engine_free(m_pEngine);
        m_pEngine = NULL;
    }
    DllRelease();
}

// IUnknown implementation
STDMETHODIMP CGtvTextService::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = NULL;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor)) {
        *ppvObj = static_cast<ITfTextInputProcessor*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppvObj = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppvObj = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfTextLayoutSink)) {
        *ppvObj = static_cast<ITfTextLayoutSink*>(this);
    }

    if (*ppvObj) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CGtvTextService::AddRef(void)
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CGtvTextService::Release(void)
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0) {
        delete this;
    }
    return cRef;
}

// ITfTextInputProcessor
STDMETHODIMP CGtvTextService::Activate(ITfThreadMgr *ptim, TfClientId tid)
{
    if (!ptim) return E_INVALIDARG;
    m_pThreadMgr = ptim;
    m_pThreadMgr->AddRef();
    m_tfClientId = tid;

    if (!InitThreadMgrEventSink()) {
        Deactivate();
        return E_FAIL;
    }

    if (!InitKeyEventSink()) {
        Deactivate();
        return E_FAIL;
    }

    /* Track initial focused context for text layout updates */
    ITfDocumentMgr *pdim = NULL;
    if (m_pThreadMgr && SUCCEEDED(m_pThreadMgr->GetFocus(&pdim)) && pdim) {
        ITfContext *pic = NULL;
        if (SUCCEEDED(pdim->GetTop(&pic)) && pic) {
            AdviseTextLayoutSink(pic);
            pic->Release();
        }
        pdim->Release();
    }

    return S_OK;
}

STDMETHODIMP CGtvTextService::Deactivate(void)
{
    /* NULL the member first: EndComposition re-enters through
     * OnCompositionTerminated, which would otherwise release the same
     * pointer again. */
    ITfComposition *pComp = m_pComposition;
    m_pComposition = NULL;
    if (pComp) {
        pComp->EndComposition(0);
        pComp->Release();
    }

    gtv_suggest_hide();
    UnadviseTextLayoutSink();
    UninitKeyEventSink();
    UninitThreadMgrEventSink();

    if (m_pThreadMgr) {
        m_pThreadMgr->Release();
        m_pThreadMgr = NULL;
    }
    m_tfClientId = 0;

    if (m_pEngine) {
        gtv_engine_reset(m_pEngine);
    }

    return S_OK;
}

// ITfThreadMgrEventSink
STDMETHODIMP CGtvTextService::OnInitDocumentMgr(ITfDocumentMgr *pdim) { return S_OK; }
STDMETHODIMP CGtvTextService::OnUninitDocumentMgr(ITfDocumentMgr *pdim) { return S_OK; }
STDMETHODIMP CGtvTextService::OnSetFocus(ITfDocumentMgr *pdimFocus, ITfDocumentMgr *pdimPrevFocus)
{
    ITfComposition *pComp = m_pComposition;
    m_pComposition = NULL;
    if (pComp) {
        pComp->EndComposition(0);
        pComp->Release();
    }
    gtv_suggest_hide();
    ReloadConfig();
    if (m_pEngine) {
        gtv_engine_reset(m_pEngine);
    }

    if (pdimFocus) {
        ITfContext *pic = NULL;
        if (SUCCEEDED(pdimFocus->GetTop(&pic)) && pic) {
            AdviseTextLayoutSink(pic);
            pic->Release();
        }
    } else {
        UnadviseTextLayoutSink();
    }
    return S_OK;
}

STDMETHODIMP CGtvTextService::OnPushContext(ITfContext *pic)
{
    AdviseTextLayoutSink(pic);
    return S_OK;
}

STDMETHODIMP CGtvTextService::OnPopContext(ITfContext *pic)
{
    if (m_pLayoutContext == pic) {
        UnadviseTextLayoutSink();
    }
    return S_OK;
}

// ITfCompositionSink
STDMETHODIMP CGtvTextService::OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition)
{
    if (m_pComposition) {
        m_pComposition->Release();
        m_pComposition = NULL;
    }
    gtv_suggest_hide();
    if (m_pEngine) {
        gtv_engine_reset(m_pEngine);
    }
    return S_OK;
}

// Layout Sink registration (Chromium / Electron async layout tracking)
void CGtvTextService::AdviseTextLayoutSink(ITfContext *pic)
{
    if (!pic || m_pLayoutContext == pic) return;
    UnadviseTextLayoutSink();

    ITfSource *pSource = NULL;
    if (SUCCEEDED(pic->QueryInterface(IID_ITfSource, (void**)&pSource)) && pSource) {
        DWORD cookie = TF_INVALID_COOKIE;
        if (SUCCEEDED(pSource->AdviseSink(IID_ITfTextLayoutSink,
                                          static_cast<ITfTextLayoutSink*>(this),
                                          &cookie))) {
            m_pLayoutContext = pic;
            m_pLayoutContext->AddRef();
            m_dwTextLayoutSinkCookie = cookie;
        }
        pSource->Release();
    }
}

void CGtvTextService::UnadviseTextLayoutSink(void)
{
    if (m_pLayoutContext && m_dwTextLayoutSinkCookie != TF_INVALID_COOKIE) {
        ITfSource *pSource = NULL;
        if (SUCCEEDED(m_pLayoutContext->QueryInterface(IID_ITfSource, (void**)&pSource)) && pSource) {
            pSource->UnadviseSink(m_dwTextLayoutSinkCookie);
            pSource->Release();
        }
        m_dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
        m_pLayoutContext->Release();
        m_pLayoutContext = NULL;
    }
}

// Event Sink registrations
BOOL CGtvTextService::InitThreadMgrEventSink()
{
    ITfSource *pSource = NULL;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource)))
        return FALSE;

    HRESULT hr = pSource->AdviseSink(IID_ITfThreadMgrEventSink,
        static_cast<ITfThreadMgrEventSink*>(this),
        &m_dwThreadMgrEventSinkCookie);
    pSource->Release();
    return SUCCEEDED(hr);
}

void CGtvTextService::UninitThreadMgrEventSink()
{
    if (m_dwThreadMgrEventSinkCookie == TF_INVALID_COOKIE) return;
    if (!m_pThreadMgr) { m_dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE; return; }
    ITfSource *pSource = NULL;
    if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource))) {
        pSource->UnadviseSink(m_dwThreadMgrEventSinkCookie);
        pSource->Release();
    }
    m_dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
}

BOOL CGtvTextService::InitKeyEventSink()
{
    ITfKeystrokeMgr *pKeystrokeMgr = NULL;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr)))
        return FALSE;

    HRESULT hr = pKeystrokeMgr->AdviseKeyEventSink(m_tfClientId,
        static_cast<ITfKeyEventSink*>(this), TRUE);
    pKeystrokeMgr->Release();
    return SUCCEEDED(hr);
}

void CGtvTextService::UninitKeyEventSink()
{
    if (!m_pThreadMgr) return;
    ITfKeystrokeMgr *pKeystrokeMgr = NULL;
    if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr))) {
        pKeystrokeMgr->UnadviseKeyEventSink(m_tfClientId);
        pKeystrokeMgr->Release();
    }
}
