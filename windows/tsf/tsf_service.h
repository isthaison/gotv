#ifndef GTV_TSF_SERVICE_H
#define GTV_TSF_SERVICE_H

#include <windows.h>
#include <msctf.h>
#include "tsf_defs.h"
#include "engine.h"
#include "../suggest_ipc.h"

/* Process-wide DLL refcount backing DllCanUnloadNow (defined in
 * tsf_register.cpp). Every live service/factory object holds one. */
void DllAddRef(void);
void DllRelease(void);

class CEditSession;

class CGtvTextService :
    public ITfTextInputProcessor,
    public ITfThreadMgrEventSink,
    public ITfKeyEventSink,
    public ITfCompositionSink
{
    friend class CEditSession;

public:
    CGtvTextService();
    virtual ~CGtvTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr *ptim, TfClientId tid);
    STDMETHODIMP Deactivate(void);

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *pdim);
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *pdim);
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *pdimFocus, ITfDocumentMgr *pdimPrevFocus);
    STDMETHODIMP OnPushContext(ITfContext *pic);
    STDMETHODIMP OnPopContext(ITfContext *pic);

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground);
    STDMETHODIMP OnTestKeyDown(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    STDMETHODIMP OnKeyDown(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    STDMETHODIMP OnTestKeyUp(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    STDMETHODIMP OnKeyUp(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    STDMETHODIMP OnPreservedKey(ITfContext *pic, REFGUID rguid, BOOL *pfEaten);

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition);

    // Internal composition helpers (Update/End create their own sessions;
    // compositions always start with initial text, never empty).
    HRESULT UpdateCompositionText(ITfContext *pic, const wchar_t *text, int len);
    HRESULT EndComposition(ITfContext *pic, BOOL commit);
    BOOL IsComposing() const { return m_pComposition != NULL; }

    void ReloadConfig();
    BOOL IsEnabled() const { return m_fEnabled; }
    void SetEnabled(BOOL enabled);
    void ToggleEnabled();

    ITfComposition *GetComposition() const { return m_pComposition; }
    GtvEngine *GetEngine() const { return m_pEngine; }
    BOOL GetCaretRect(ITfContext *pic, RECT *rc, GTV_CARET_SOURCE *src);
    void RequestDeferredCaret(ITfContext *pic, guint generation);

private:
    volatile LONG m_cRef;
    ITfThreadMgr *m_pThreadMgr;
    TfClientId m_tfClientId;
    DWORD m_dwThreadMgrEventSinkCookie;
    DWORD m_dwKeyEventSinkCookie;
    ITfComposition *m_pComposition;
    GtvEngine *m_pEngine;
    BOOL m_fEnabled;

    BOOL InitKeyEventSink();
    void UninitKeyEventSink();
    BOOL InitThreadMgrEventSink();
    void UninitThreadMgrEventSink();
};

#endif // GTV_TSF_SERVICE_H
