/* Stable TSF stub (gtv_tsf.dll): the only module Windows ever registers.
 *
 * The stub is installed once at {app}\gtv_tsf.dll and (almost) never
 * replaced, so updates never fight locked files, never re-register, need
 * no UAC and no logoff. It forwards DllGetClassObject/DllCanUnloadNow to
 * the versioned engine ({app}\ver\<current>\gtv_engine.dll), resolved
 * through {app}\current.txt at load time:
 *
 *   {app}\
 *     gtv_tsf.dll       this stub (registered path, stable)
 *     current.txt       e.g. "0.8.12"
 *     ver\<V>\          payload incl. gtv_engine.dll
 *
 * Old processes keep the old engine mapped until they exit; new processes
 * load the current one. Same model as Chrome's updater, no forced closes.
 *
 * Pure Win32 (no glib, no runtime DLL at all): the engine this stub forwards
 * to is self-contained - glib/gio/zlib are statically linked INTO
 * gtv_engine.dll (see GLIB_STATIC_DEFS in Makefile.win). That is deliberate:
 * Windows injects this DLL into every text-input process, and an injected DLL
 * that imported e.g. zlib1.dll would be hijacked by any host app shipping its
 * own same-named DLL (Microsoft Teams does, exporting only z_deflate).
 */
#include <windows.h>
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <ole2.h>
#include "tsf_defs.h"

typedef HRESULT(STDAPICALLTYPE *PFN_GETCLASSOBJECT)(REFCLSID, REFIID, void **);
typedef HRESULT(STDAPICALLTYPE *PFN_CANUNLOADNOW)(void);

/* The 64-bit stub forwards to gtv_engine.dll; the 32-bit twin built from the
 * same source is compiled with GTV_ENGINE_DLL_FILE=gtv_engine32.dll so both
 * payloads can share ver\<V>\. Stringify instead of embedding quotes in the
 * compiler flag: make -> sh -> gcc quoting is fragile on Windows shells. */
#define GTV_STR_(x) #x
#define GTV_STR(x) GTV_STR_(x)
#ifndef GTV_ENGINE_DLL_FILE
#define GTV_ENGINE_DLL_FILE gtv_engine.dll
#endif
#define GTV_ENGINE_DLL_NAME L"" GTV_STR(GTV_ENGINE_DLL_FILE)

static HINSTANCE g_hStub = NULL;
static INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;
static HMODULE g_hEngine = NULL;
static PFN_GETCLASSOBJECT g_pfnGetClassObject = NULL;
static PFN_CANUNLOADNOW g_pfnCanUnloadNow = NULL;

/* The registration code (tsf_register.cpp, same DLL) needs our path. */
extern "C" HINSTANCE gtv_stub_instance(void) { return g_hStub; }

/* intermittently-used buffer helpers (stack based, no CRT dependency issues) */
static void dir_name(WCHAR *path) {
    size_t n = 0, last = 0;
    for (size_t i = 0; path[i]; i++) {
        if (path[i] == L'\\' || path[i] == L'/') last = i;
        n = i + 1;
    }
    (void)n;
    path[last] = L'\0';
}

/* Resolve {app}\ver\<current>\gtv_engine.dll into out (cch chars).
 * Returns FALSE when current.txt is missing (dev/legacy layout). */
static BOOL resolve_engine_path(WCHAR *out, DWORD cch) {
    WCHAR stub[MAX_PATH];
    if (!GetModuleFileNameW(g_hStub, stub, MAX_PATH)) return FALSE;
    dir_name(stub); /* {app} */

    WCHAR cur[MAX_PATH];
    wsprintfW(cur, L"%ls\\current.txt", stub);
    HANDLE h = CreateFileW(cur, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    char buf[64];
    DWORD rd = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &rd, NULL);
    CloseHandle(h);
    if (!ok || rd == 0) return FALSE;
    buf[rd] = '\0';
    /* Strip whitespace/newlines (both ends). */
    char *s = buf, *e = buf + rd;
    while (s < e && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    *e = '\0';
    if (!*s) return FALSE;
    WCHAR wver[64];
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, wver, 64)) return FALSE;
    wsprintfW(out, L"%ls\\ver\\%ls\\%ls", stub, wver, GTV_ENGINE_DLL_NAME);
    return out[0] != L'\0' && cch > 0;
}

static BOOL CALLBACK load_engine(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    WCHAR path[MAX_PATH];
    if (!resolve_engine_path(path, MAX_PATH)) return FALSE;
    /* LOAD_WITH_ALTERED_SEARCH_PATH: the engine's sibling runtime DLLs
     * (glib etc.) live next to it in the versioned dir. */
    HMODULE h = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) return FALSE;
    /* FARPROC -> function pointer: the canonical COM forwarding pattern. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    PFN_GETCLASSOBJECT gco =
        (PFN_GETCLASSOBJECT)GetProcAddress(h, "DllGetClassObject");
#pragma GCC diagnostic pop
    if (!gco) {
        FreeLibrary(h);
        return FALSE;
    }
    g_pfnCanUnloadNow = NULL;
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        g_pfnCanUnloadNow =
            (PFN_CANUNLOADNOW)GetProcAddress(h, "DllCanUnloadNow");
#pragma GCC diagnostic pop
    }
    g_pfnGetClassObject = gco;
    g_hEngine = h; /* intentionally leaked: engine lives with the process */
    return TRUE;
}

static BOOL ensure_engine(void) {
    return InitOnceExecuteOnce(&g_initOnce, load_engine, NULL, NULL) &&
           g_pfnGetClassObject != NULL;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hStub = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    (void)lpvReserved;
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (!IsEqualCLSID(rclsid, CLSID_GtvTextService))
        return CLASS_E_CLASSNOTAVAILABLE;
    if (!ensure_engine()) return E_FAIL;
    return g_pfnGetClassObject(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow(void) {
    if (g_pfnCanUnloadNow) return g_pfnCanUnloadNow();
    return S_OK;
}
