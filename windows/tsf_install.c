#include "tsf_install.h"

#include <windows.h>
#include "engine.h"
#include <stdio.h>
#include <glib.h>
#include <wchar.h>
#include <objbase.h>
#include <msctf.h>

static const wchar_t *GTV_TSF_CLSID_STR =
    L"{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}";
static const wchar_t *GTV_TSF_PROFILE_GUID_STR =
    L"{D4C5B6A7-1E2F-4A3B-8C9D-0E1F2A3B4C5D}";

static const wchar_t *GTV_TSF_INPROC_KEY =
    L"Software\\Classes\\CLSID\\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}\\InprocServer32";
/* 32-bit COM view: WOW64 processes resolve HKCU\Software\Classes through this
 * redirected branch. The x64 DLL cannot be loaded by a 32-bit host (Zalo,
 * legacy Electron), so the package ships a second stub there. */
static const wchar_t *GTV_TSF_INPROC_KEY32 =
    L"Software\\Classes\\Wow6432Node\\CLSID\\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}\\InprocServer32";
static const wchar_t *GTV_TSF_STUB32_NAME = L"gtv_tsf32.dll";

/* TSF profile keys: HKCU\Software\Microsoft\CTF\TIP\{CLSID}\LanguageProfile\{langid}\{profileguid}
 * If these don't exist, the keyboard won't appear in language settings.
 * The HKLM copy is the one that decides whether Windows KEEPS our tip in the
 * user's language list: with only the HKCU keys present, Set-WinUserLanguageList
 * silently drops 042A:{CLSID}{profile} and vi-VN ends up with zero keyboards,
 * so Win+Space shows nothing. Verified with EnumLanguageProfiles(0x042A). */
static const wchar_t *GTV_TSF_PROFILE_BASE =
    L"Software\\Microsoft\\CTF\\TIP\\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}\\LanguageProfile";
static const wchar_t *GTV_TSF_PROFILE_BASE_MACHINE =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}\\LanguageProfile";
static const wchar_t *GTV_TSF_LANG_VI = L"0x0000042a";

/* Category GUIDs: a keyboard TIP must register these, otherwise Win10/11
 * won't list it in language settings (see windows/tsf/tsf_register.cpp).
 * 4784… is the keyboard category Windows' own TIPs actually carry
 * (verified against HKLM\SOFTWARE\Microsoft\CTF\TIP on Win10);
 * 11D0… is the documented GUID_TFCAT_TIP_KEYBOARD value. */
static const wchar_t *GTV_TSF_CAT_KEYBOARD = L"{34745C63-B2F0-11D0-98EF-00AA006D2E36}";
static const wchar_t *GTV_TSF_CAT_KEYBOARD_4784 = L"{34745C63-B2F0-4784-8B67-5E12C8701A31}";
static const wchar_t *GTV_TSF_CAT_TEXTSERVICE = L"{12A1D29F-A065-440C-9746-EB2002C2B667}";
static const wchar_t *GTV_TSF_TIP_BASE =
    L"Software\\Microsoft\\CTF\\TIP\\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}";

static gchar *s_log_path = NULL;

static const gchar *get_log_path(void) {
    if (!s_log_path)
        s_log_path = gtv_user_path(g_get_user_data_dir(), "tsf_install.log");
    return s_log_path;
}

static void log_msg(const gchar *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gchar *msg = g_strdup_vprintf(fmt, args);
    va_end(args);

    gchar *dir = g_path_get_dirname(get_log_path());
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    FILE *f = fopen(get_log_path(), "a");
    if (f) {
        gchar *ts = g_strdup_printf("[%lu] ", (unsigned long)GetTickCount());
        fputs(ts, f);
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
        g_free(ts);
    }
    g_free(msg);
}

static gboolean same_path_ci(const wchar_t *a, const wchar_t *b) {
    wchar_t full_a[MAX_PATH];
    wchar_t full_b[MAX_PATH];

    if (!a || !b) return FALSE;
    if (!GetFullPathNameW(a, MAX_PATH, full_a, NULL)) return FALSE;
    if (!GetFullPathNameW(b, MAX_PATH, full_b, NULL)) return FALSE;
    return _wcsicmp(full_a, full_b) == 0;
}

static gboolean registered_to_key(const wchar_t *key_path, const wchar_t *dll_path) {
    HKEY hkey = NULL;
    wchar_t registered[MAX_PATH];
    DWORD type = 0;
    DWORD size = sizeof(registered);
    gboolean ok = FALSE;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, key_path, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        log_msg("InprocServer32 key not found under HKCU (%ls)", key_path);
        return FALSE;
    }

    if (RegQueryValueExW(hkey, NULL, NULL, &type, (BYTE *)registered, &size) == ERROR_SUCCESS &&
        type == REG_SZ && registered[0]) {
        registered[G_N_ELEMENTS(registered) - 1] = L'\0';
        ok = same_path_ci(registered, dll_path);
        if (!ok)
            log_msg("DLL path mismatch (registered=%ls expected=%ls)",
                    registered, dll_path);
    }

    RegCloseKey(hkey);
    return ok;
}

static gboolean registered_to(const wchar_t *dll_path) {
    return registered_to_key(GTV_TSF_INPROC_KEY, dll_path);
}

static gboolean registered_to_32(const wchar_t *dll_path) {
    return registered_to_key(GTV_TSF_INPROC_KEY32, dll_path);
}

static gboolean reg_key_exists_root(HKEY root, const wchar_t *subkey) {
    HKEY hkey = NULL;
    LONG rc = RegOpenKeyExW(root, subkey, 0, KEY_READ, &hkey);
    if (rc == ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return TRUE;
    }
    return FALSE;
}

static gboolean reg_key_exists(const wchar_t *subkey) {
    return reg_key_exists_root(HKEY_CURRENT_USER, subkey);
}

/* TRUE when the machine-level Vietnamese profile leaf exists: that is the
 * one ITfInputProcessorProfiles writes and the one Windows validates the
 * 042A:{CLSID}{profile} tip string against. */
static gboolean machine_profile_registered(void) {
    wchar_t key_vi[MAX_PATH];
    swprintf(key_vi, MAX_PATH, L"%ls\\%ls\\%ls",
             GTV_TSF_PROFILE_BASE_MACHINE, GTV_TSF_LANG_VI, GTV_TSF_PROFILE_GUID_STR);
    if (reg_key_exists_root(HKEY_LOCAL_MACHINE, key_vi))
        return TRUE;
    log_msg("No machine-level TSF profile for 0x042a (HKLM ...\\{profile})");
    return FALSE;
}

static gboolean tsf_profiles_registered(void) {
    wchar_t key_vi[MAX_PATH];
    swprintf(key_vi, MAX_PATH, L"%ls\\%ls\\%ls",
             GTV_TSF_PROFILE_BASE, GTV_TSF_LANG_VI, GTV_TSF_PROFILE_GUID_STR);
    if (reg_key_exists(key_vi))
        return TRUE;
    log_msg("No TSF language profiles found under HKCU");
    return FALSE;
}

static gboolean tsf_category_registered(void) {
    /* Accept any of the layouts Windows versions use; the manual fallback
     * writes all of them (see ensure_category_registered). */
    const wchar_t *cats[3] = { GTV_TSF_CAT_KEYBOARD, GTV_TSF_CAT_KEYBOARD_4784,
                               GTV_TSF_CAT_TEXTSERVICE };
    for (int i = 0; i < 3; i++) {
        wchar_t k1[MAX_PATH], k2[MAX_PATH], k3[MAX_PATH];
        swprintf(k1, MAX_PATH, L"%ls\\Category\\Category\\%ls\\%ls",
                 GTV_TSF_TIP_BASE, cats[i], GTV_TSF_CLSID_STR);
        swprintf(k2, MAX_PATH, L"%ls\\Category\\%ls\\%ls",
                 GTV_TSF_TIP_BASE, cats[i], GTV_TSF_CLSID_STR);
        swprintf(k3, MAX_PATH, L"%ls\\Category\\%ls",
                 GTV_TSF_TIP_BASE, cats[i]);
        if (reg_key_exists(k1) || reg_key_exists(k2) || reg_key_exists(k3))
            continue;
        return FALSE;
    }
    return TRUE;
}

/* Manually register a TSF language profile under HKCU.
 * Creates ...\LanguageProfile\{langid} then ...\{profileguid} in two
 * steps: RegCreateKeyEx on an open HKEY must not be given a multi-level
 * relative path with a backslash (fails on some Windows builds). */
static void ensure_profile_registered(const wchar_t *langid,
                                      const wchar_t *profile_guid,
                                      const wchar_t *description) {
    HKEY hKeyLang = NULL;
    HKEY hKeyProfile = NULL;
    wchar_t lang_key[MAX_PATH];
    LONG rc;

    swprintf(lang_key, MAX_PATH, L"%ls\\%ls", GTV_TSF_PROFILE_BASE, langid);
    rc = RegCreateKeyExW(HKEY_CURRENT_USER, lang_key,
                         0, NULL, 0, KEY_WRITE, NULL, &hKeyLang, NULL);
    if (rc != ERROR_SUCCESS) {
        log_msg("Failed to create profile lang key: %ld", (long)rc);
        return;
    }

    rc = RegCreateKeyExW(hKeyLang, profile_guid,
                         0, NULL, 0, KEY_WRITE, NULL, &hKeyProfile, NULL);
    if (rc != ERROR_SUCCESS) {
        log_msg("Failed to create profile guid subkey: %ld", (long)rc);
        RegCloseKey(hKeyLang);
        return;
    }

    rc = RegSetValueExW(hKeyProfile, NULL, 0, REG_SZ,
                        (const BYTE *)description,
                        (DWORD)((wcslen(description) + 1) * sizeof(wchar_t)));
    if (rc != ERROR_SUCCESS)
        log_msg("Failed to set profile description: %ld", (long)rc);

    rc = RegSetValueExW(hKeyProfile, L"Description", 0, REG_SZ,
                        (const BYTE *)description,
                        (DWORD)((wcslen(description) + 1) * sizeof(wchar_t)));
    if (rc != ERROR_SUCCESS)
        log_msg("Failed to set profile Description value: %ld", (long)rc);

    RegCloseKey(hKeyProfile);
    RegCloseKey(hKeyLang);

    log_msg("Profile registered under HKCU");
}

static void ensure_profiles_registered(void) {
    ensure_profile_registered(GTV_TSF_LANG_VI, GTV_TSF_PROFILE_GUID_STR, L"GoTV");
}

/* Remove the stale English profile so GoTV appears only under Vietnamese.
 * HKCU delete works unelevated; HKLM needs
 * admin (see gtv_tsf_register_elevated). Missing keys are fine. */
static void remove_stale_en_profile_tree(HKEY root) {
    wchar_t leaf[MAX_PATH], parent[MAX_PATH];
    LONG rc_leaf, rc_parent;
    swprintf(leaf, MAX_PATH, L"%ls\\0x00000409\\%ls",
             GTV_TSF_PROFILE_BASE, GTV_TSF_PROFILE_GUID_STR);
    swprintf(parent, MAX_PATH, L"%ls\\0x00000409", GTV_TSF_PROFILE_BASE);
    rc_leaf = RegDeleteKeyW(root, leaf);
    rc_parent = RegDeleteKeyW(root, parent);
    if (rc_leaf == ERROR_SUCCESS)
        log_msg("Removed stale en-US profile leaf (root=%p)", (void *)root);
    if (rc_parent == ERROR_SUCCESS)
        log_msg("Removed stale en-US profile key (root=%p)", (void *)root);
    /* A leftover en-US profile makes the switcher offer GoTV twice, so make
     * the failure visible instead of letting it pass silently. */
    if (rc_leaf != ERROR_SUCCESS && rc_leaf != ERROR_FILE_NOT_FOUND)
        log_msg("Failed to remove stale en-US profile leaf (rc=%ld)", (long)rc_leaf);
    if (rc_parent != ERROR_SUCCESS && rc_parent != ERROR_FILE_NOT_FOUND)
        log_msg("Failed to remove stale en-US profile key (rc=%ld)", (long)rc_parent);
}

/* Ensure BOTH keyboard and textservice categories are registered under
 * HKCU. Writes every layout Windows has used so at least one matches:
 *   TIP\{CLSID}\Category\Category\{cat}\{clsid}  (Win10 RegisterCategory)
 *   TIP\{CLSID}\Category\{cat}\{clsid}
 *   TIP\{CLSID}\Category\{cat}                   (legacy fallback) */
static void ensure_one_category(const wchar_t *cat_guid) {
    const wchar_t *layouts[3];
    wchar_t k_double[MAX_PATH], k_single[MAX_PATH], k_legacy[MAX_PATH];
    swprintf(k_double, MAX_PATH, L"%ls\\Category\\Category\\%ls\\%ls",
             GTV_TSF_TIP_BASE, cat_guid, GTV_TSF_CLSID_STR);
    swprintf(k_single, MAX_PATH, L"%ls\\Category\\%ls\\%ls",
             GTV_TSF_TIP_BASE, cat_guid, GTV_TSF_CLSID_STR);
    swprintf(k_legacy, MAX_PATH, L"%ls\\Category\\%ls",
             GTV_TSF_TIP_BASE, cat_guid);
    layouts[0] = k_double;
    layouts[1] = k_single;
    layouts[2] = k_legacy;
    for (int i = 0; i < 3; i++) {
        HKEY h = NULL;
        LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, layouts[i], 0, NULL, 0,
                                  KEY_WRITE, NULL, &h, NULL);
        if (rc != ERROR_SUCCESS) {
            log_msg("Failed to create category key (%d): %ld", i, (long)rc);
            continue;
        }
        rc = RegSetValueExW(h, NULL, 0, REG_SZ,
                            (const BYTE *)GTV_TSF_CLSID_STR,
                            (DWORD)((wcslen(GTV_TSF_CLSID_STR) + 1) * sizeof(wchar_t)));
        if (rc != ERROR_SUCCESS)
            log_msg("Failed to set category value (%d): %ld", i, (long)rc);
        RegCloseKey(h);
    }
    log_msg("Category registered under HKCU");
}

static void ensure_category_registered(void) {
    ensure_one_category(GTV_TSF_CAT_KEYBOARD);
    ensure_one_category(GTV_TSF_CAT_KEYBOARD_4784);
    ensure_one_category(GTV_TSF_CAT_TEXTSERVICE);
}

/* Call the official TSF COM APIs at startup to enable the profiles.
 * Registry keys alone leave the keyboard registered-but-disabled, so it
 * never becomes selectable in language settings. EnableLanguageProfile
 * flips the enabled bit under HKCU. Failures are logged, never fatal. */
static void enable_profiles_via_api(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    gboolean uninit = SUCCEEDED(hr);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        log_msg("CoInitializeEx failed: 0x%08lx", (unsigned long)hr);
        return;
    }

    ITfInputProcessorProfiles *pProfiles = NULL;
    hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfInputProcessorProfiles, (void **)&pProfiles);
    if (FAILED(hr) || !pProfiles) {
        log_msg("CoCreate ITfInputProcessorProfiles failed: 0x%08lx", (unsigned long)hr);
        if (uninit) CoUninitialize();
        return;
    }

    /* Best-effort re-register (usually done by regsvr32 already). */
    CLSID clsid = GUID_NULL;
    CLSIDFromString((LPOLESTR)GTV_TSF_CLSID_STR, &clsid);
    GUID guidProfile = GUID_NULL;
    CLSIDFromString((LPOLESTR)GTV_TSF_PROFILE_GUID_STR, &guidProfile);
    pProfiles->lpVtbl->Register(pProfiles, &clsid);

    const LANGID langs[1] = { 0x042A };
    for (int i = 0; i < 1; i++) {
        /* AddLanguageProfile is what actually writes
         * ...\CTF\TIP\{CLSID}\LanguageProfile\0x0000042a\{profile}.
         * EnableLanguageProfile alone only flips the per-user enabled bit, and
         * without the profile leaf Windows drops 042A:{CLSID}{profile} from
         * the user language list — the language then has no keyboard at all
         * and Win+Space shows nothing. It is idempotent (S_FALSE when the
         * profile is already there), so call it on every launch.
         * Unelevated: it writes HKLM only when it can; the installer runs
         * this once as admin for the machine-level copy. */
        hr = pProfiles->lpVtbl->AddLanguageProfile(pProfiles, &clsid, langs[i],
                                                   &guidProfile, L"GoTV", 4,
                                                   NULL, 0, 0);
        log_msg("AddLanguageProfile lang=0x%04x hr=0x%08lx", (unsigned)langs[i],
                (unsigned long)hr);
        hr = pProfiles->lpVtbl->EnableLanguageProfile(pProfiles, &clsid, langs[i],
                                                       &guidProfile, TRUE);
        log_msg("EnableLanguageProfile lang=0x%04x hr=0x%08lx", (unsigned)langs[i],
                (unsigned long)hr);
    }
    pProfiles->lpVtbl->Release(pProfiles);

    /* Register both categories via the official API as well. */
    ITfCategoryMgr *pCat = NULL;
    hr = CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfCategoryMgr, (void **)&pCat);
    if (SUCCEEDED(hr) && pCat) {
        GUID guidCatKB = GUID_NULL, guidCatKB2 = GUID_NULL, guidCatTS = GUID_NULL, guidClsid = clsid;
        CLSIDFromString((LPOLESTR)GTV_TSF_CAT_KEYBOARD, &guidCatKB);
        CLSIDFromString((LPOLESTR)GTV_TSF_CAT_KEYBOARD_4784, &guidCatKB2);
        CLSIDFromString((LPOLESTR)GTV_TSF_CAT_TEXTSERVICE, &guidCatTS);
        hr = pCat->lpVtbl->RegisterCategory(pCat, &clsid, &guidCatKB, &guidClsid);
        log_msg("RegisterCategory KEYBOARD hr=0x%08lx", (unsigned long)hr);
        hr = pCat->lpVtbl->RegisterCategory(pCat, &clsid, &guidCatKB2, &guidClsid);
        log_msg("RegisterCategory KEYBOARD-4784 hr=0x%08lx", (unsigned long)hr);
        hr = pCat->lpVtbl->RegisterCategory(pCat, &clsid, &guidCatTS, &guidClsid);
        log_msg("RegisterCategory TEXTSERVICE hr=0x%08lx", (unsigned long)hr);
        pCat->lpVtbl->Release(pCat);
    } else {
        log_msg("CoCreate ITfCategoryMgr failed: 0x%08lx", (unsigned long)hr);
    }

    if (uninit) CoUninitialize();
}

static wchar_t *prepend_app_dir_to_path(const wchar_t *app_dir) {
    DWORD old_len = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t *old_path = NULL;
    wchar_t *new_path = NULL;
    size_t new_len;

    if (old_len > 0) {
        old_path = g_new0(wchar_t, old_len + 1);
        GetEnvironmentVariableW(L"PATH", old_path, old_len + 1);
        new_len = wcslen(app_dir) + 1 + wcslen(old_path);
    } else {
        new_len = wcslen(app_dir);
    }

    new_path = g_new0(wchar_t, new_len + 1);
    if (old_path && old_path[0])
        swprintf(new_path, new_len + 1, L"%ls;%ls", app_dir, old_path);
    else
        swprintf(new_path, new_len + 1, L"%ls", app_dir);

    SetEnvironmentVariableW(L"PATH", new_path);
    g_free(new_path);
    return old_path;
}

static void restore_path(wchar_t *old_path) {
    if (old_path)
        SetEnvironmentVariableW(L"PATH", old_path);
    else
        SetEnvironmentVariableW(L"PATH", L"");
    g_free(old_path);
}

/* Run regsvr32 on a stub. The 64-bit and 32-bit stubs each register into their
 * own registry view, so this is called once per architecture. */
static gboolean run_regsvr32(const wchar_t *regsvr32_path, const wchar_t *dll_path,
                             const wchar_t *app_dir) {
    wchar_t command_line[MAX_PATH * 3];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    wchar_t *old_path = NULL;
    gboolean ok = FALSE;

    swprintf(command_line, G_N_ELEMENTS(command_line),
             L"\"%ls\" /s \"%ls\"", regsvr32_path, dll_path);
    log_msg("Running %ls /s %ls", regsvr32_path, dll_path);

    old_path = prepend_app_dir_to_path(app_dir);
    si.cb = sizeof(si);
    if (CreateProcessW(NULL, command_line, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, app_dir, &si, &pi)) {
        DWORD wait_rc = WaitForSingleObject(pi.hProcess, 15000);
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        ok = (wait_rc == WAIT_OBJECT_0 && exit_code == 0);
        log_msg("regsvr32 wait=%lu exit_code=%lu", (unsigned long)wait_rc,
                (unsigned long)exit_code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        log_msg("CreateProcessW regsvr32 failed: %lu", (unsigned long)GetLastError());
    }
    restore_path(old_path);
    return ok;
}

/* Register the 32-bit twin (gtv_tsf32.dll) in the WOW64 registry view. The
 * x64 stub cannot be loaded by 32-bit hosts, so without this Zalo and other
 * 32-bit apps get no GoTV keyboard at all. Returns TRUE when absent (old
 * payload) or successfully registered. */
static gboolean ensure_32bit_registered(const wchar_t *app_dir) {
    wchar_t dll32[MAX_PATH];
    wchar_t wow64_dir[MAX_PATH];
    wchar_t regsvr32_path[MAX_PATH];
    UINT n = 0;

    swprintf(dll32, MAX_PATH, L"%ls\\%ls", app_dir, GTV_TSF_STUB32_NAME);
    if (GetFileAttributesW(dll32) == INVALID_FILE_ATTRIBUTES) {
        log_msg("gtv_tsf32.dll not installed - skipping 32-bit TSF registration");
        return TRUE;
    }

    if (registered_to_32(dll32)) {
        log_msg("32-bit TSF registration OK");
        return TRUE;
    }

    n = GetSystemWow64DirectoryW(wow64_dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        log_msg("SysWOW64 directory unavailable - cannot register 32-bit TSF");
        return FALSE;
    }
    swprintf(regsvr32_path, MAX_PATH, L"%ls\\regsvr32.exe", wow64_dir);

    if (!run_regsvr32(regsvr32_path, dll32, app_dir)) {
        log_msg("WARNING: 32-bit TSF registration failed (32-bit apps like Zalo "
                "will not have GoTV)");
        return FALSE;
    }
    if (registered_to_32(dll32)) {
        log_msg("32-bit TSF registration verified OK");
        return TRUE;
    }
    log_msg("WARNING: 32-bit TSF registration not visible after regsvr32");
    return FALSE;
}

static gboolean app_paths(wchar_t *app_dir, wchar_t *dll_path) {
    wchar_t exe_path[MAX_PATH];
    wchar_t *slash = NULL;

    if (!GetModuleFileNameW(NULL, exe_path, MAX_PATH))
        return FALSE;

    wcscpy(app_dir, exe_path);
    slash = wcsrchr(app_dir, L'\\');
    if (!slash) return FALSE;
    *slash = L'\0';

    swprintf(dll_path, MAX_PATH, L"%ls\\gtv_tsf.dll", app_dir);
    if (GetFileAttributesW(dll_path) != INVALID_FILE_ATTRIBUTES)
        return TRUE;
    /* Versioned payload: the stable stub lives two levels up
     * ({app}\ver\<V>\exe -> {app}\gtv_tsf.dll). */
    {
        wchar_t *parent = wcsrchr(app_dir, L'\\');
        if (parent) {
            wchar_t grand[MAX_PATH];
            size_t plen = (size_t)(parent - app_dir);
            if (plen >= MAX_PATH) plen = MAX_PATH - 1;
            wcsncpy(grand, app_dir, plen);
            grand[plen] = L'\0';
            wchar_t *gbase = wcsrchr(grand, L'\\');
            gbase = gbase ? gbase + 1 : grand;
            if (!_wcsicmp(gbase, L"ver")) {
                wchar_t *up = wcsrchr(grand, L'\\');
                if (up) *up = L'\0';
                wcscpy(app_dir, grand);
                swprintf(dll_path, MAX_PATH, L"%ls\\gtv_tsf.dll", app_dir);
                if (GetFileAttributesW(dll_path) != INVALID_FILE_ATTRIBUTES)
                    return TRUE;
            }
        }
    }
    log_msg("DLL gtv_tsf.dll not found next to exe or app root");
    return FALSE;
}

void gtv_tsf_install_ensure_registered(void) {
    wchar_t app_dir[MAX_PATH];
    wchar_t dll_path[MAX_PATH];
    wchar_t system_dir[MAX_PATH];
    wchar_t regsvr32_path[MAX_PATH];
    gboolean need64 = TRUE;

    log_msg("=== gtv_tsf_install_ensure_registered ===");

    if (!app_paths(app_dir, dll_path)) {
        log_msg("app_paths failed - DLL missing or cannot determine app dir");
        return;
    }

    log_msg("app dir resolved, checking TSF registration");
    remove_stale_en_profile_tree(HKEY_CURRENT_USER);

    /* Re-register if: InprocServer32 path mismatch OR TSF profiles missing
     * OR category missing. NOTE: RegQueryValueExW takes a value name, not
     * a key path, so the check must use RegOpenKeyEx (see
     * tsf_category_registered). */
    gboolean inproc_ok = registered_to(dll_path);
    gboolean profiles_ok = tsf_profiles_registered();
    gboolean category_ok = tsf_category_registered();

    if (inproc_ok && profiles_ok && category_ok) {
        log_msg("Inproc+profiles+category present - enabling profiles via API");
        enable_profiles_via_api();
        if (registered_to(dll_path) && tsf_profiles_registered() &&
            tsf_category_registered()) {
            log_msg("All 64-bit TSF registrations OK - skipping regsvr32");
            need64 = FALSE;
        } else {
            log_msg("Enable via API incomplete - will re-register");
        }
    } else {
        log_msg("Missing registration: inproc=%d profiles=%d category=%d - will re-register",
                inproc_ok, profiles_ok, category_ok);
    }

    if (need64) {
        /* regsvr32 first (registers COM class + calls DllRegisterServer), then
         * manual HKCU fallback + EnableLanguageProfile (makes it selectable). */
        if (GetSystemDirectoryW(system_dir, MAX_PATH))
            swprintf(regsvr32_path, MAX_PATH, L"%ls\\regsvr32.exe", system_dir);
        else
            wcscpy(regsvr32_path, L"regsvr32.exe");

        run_regsvr32(regsvr32_path, dll_path, app_dir);

        ensure_profiles_registered();
        remove_stale_en_profile_tree(HKEY_CURRENT_USER);
        ensure_category_registered();
        enable_profiles_via_api();

        /* Verify after registration */
        if (registered_to(dll_path) && tsf_profiles_registered() && tsf_category_registered())
            log_msg("TSF registration verified OK");
        else
            log_msg("WARNING: TSF registration may have failed - check logs");
    }

    /* Always check the 32-bit view separately: a healthy 64-bit install says
     * nothing about WOW64 hosts such as Zalo. */
    ensure_32bit_registered(app_dir);

    /* The HKLM profile leaf needs admin to write. Without it Windows drops
     * GoTV from the vi language, so point the user at the one command that
     * fixes it instead of leaving an empty language in the switcher. */
    if (!machine_profile_registered())
        log_msg("WARNING: machine profile missing - run: gotv.exe --register-tsf "
                "(admin) or reinstall; vi-VN will show no keyboard until then");
}

/* TRUE when EnumLanguageProfiles(0x042A) returns our profile, i.e. the
 * OS genuinely recognises GoTV as a Vietnamese keyboard. Registry keys
 * alone are not enough (per-user Register/AddLanguageProfile fail). */
static gboolean profile_enumerated(void) {
    gboolean found = FALSE;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    gboolean uninit = SUCCEEDED(hr);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE)
        return FALSE;

    CLSID clsid = GUID_NULL;
    CLSIDFromString((LPOLESTR)GTV_TSF_CLSID_STR, &clsid);

    ITfInputProcessorProfiles *p = NULL;
    hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfInputProcessorProfiles, (void **)&p);
    if (SUCCEEDED(hr) && p) {
        IEnumTfLanguageProfiles *e = NULL;
        if (SUCCEEDED(p->lpVtbl->EnumLanguageProfiles(p, 0x042A, &e)) && e) {
            TF_LANGUAGEPROFILE pr;
            while (e->lpVtbl->Next(e, 1, &pr, NULL) == S_OK) {
                if (IsEqualGUID(&pr.clsid, &clsid)) {
                    found = TRUE;
                    break;
                }
            }
            e->lpVtbl->Release(e);
        }
        p->lpVtbl->Release(p);
    }
    if (uninit) CoUninitialize();
    return found;
}

gboolean gtv_tsf_register_elevated(void) {
    wchar_t app_dir[MAX_PATH];
    wchar_t dll_path[MAX_PATH];
    gboolean ok_all = TRUE;

    log_msg("=== gtv_tsf_register_elevated (admin) ===");
    if (!app_paths(app_dir, dll_path)) {
        log_msg("app_paths failed - cannot find gtv_tsf.dll");
        return FALSE;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    gboolean uninit = SUCCEEDED(hr);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        log_msg("CoInitializeEx failed: 0x%08lx", (unsigned long)hr);
        return FALSE;
    }

    CLSID clsid = GUID_NULL;
    CLSIDFromString((LPOLESTR)GTV_TSF_CLSID_STR, &clsid);
    GUID guidProfile = GUID_NULL;
    CLSIDFromString((LPOLESTR)GTV_TSF_PROFILE_GUID_STR, &guidProfile);

    ITfInputProcessorProfiles *p = NULL;
    hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfInputProcessorProfiles, (void **)&p);
    if (FAILED(hr) || !p) {
        log_msg("CoCreate Profiles failed: 0x%08lx", (unsigned long)hr);
        if (uninit) CoUninitialize();
        return FALSE;
    }

    hr = p->lpVtbl->Register(p, &clsid);
    log_msg("Register hr=0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) ok_all = FALSE;

    const LANGID langs[1] = { 0x042A };
    for (int i = 0; i < 1; i++) {
        hr = p->lpVtbl->AddLanguageProfile(p, &clsid, langs[i], &guidProfile,
                                           L"GoTV", 4, dll_path,
                                           (ULONG)wcslen(dll_path), 0);
        log_msg("AddLanguageProfile 0x%04x hr=0x%08lx", (unsigned)langs[i], (unsigned long)hr);
        if (FAILED(hr)) ok_all = FALSE;
        hr = p->lpVtbl->EnableLanguageProfile(p, &clsid, langs[i], &guidProfile, TRUE);
        log_msg("EnableLanguageProfile 0x%04x hr=0x%08lx", (unsigned)langs[i], (unsigned long)hr);
    }
    hr = p->lpVtbl->RemoveLanguageProfile(p, &clsid, 0x0409, &guidProfile);
    log_msg("RemoveLanguageProfile 0x0409 hr=0x%08lx", (unsigned long)hr);
    p->lpVtbl->Release(p);

    ITfCategoryMgr *c = NULL;
    hr = CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfCategoryMgr, (void **)&c);
    if (SUCCEEDED(hr) && c) {
        const wchar_t *cats[3] = { GTV_TSF_CAT_KEYBOARD_4784, GTV_TSF_CAT_KEYBOARD,
                                   GTV_TSF_CAT_TEXTSERVICE };
        for (int i = 0; i < 3; i++) {
            GUID g = GUID_NULL;
            CLSIDFromString((LPOLESTR)cats[i], &g);
            hr = c->lpVtbl->RegisterCategory(c, &clsid, &g, &clsid);
            log_msg("RegisterCategory #%d hr=0x%08lx", i, (unsigned long)hr);
            if (FAILED(hr)) ok_all = FALSE;
        }
        c->lpVtbl->Release(c);
    } else {
        log_msg("CoCreate CategoryMgr failed: 0x%08lx", (unsigned long)hr);
        ok_all = FALSE;
    }

    if (uninit) CoUninitialize();

    /* HKCU fallback + enable so the current user gets it immediately. */
    ensure_profiles_registered();
    remove_stale_en_profile_tree(HKEY_CURRENT_USER);
    remove_stale_en_profile_tree(HKEY_LOCAL_MACHINE);
    ensure_category_registered();
    enable_profiles_via_api();

    if (profile_enumerated()) {
        log_msg("Elevated registration verified: GoTV enumerates for 0x042A");
        return TRUE;
    }
    log_msg("WARNING: GoTV still not enumerated (ok_all=%d)", ok_all);
    return FALSE;
}
