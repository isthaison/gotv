#ifndef GTV_TSF_DEFS_H
#define GTV_TSF_DEFS_H

#include <windows.h>
#include <msctf.h>
#include <initguid.h>

/* GoTV TSF Text Service CLSID:
 * {E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B} */
DEFINE_GUID(CLSID_GtvTextService,
    0xe3b0c442, 0x98fc, 0x4f2e, 0x9c, 0x8f, 0x7b, 0x2a, 0x3e, 0x1d, 0x4c, 0x5b);

/* GoTV Language Profile GUID:
 * {D4C5B6A7-1E2F-4A3B-8C9D-0E1F2A3B4C5D} */
DEFINE_GUID(GUID_GtvProfile,
    0xd4c5b6a7, 0x1e2f, 0x4a3b, 0x8c, 0x9d, 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d);

#define GTV_TSF_MODEL_NAME L"GoTV"
#define GTV_TSF_DESC       L"GoTV"

/* Vietnamese Language ID: 0x042A. GoTV registers under Vietnamese; the
 * taskbar badge shows VIE. Users add the vi language once in Settings. */
#define GTV_LANG_VIETNAMESE MAKELANGID(LANG_VIETNAMESE, SUBLANG_DEFAULT)
/* English (US) Language ID: 0x0409, retained for stale-profile removal. */
#define GTV_LANG_ENGLISH    MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)

/* GUID_TFCAT_TIP_TEXTSERVICE is not defined in MinGW's msctf.h.
 * A keyboard TIP must register for BOTH GUID_TFCAT_TIP_TEXTSERVICE and
 * GUID_TFCAT_TIP_KEYBOARD to appear in Windows language settings. */
#ifndef GUID_TFCAT_TIP_TEXTSERVICE
DEFINE_GUID(GUID_TFCAT_TIP_TEXTSERVICE,
    0x12A1D29F, 0xA065, 0x440C, 0x97, 0x46, 0xEB, 0x20, 0x02, 0xC2, 0xB6, 0x67);
#endif

/* ITfTextLayoutSink is missing in MinGW msctf.h headers.
 * Implemented by TIP to track asynchronous layout updates (Chromium / Electron). */
#ifndef __ITfTextLayoutSink_INTERFACE_DEFINED__
#define __ITfTextLayoutSink_INTERFACE_DEFINED__

typedef enum {
    TF_LC_CREATE = 0,
    TF_LC_CHANGE = 1,
    TF_LC_DESTROY = 2
} TfLayoutCode;

#undef INTERFACE
#define INTERFACE ITfTextLayoutSink
DECLARE_INTERFACE_(ITfTextLayoutSink, IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObj) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD(OnLayoutChange)(THIS_ ITfContext *pic, TfLayoutCode lcode, ITfContextView *pView) PURE;
};

#ifndef IID_ITfTextLayoutSink
DEFINE_GUID(IID_ITfTextLayoutSink,
    0x2288303a, 0x3347, 0x11d2, 0x8b, 0x71, 0x00, 0x60, 0x08, 0x06, 0xd9, 0xb9);
#endif

#endif /* __ITfTextLayoutSink_INTERFACE_DEFINED__ */

#endif /* GTV_TSF_DEFS_H */
