#include "tsf_service.h"

enum EditAction {
    EDIT_START_COMPOSITION,
    EDIT_UPDATE_TEXT,
    EDIT_END_COMPOSITION,
    EDIT_CANCEL_COMPOSITION
};

class CEditSession : public ITfEditSession {
public:
    CEditSession(CGtvTextService *pService, ITfContext *pic, EditAction action, const wchar_t *text = NULL, int len = 0) :
        m_cRef(1),
        m_pService(pService),
        m_pContext(pic),
        m_action(action),
        m_len(len)
    {
        if (m_pService) m_pService->AddRef();
        if (text && len > 0) {
            m_text = new wchar_t[len + 1];
            wcsncpy(m_text, text, len);
            m_text[len] = L'\0';
        } else {
            m_text = NULL;
            m_len = 0;
        }
        if (m_pContext) m_pContext->AddRef();
    }

    virtual ~CEditSession() {
        if (m_text) delete[] m_text;
        if (m_pContext) m_pContext->Release();
        if (m_pService) m_pService->Release();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppvObj = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() {
        LONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) {
        switch (m_action) {
            case EDIT_START_COMPOSITION:
                return DoStartComposition(ec);
            case EDIT_UPDATE_TEXT:
                return DoUpdateText(ec);
            case EDIT_END_COMPOSITION:
                return DoEndComposition(ec);
            case EDIT_CANCEL_COMPOSITION:
                return DoCancelComposition(ec);
        }
        return S_OK;
    }

private:
    volatile LONG m_cRef;
    CGtvTextService *m_pService;
    ITfContext *m_pContext;
    EditAction m_action;
    wchar_t *m_text;
    int m_len;

    HRESULT DoStartComposition(TfEditCookie ec) {
        ITfContextComposition *pContextComp = NULL;
        if (FAILED(m_pContext->QueryInterface(IID_ITfContextComposition, (void**)&pContextComp)))
            return E_FAIL;

        ITfInsertAtSelection *pInsertAtSelection = NULL;
        if (FAILED(m_pContext->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsertAtSelection))) {
            pContextComp->Release();
            return E_FAIL;
        }

        ITfRange *pRange = NULL;
        HRESULT hr = pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0, &pRange);
        pInsertAtSelection->Release();

        if (SUCCEEDED(hr) && pRange) {
            ITfComposition *pComp = NULL;
            hr = pContextComp->StartComposition(ec, pRange, static_cast<ITfCompositionSink*>(m_pService), &pComp);
            if (SUCCEEDED(hr) && pComp) {
                // Store active composition in service via helper
                m_pService->m_pComposition = pComp;
                if (m_text && m_len > 0) {
                    pRange->SetText(ec, 0, m_text, m_len);
                    pComp->ShiftEnd(ec, pRange);
                    // Move caret to end of range
                    ITfRange *pRangeEnd = NULL;
                    if (SUCCEEDED(pRange->Clone(&pRangeEnd))) {
                        pRangeEnd->Collapse(ec, TF_ANCHOR_END);
                        TF_SELECTION sel;
                        sel.range = pRangeEnd;
                        sel.style.ase = TF_AE_NONE;
                        sel.style.fInterimChar = FALSE;
                        m_pContext->SetSelection(ec, 1, &sel);
                        m_pService->CaptureCaretInSession(ec, m_pContext, pRange, pRangeEnd);
                        pRangeEnd->Release();
                    } else {
                        m_pService->CaptureCaretInSession(ec, m_pContext, pRange, NULL);
                    }
                }
            }
            pRange->Release();
        }
        pContextComp->Release();
        return hr;
    }

    HRESULT DoUpdateText(TfEditCookie ec) {
        if (!m_pService->m_pComposition) return E_FAIL;

        ITfRange *pRange = NULL;
        if (FAILED(m_pService->m_pComposition->GetRange(&pRange)) || !pRange)
            return E_FAIL;

        HRESULT hr = pRange->SetText(ec, 0, m_text ? m_text : L"", m_len);
        if (SUCCEEDED(hr)) {
            m_pService->m_pComposition->ShiftEnd(ec, pRange);
            ITfRange *pRangeEnd = NULL;
            if (SUCCEEDED(pRange->Clone(&pRangeEnd))) {
                pRangeEnd->Collapse(ec, TF_ANCHOR_END);
                TF_SELECTION sel;
                sel.range = pRangeEnd;
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                m_pContext->SetSelection(ec, 1, &sel);
                m_pService->CaptureCaretInSession(ec, m_pContext, pRange, pRangeEnd);
                pRangeEnd->Release();
            } else {
                m_pService->CaptureCaretInSession(ec, m_pContext, pRange, NULL);
            }
        }
        pRange->Release();
        return hr;
    }

    HRESULT DoEndComposition(TfEditCookie ec) {
        if (!m_pService->m_pComposition) return S_OK;
        /* Clear the member FIRST: EndComposition re-enters through
         * OnCompositionTerminated, which would otherwise release the
         * same pointer again (use-after-free / double free). */
        ITfComposition *pComp = m_pService->m_pComposition;
        m_pService->m_pComposition = NULL;
        pComp->EndComposition(ec);
        pComp->Release();
        return S_OK;
    }

    HRESULT DoCancelComposition(TfEditCookie ec) {
        if (!m_pService->m_pComposition) return S_OK;
        ITfComposition *pComp = m_pService->m_pComposition;
        m_pService->m_pComposition = NULL;
        ITfRange *pRange = NULL;
        if (SUCCEEDED(pComp->GetRange(&pRange)) && pRange) {
            pRange->SetText(ec, 0, L"", 0);
            pRange->Release();
        }
        pComp->EndComposition(ec);
        pComp->Release();
        return S_OK;
    }
};

HRESULT CGtvTextService::UpdateCompositionText(ITfContext *pic, const wchar_t *text, int len)
{
    if (!pic) return E_INVALIDARG;
    if (m_pComposition == NULL) {
        // Start composition with initial text
        CEditSession *pSession = new CEditSession(this, pic, EDIT_START_COMPOSITION, text, len);
        HRESULT hrSession = E_FAIL;
        HRESULT hr = pic->RequestEditSession(m_tfClientId, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (hr == TF_E_SYNCHRONOUS || hrSession == TF_E_SYNCHRONOUS) {
            hr = pic->RequestEditSession(m_tfClientId, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
        }
        pSession->Release();
        return SUCCEEDED(hr) ? hrSession : hr;
    }

    CEditSession *pSession = new CEditSession(this, pic, EDIT_UPDATE_TEXT, text, len);
    HRESULT hrSession = E_FAIL;
    HRESULT hr = pic->RequestEditSession(m_tfClientId, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    if (hr == TF_E_SYNCHRONOUS || hrSession == TF_E_SYNCHRONOUS) {
        hr = pic->RequestEditSession(m_tfClientId, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
    }
    pSession->Release();
    return SUCCEEDED(hr) ? hrSession : hr;
}

HRESULT CGtvTextService::EndComposition(ITfContext *pic, BOOL commit)
{
    if (!pic || !m_pComposition) return S_OK;
    CEditSession *pSession = new CEditSession(this, pic, commit ? EDIT_END_COMPOSITION : EDIT_CANCEL_COMPOSITION);
    HRESULT hrSession = E_FAIL;
    HRESULT hr = pic->RequestEditSession(m_tfClientId, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    if (hr == TF_E_SYNCHRONOUS || hrSession == TF_E_SYNCHRONOUS) {
        hr = pic->RequestEditSession(m_tfClientId, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
    }
    pSession->Release();
    return SUCCEEDED(hr) ? hrSession : hr;
}
