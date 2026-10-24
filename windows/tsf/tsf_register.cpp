#include <windows.h>
#include <ole2.h>
#include <msctf.h>
#include "tsf_defs.h"
#include "tsf_service.h"

extern "C" HINSTANCE gtv_stub_instance(void);

/* Per-user COM registration under HKCU\Software\Classes: the installer
 * runs without elevation (PrivilegesRequired=lowest), so HKLM writes
 * would fail silently and the service would never activate. HKCU classes
 * take precedence over HKLM for COM lookup. */
static const WCHAR *const kClassesRoot = L"Software\\Classes\\CLSID\\";

static BOOL RegisterServerKeys(LPCWSTR szClsid, LPCWSTR szModule)
{
    WCHAR szKey[256];
    HKEY hKey;
    BOOL ok = TRUE;

    wsprintfW(szKey, L"%ls%ls", kClassesRoot, szClsid);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, szKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return FALSE;
    if (RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)GTV_TSF_MODEL_NAME, (DWORD)((wcslen(GTV_TSF_MODEL_NAME) + 1) * sizeof(WCHAR))) != ERROR_SUCCESS)
        ok = FALSE;
    RegCloseKey(hKey);

    wsprintfW(szKey, L"%ls%ls\\InprocServer32", kClassesRoot, szClsid);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, szKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return FALSE;
    if (RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)szModule, (DWORD)((wcslen(szModule) + 1) * sizeof(WCHAR))) != ERROR_SUCCESS)
        ok = FALSE;
    else {
        LPCWSTR szModel = L"Apartment";
        if (RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)szModel, (DWORD)((wcslen(szModel) + 1) * sizeof(WCHAR))) != ERROR_SUCCESS)
            ok = FALSE;
    }
    RegCloseKey(hKey);
    return ok;
}

static void UnregisterServerKeys(LPCWSTR szClsid)
{
    WCHAR szKey[256];
    wsprintfW(szKey, L"%ls%ls\\InprocServer32", kClassesRoot, szClsid);
    RegDeleteKeyW(HKEY_CURRENT_USER, szKey);
    wsprintfW(szKey, L"%ls%ls", kClassesRoot, szClsid);
    RegDeleteKeyW(HKEY_CURRENT_USER, szKey);
}

STDAPI DllRegisterServer(void)
{
    HINSTANCE hinst = gtv_stub_instance();
    if (!hinst) hinst = GetModuleHandleW(NULL);
    WCHAR szModule[MAX_PATH];
    if (!GetModuleFileNameW(hinst, szModule, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    WCHAR szClsid[64];
    if (!StringFromGUID2(CLSID_GtvTextService, szClsid, 64))
        return E_FAIL;

    if (!RegisterServerKeys(szClsid, szModule))
        return E_FAIL;

    // Register TSF Profiles
    ITfInputProcessorProfiles *pProfiles = NULL;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, (void**)&pProfiles);

    if (SUCCEEDED(hr) && pProfiles) {
        hr = pProfiles->Register(CLSID_GtvTextService);

        // Register under Vietnamese (0x042A) with description "GoTV"
        pProfiles->AddLanguageProfile(CLSID_GtvTextService,
            GTV_LANG_VIETNAMESE,
            GUID_GtvProfile,
            GTV_TSF_DESC,
            (ULONG)wcslen(GTV_TSF_DESC),
            szModule,
            (ULONG)wcslen(szModule),
            0);
        pProfiles->EnableLanguageProfile(CLSID_GtvTextService, GTV_LANG_VIETNAMESE, GUID_GtvProfile, TRUE);

        // Some Windows apps only activate TIPs registered for the current
        // input language. Keep the Vietnamese profile, and also expose the
        // same keyboard under en-US for systems/apps that never switch to vi-VN.
        pProfiles->AddLanguageProfile(CLSID_GtvTextService,
            GTV_LANG_ENGLISH,
            GUID_GtvProfile,
            GTV_TSF_DESC,
            (ULONG)wcslen(GTV_TSF_DESC),
            szModule,
            (ULONG)wcslen(szModule),
            0);
        pProfiles->EnableLanguageProfile(CLSID_GtvTextService, GTV_LANG_ENGLISH, GUID_GtvProfile, TRUE);
        pProfiles->Release();
    }

    // Register TSF Categories (keyboard TIP only; the display-attribute
    // provider category is intentionally not claimed: QueryInterface does
    // not implement ITfDisplayAttributeProvider).
    ITfCategoryMgr *pCategoryMgr = NULL;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, (void**)&pCategoryMgr);

    if (SUCCEEDED(hr) && pCategoryMgr) {
        pCategoryMgr->RegisterCategory(CLSID_GtvTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_GtvTextService);
        pCategoryMgr->Release();
    }

    return S_OK;
}

STDAPI DllUnregisterServer(void)
{
    WCHAR szClsid[64];
    if (!StringFromGUID2(CLSID_GtvTextService, szClsid, 64))
        return E_FAIL;

    // Unregister TSF Categories
    ITfCategoryMgr *pCategoryMgr = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, (void**)&pCategoryMgr)) && pCategoryMgr) {
        pCategoryMgr->UnregisterCategory(CLSID_GtvTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_GtvTextService);
        pCategoryMgr->Release();
    }

    // Unregister TSF Profiles
    ITfInputProcessorProfiles *pProfiles = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, (void**)&pProfiles)) && pProfiles) {
        pProfiles->Unregister(CLSID_GtvTextService);
        pProfiles->Release();
    }

    UnregisterServerKeys(szClsid);
    return S_OK;
}
