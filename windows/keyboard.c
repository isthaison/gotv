/* Windows keyboard (Win+Space switcher) manager.
 *
 * The user's keyboard list is a Windows-owned structure: a list of languages,
 * each with an ordered list of InputMethodTips. It is read and written
 * through Get/Set-WinUserLanguageList, because raw registry writes to the
 * input stack are silently ignored (same reason input_setup.c goes through
 * powershell rather than touching HKCU directly).
 *
 * "What can I add" needs two different sources, because Windows splits the
 * catalog in two:
 *   - TSF profiles (GoTV, Vietnamese Telex, ...): ITfInputProcessorProfiles
 *     -> EnumLanguageProfiles. Verified to return nothing for 0x0409, i.e.
 *     plain keyboard layouts are NOT TSF profiles.
 *   - classic keyboard layouts ("US", "French (Legacy, AZERTY)"): the
 *     registry key is literally "Keyboard Layouts" (with a space) -
 *     "Keyboard\Layouts" does not exist and is a common wrong guess. Subkeys
 *     are KLIDs where the LOW word is the langid and the high word is the
 *     layout variant (00000409 = US, 00020409 = US-International), so the
 *     KLID already tells us which language it belongs to. */
#include "keyboard.h"
#include "tray.h"
#include "engine.h"

#include <windows.h>
/* These are used directly (wcstoul / wcsncpy / swprintf / strtoul / strchr /
 * vfprintf / va_list). Relying on transitive includes works at -std=gnu17 but
 * silently changes behaviour at -std=gnu11, which is what the app builds
 * with - so name every one of them. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <objbase.h>
#include <msctf.h>

/* GoTV, same GUIDs as tsf_install.c / input_setup.c. The TIP string is the
 * format Windows itself uses: langid:{CLSID}{profile}. */
#define GTV_TIP_VI "042a:{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}" \
                  "{D4C5B6A7-1E2F-4A3B-8C9D-0E1F2A3B4C5D}"
#define GTV_LANG_VI 0x042a

/* The layout catalog lives here; note the space in "Keyboard Layouts". */
static const wchar_t *KBD_LAYOUTS_KEY =
    L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";

static gboolean is_gtv_vi_tip(const gchar *tip) {
    return tip && (g_strcmp0(tip, GTV_TIP_VI) == 0 ||
                   g_str_has_prefix(tip, "042a:{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}"));
}

static LANGID tip_langid(const gchar *tip);

static void kbd_log(const gchar *fmt, ...) {
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    g_mkdir_with_parents(dir, 0755);
    gchar *path = g_build_filename(dir, "keyboard.log", NULL);
    g_free(dir);
    /* Append: one save emits several lines and each line must survive. */
    FILE *f = g_fopen(path, "a");
    g_free(path);
    if (!f) return;
    va_list fargs;
    va_start(fargs, fmt);
    vfprintf(f, fmt, fargs);
    va_end(fargs);
    fputc('\n', f);
    fclose(f);
}

/* ------------------------------------------------------------------ model */

static void kbd_free(gpointer p) {
    GtvKbd *k = p;
    g_free(k->lang);
    g_free(k->tip);
}

GtvKbdList *gtv_kbd_list_new(void) {
    GtvKbdList *list = g_new0(GtvKbdList, 1);
    list->items = g_array_new(FALSE, FALSE, sizeof(GtvKbd));
    g_array_set_clear_func(list->items, kbd_free);
    return list;
}

void gtv_kbd_list_free(GtvKbdList *list) {
    if (!list) return;
    g_array_free(list->items, TRUE);
    g_free(list);
}

void gtv_kbd_list_clear(GtvKbdList *list) {
    if (list) g_array_set_size(list->items, 0);
}

void gtv_kbd_list_add(GtvKbdList *list, const gchar *lang, const gchar *tip) {
    GtvKbd k = { g_strdup(lang), g_strdup(tip) };
    g_array_append_val(list->items, k);
}

void gtv_kbd_list_copy(GtvKbdList *dst, const GtvKbdList *src) {
    gtv_kbd_list_clear(dst);
    for (guint i = 0; i < src->items->len; i++) {
        const GtvKbd *k = &g_array_index(src->items, GtvKbd, i);
        gtv_kbd_list_add(dst, k->lang, k->tip);
    }
}

void gtv_kbd_list_move(GtvKbdList *list, guint from, guint to) {
    GArray *a = list->items;
    if (from == to || from >= a->len || to >= a->len) return;
    /* Straight pointer shuffle. Do NOT use g_array_remove_index + insert:
     * remove_index runs the clear func, which frees the moved entry's
     * strings, so re-inserting that struct would leave dangling pointers.
     * Moving down shifts the entries in between one slot towards 0, moving
     * up shifts them towards the end, and only then is the moved struct
     * dropped into place (which keeps every string owned exactly once). */
    GtvKbd moved = g_array_index(a, GtvKbd, from);
    if (to > from) {
        for (guint i = from; i < to; i++)
            g_array_index(a, GtvKbd, i) = g_array_index(a, GtvKbd, i + 1);
    } else {
        for (guint i = from; i > to; i--)
            g_array_index(a, GtvKbd, i) = g_array_index(a, GtvKbd, i - 1);
    }
    g_array_index(a, GtvKbd, to) = moved;
}

gboolean gtv_kbd_list_has(const GtvKbdList *list, const gchar *tip) {
    for (guint i = 0; i < list->items->len; i++) {
        const GtvKbd *k = &g_array_index(list->items, GtvKbd, i);
        if (!g_ascii_strcasecmp(k->tip, tip)) return TRUE;
    }
    return FALSE;
}

gboolean gtv_kbd_list_equals(const GtvKbdList *a, const GtvKbdList *b) {
    if (a->items->len != b->items->len) return FALSE;
    for (guint i = 0; i < a->items->len; i++) {
        const GtvKbd *x = &g_array_index(a->items, GtvKbd, i);
        const GtvKbd *y = &g_array_index(b->items, GtvKbd, i);
        if (g_ascii_strcasecmp(x->tip, y->tip) != 0) return FALSE;
        if (g_ascii_strcasecmp(x->lang, y->lang) != 0) return FALSE;
    }
    return TRUE;
}

const GtvKbd *gtv_kbd_list_get(const GtvKbdList *list, guint index) {
    if (index >= list->items->len) return NULL;
    return &g_array_index(list->items, GtvKbd, index);
}

/* Split "042a:{CLSID}{profile}" or "0409:00000409". Returns FALSE when the
 * string is not a TIP at all. */
static gboolean tip_split(const gchar *tip, gchar **lang_out, gchar **rest_out) {
    const gchar *colon = strchr(tip, ':');
    if (!colon || colon - tip != 4) return FALSE;
    if (colon[1] == '\0') return FALSE;
    *lang_out = g_strndup(tip, 4);
    *rest_out = g_strdup(colon + 1);
    return TRUE;
}

/* "{CLSID}{profile}" is 76 chars: '}' closes the first GUID at index 37, so
 * the second '{' is at 38 (getting that index wrong sends every TSF tip
 * down the classic-layout path and every name comes back unresolved). */
static gboolean rest_is_guid_pair(const gchar *rest) {
    return strlen(rest) == 76 && rest[0] == '{' && rest[37] == '}' && rest[38] == '{';
}

/* Display name for a classic layout: "Layout Text" is a plain string, so no
 * indirect-string resolution is needed (Layout Display Name is the
 * "@input.dll,-5000" form, which only SHLoadIndirectString can expand). */
static gchar *layout_text_name(const gchar *klid) {
    gunichar2 *wklid = g_utf8_to_utf16(klid, -1, NULL, NULL, NULL);
    if (!wklid) return NULL;
    wchar_t wpath[512];
    swprintf(wpath, 512, L"%ls\\%ls", KBD_LAYOUTS_KEY, (const wchar_t *)wklid);
    g_free(wklid);

    gchar *value = NULL;
    for (int i = 0; i < 2; i++) {
        HKEY root = (i == 0) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        HKEY hkey = NULL;
        if (RegOpenKeyExW(root, wpath, 0, KEY_READ, &hkey) != ERROR_SUCCESS) continue;
        DWORD type = 0, size = 0;
        if (RegQueryValueExW(hkey, L"Layout Text", NULL, &type, NULL, &size) == ERROR_SUCCESS &&
            type == REG_SZ && size > 0) {
            WCHAR *buf = g_new0(WCHAR, size / sizeof(WCHAR) + 1);
            if (RegQueryValueExW(hkey, L"Layout Text", NULL, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS)
                value = g_utf16_to_utf8((const gunichar2 *)buf, -1, NULL, NULL, NULL);
            g_free(buf);
        }
        RegCloseKey(hkey);
        if (value && *value) return value;
        g_clear_pointer(&value, g_free);
    }
    return NULL;
}

/* Read a string value out of a CTF profile key, trying each root in turn.
 * The description lives in different places depending on who registered the
 * profile: HKCU has it as the key's default value, while the machine-level
 * copy written by tsf_install.c uses a named "Description" value and leaves
 * the default empty. Reading only the default therefore resolves nothing
 * whenever the per-user copy has been pruned. */
static gchar *profile_key_string(const wchar_t *wpath) {
    static const wchar_t *names[] = { L"", L"Description" };
    for (int root_i = 0; root_i < 2; root_i++) {
        HKEY root = (root_i == 0) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
        HKEY hkey = NULL;
        if (RegOpenKeyExW(root, wpath, 0, KEY_READ, &hkey) != ERROR_SUCCESS) continue;
        for (guint n = 0; n < G_N_ELEMENTS(names); n++) {
            DWORD type = 0, size = 0;
            if (RegQueryValueExW(hkey, names[n], NULL, &type, NULL, &size) != ERROR_SUCCESS)
                continue;
            if (type != REG_SZ || size == 0) continue;
            WCHAR *buf = g_new0(WCHAR, size / sizeof(WCHAR) + 1);
            gchar *out = NULL;
            if (RegQueryValueExW(hkey, names[n], NULL, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS)
                out = g_utf16_to_utf8((const gunichar2 *)buf, -1, NULL, NULL, NULL);
            g_free(buf);
            if (out && *out) { RegCloseKey(hkey); return out; }
            g_free(out);
        }
        RegCloseKey(hkey);
    }
    return NULL;
}

/* Display name for a TSF profile. Prefers what the TSF API itself reports
 * (localized, and correct even when the registry copy is stale or pruned),
 * then falls back to the registry. */
static gchar *tsf_profile_name(const gchar *tip) {
    gchar *lang = NULL, *rest = NULL;
    if (!tip_split(tip, &lang, &rest) || !rest_is_guid_pair(rest)) {
        g_free(lang); g_free(rest);
        return NULL;
    }
    gunichar2 *wrest = g_utf8_to_utf16(rest, -1, NULL, NULL, NULL);
    g_free(lang);
    g_free(rest);
    if (!wrest) return NULL;

    CLSID clsid = GUID_NULL;
    GUID profile = GUID_NULL;
    /* rest is "{CLSID}{profile}"; parse each half on its own. */
    gunichar2 *head = g_new0(gunichar2, 39);
    memcpy(head, wrest, 38 * sizeof(gunichar2));
    CLSIDFromString((LPCOLESTR)head, &clsid);
    g_free(head);
    gunichar2 *tail = g_new0(gunichar2, 39);
    memcpy(tail, wrest + 38, 38 * sizeof(gunichar2));
    IIDFromString((LPCOLESTR)tail, &profile);
    g_free(tail);

    /* The TIP's own langid is authoritative. */
    LANGID langid = (LANGID)strtoul(tip, NULL, 16);

    gchar *name = NULL;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    gboolean uninit = SUCCEEDED(hr);
    if (hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
        ITfInputProcessorProfiles *profiles = NULL;
        if (SUCCEEDED(CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL,
                                       CLSCTX_INPROC_SERVER, &IID_ITfInputProcessorProfiles,
                                       (void **)&profiles)) && profiles) {
            BSTR desc = NULL;
            if (SUCCEEDED(profiles->lpVtbl->GetLanguageProfileDescription(
                    profiles, &clsid, langid, &profile, &desc)) && desc) {
                name = g_utf16_to_utf8((const gunichar2 *)desc, -1, NULL, NULL, NULL);
                SysFreeString(desc);
            }
            profiles->lpVtbl->Release(profiles);
        }
        if (uninit) CoUninitialize();
    }
    if (name && *name) {
        if (is_gtv_vi_tip(tip)) {
            g_free(name);
            g_free(wrest);
            return g_strdup("Vietnamese (GoTV)");
        }
        g_free(wrest);
        return name;
    }
    g_clear_pointer(&name, g_free);

    /* Registry fallback. */
    wchar_t wcls[64], wprof[64], wleaf[16], wpath[512];
    wcsncpy(wcls, (const wchar_t *)wrest, 38); wcls[38] = L'\0';
    wcsncpy(wprof, (const wchar_t *)wrest + 38, 38); wprof[38] = L'\0';
    swprintf(wleaf, 16, L"0x%08x", (unsigned)langid);
    swprintf(wpath, 512, L"Software\\Microsoft\\CTF\\TIP\\%ls\\LanguageProfile\\%ls\\%ls",
             wcls, wleaf, wprof);
    g_free(wrest);
    return profile_key_string(wpath);
}

gchar *gtv_kbd_tip_name(const gchar *tip) {
    if (is_gtv_vi_tip(tip)) return g_strdup("Vietnamese (GoTV)");
    gchar *lang = NULL, *rest = NULL;
    if (!tip_split(tip, &lang, &rest)) return NULL;
    gboolean is_tip = rest_is_guid_pair(rest);
    gchar *name = is_tip ? tsf_profile_name(tip) : layout_text_name(rest);
    g_free(lang);
    g_free(rest);
    return name;
}

/* ---------------------------------------------------------------- catalog */

static void choice_free_cb(gpointer p) {
    GtvKbdChoice *c = p;
    g_free(c->tip);
    g_free(c->name);
    g_free(c);
}

static int choice_cmp(gconstpointer a, gconstpointer b) {
    const GtvKbdChoice *x = *(GtvKbdChoice *const *)a;
    const GtvKbdChoice *y = *(GtvKbdChoice *const *)b;
    return g_utf8_collate(x->name, y->name);
}

void gtv_kbd_choices_free(GPtrArray *choices) {
    if (choices) g_ptr_array_free(choices, TRUE);
}

/* Classic layouts whose KLID belongs to `langid`. */
static void add_classic_layouts(GPtrArray *out, LANGID langid, GHashTable *seen) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, KBD_LAYOUTS_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    for (DWORD i = 0;; i++) {
        WCHAR name[64];
        DWORD cch = 64;
        LONG rc = RegEnumKeyExW(key, i, name, &cch, NULL, NULL, NULL, NULL);
        if (rc != ERROR_SUCCESS) break;
        if (cch != 8) continue;                 /* "Substitutes" etc. */
        if ((LANGID)wcstoul(name + 4, NULL, 16) != langid) continue;
        gchar *klid = g_utf16_to_utf8((const gunichar2 *)name, -1, NULL, NULL, NULL);
        gchar *tip = g_strdup_printf("%04x:%s", (unsigned)langid, klid);
        if (g_hash_table_contains(seen, tip)) {
            g_free(tip);
        } else {
            gchar *text = layout_text_name(klid);
            GtvKbdChoice *c = g_new0(GtvKbdChoice, 1);
            c->tip = tip;  /* ownership moves to the array */
            c->name = text ? text : g_strdup(klid);
            g_ptr_array_add(out, c);
            g_hash_table_add(seen, g_strdup(tip));
        }
        g_free(klid);
    }
    RegCloseKey(key);
}

/* TSF profiles registered for `langid` (GoTV, Vietnamese Telex, ...). */
static void add_tsf_profiles(GPtrArray *out, LANGID langid, GHashTable *seen) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    gboolean uninit = SUCCEEDED(hr);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) return;

    ITfInputProcessorProfiles *profiles = NULL;
    hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfInputProcessorProfiles, (void **)&profiles);
    if (SUCCEEDED(hr) && profiles) {
        IEnumTfLanguageProfiles *enum_profiles = NULL;
        if (SUCCEEDED(profiles->lpVtbl->EnumLanguageProfiles(profiles, langid, &enum_profiles)) &&
            enum_profiles) {
            TF_LANGUAGEPROFILE pr;
            while (enum_profiles->lpVtbl->Next(enum_profiles, 1, &pr, NULL) == S_OK) {
                if (pr.langid != langid) continue;
                WCHAR wcls[64], wprof[64];
                StringFromGUID2(&pr.clsid, wcls, 64);
                StringFromGUID2(&pr.guidProfile, wprof, 64);
                gchar *tip;
                if (IsEqualGUID(&pr.clsid, &GUID_NULL)) {
                    /* Legacy profile: guidProfile carries the KLID. */
                    tip = g_strdup_printf("%04x:%08lx", (unsigned)pr.langid,
                                          (unsigned long)pr.guidProfile.Data1);
                } else {
                    gchar *cs = g_utf16_to_utf8((const gunichar2 *)wcls, -1, NULL, NULL, NULL);
                    gchar *pf = g_utf16_to_utf8((const gunichar2 *)wprof, -1, NULL, NULL, NULL);
                    tip = g_strdup_printf("%04x:%s%s", (unsigned)pr.langid,
                                          cs ? cs : "", pf ? pf : "");
                    g_free(cs);
                    g_free(pf);
                }
                /* GoTV is a Vietnamese input method; never offer it under English */
                if ((pr.langid == 0x0409 || PRIMARYLANGID(pr.langid) == LANG_ENGLISH) &&
                    strstr(tip, "E3B0C442") != NULL) {
                    g_free(tip);
                    continue;
                }
                if (g_hash_table_contains(seen, tip)) { g_free(tip); continue; }

                gchar *name = NULL;
                BSTR desc = NULL;
                if (SUCCEEDED(profiles->lpVtbl->GetLanguageProfileDescription(
                        profiles, &pr.clsid, pr.langid, &pr.guidProfile, &desc)) && desc) {
                    name = g_utf16_to_utf8((const gunichar2 *)desc, -1, NULL, NULL, NULL);
                    SysFreeString(desc);
                }
                if (!name || !*name) {
                    g_free(name);
                    name = gtv_kbd_tip_name(tip);
                }
                GtvKbdChoice *c = g_new0(GtvKbdChoice, 1);
                c->tip = tip;
                c->name = name ? name : g_strdup("?");
                g_ptr_array_add(out, c);
                g_hash_table_add(seen, g_strdup(tip));
            }
            enum_profiles->lpVtbl->Release(enum_profiles);
        }
        profiles->lpVtbl->Release(profiles);
    }
    if (uninit) CoUninitialize();
}

GPtrArray *gtv_kbd_choices_for_langid(LANGID langid) {
    GPtrArray *out = g_ptr_array_new_with_free_func(choice_free_cb);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    add_classic_layouts(out, langid, seen);
    add_tsf_profiles(out, langid, seen);
    g_hash_table_destroy(seen);
    g_ptr_array_sort(out, choice_cmp);
    return out;
}

/* ------------------------------------------------------------- powershell */

/* Run one powershell script in a hidden child and capture its stdout.
 * Powershell is the only supported way to read/write the user's keyboard
 * list; registry writes to the input stack are ignored. Output is decoded
 * as UTF-8 (the scripts set [Console]::OutputEncoding accordingly). */
static gchar *ps_run(const gchar *script, gint *exit_code, GError **error) {
    WCHAR ps_path[MAX_PATH];
    WCHAR sysdir[MAX_PATH];
    if (GetSystemDirectoryW(sysdir, MAX_PATH))
        swprintf(ps_path, MAX_PATH, L"%ls\\WindowsPowerShell\\v1.0\\powershell.exe", sysdir);
    else
        wcscpy(ps_path, L"powershell.exe");
    if (GetFileAttributesW(ps_path) == INVALID_FILE_ATTRIBUTES)
        wcscpy(ps_path, L"powershell.exe");

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "CreatePipe failed");
        return NULL;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    gunichar2 *wscript = g_utf8_to_utf16(script, -1, NULL, NULL, NULL);
    if (!wscript) {
        CloseHandle(rd); CloseHandle(wr);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "script encoding failed");
        return NULL;
    }
    size_t cmdlen = wcslen(ps_path) + wcslen((WCHAR *)wscript) + 128;
    WCHAR *cmd = g_new0(WCHAR, cmdlen);
    swprintf(cmd, cmdlen, L"\"%ls\" -NoProfile -NonInteractive -Command \"%ls\"",
             ps_path, (WCHAR *)wscript);
    g_free(wscript);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    /* STARTF_USESTDHANDLES wants a real handle for all three; a NULL stdin
     * would be inherited as an invalid handle. */
    si.hStdInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, 0, NULL);
    ZeroMemory(&pi, sizeof(pi));

    BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                  NULL, NULL, &si, &pi);
    g_free(cmd);
    CloseHandle(wr);
    if (si.hStdInput != INVALID_HANDLE_VALUE) CloseHandle(si.hStdInput);
    if (!started) {
        CloseHandle(rd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "cannot start powershell (%lu)", (unsigned long)GetLastError());
        return NULL;
    }
    CloseHandle(pi.hThread);

    GByteArray *buf = g_byte_array_new();
    char chunk[4096];
    DWORD got = 0;
    /* Read until the child closes stdout; the 60s cap keeps a wedged
     * powershell from freezing the (modal) dialog forever. */
    DWORD deadline = GetTickCount() + 60000;
    for (;;) {
        if (!ReadFile(rd, chunk, sizeof(chunk), &got, NULL) || got == 0) break;
        g_byte_array_append(buf, (const guint8 *)chunk, got);
        if (GetTickCount() > deadline) break;
    }
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (exit_code) *exit_code = (gint)code;

    if (!buf->len) {
        g_byte_array_free(buf, TRUE);
        return g_strdup("");
    }
    g_byte_array_append(buf, (const guint8 *)"\0", 1);
    gchar *out = g_strdup((const gchar *)buf->data);
    g_byte_array_free(buf, TRUE);
    return out;
}

/* The read script prints one "lang<TAB>tip" line per keyboard, in the exact
 * order the switcher shows them.
 *
 * Uses a foreach statement + Write-Output, NOT a pipeline with
 * [Console]::Out.WriteLine: the latter emits the raw WinUserLanguage objects
 * ("System.Object[]") instead of the script block's output. */
static const char *PS_READ =
    "$ErrorActionPreference='Stop';"
    "[Console]::OutputEncoding=[Text.Encoding]::UTF8;"
    "foreach($l in (Get-WinUserLanguageList)){"
    "  $lang=$l.LanguageTag;"
    "  $tips=@($l.InputMethodTips);"
    "  if($tips.Count -eq 0){Write-Output ($lang + [char]9)}"
    "  else{foreach($t in $tips){Write-Output ($lang + [char]9 + $t)}}"
    "}";

gboolean gtv_kbd_list_load(GtvKbdList *list, GError **error) {
    GError *err = NULL;
    gint code = 1;
    gchar *out = ps_run(PS_READ, &code, &err);
    kbd_log("read: exit=%d err=%s", code, err ? err->message : "-");
    if (!out) {
        g_propagate_error(error, err);
        return FALSE;
    }
    if (code != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Không đọc được danh sách bàn phím (powershell exit %d).", code);
        g_free(out);
        return FALSE;
    }
    gtv_kbd_list_clear(list);
    gchar **lines = g_strsplit(out, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        g_strstrip(lines[i]);
        if (!*lines[i]) continue;
        gchar *tab = strchr(lines[i], '\t');
        if (!tab) continue;
        *tab = '\0';
        gtv_kbd_list_add(list, lines[i], tab + 1);
    }
    g_strfreev(lines);
    g_free(out);
    kbd_log("read: got %u keyboards", list->items->len);
    return TRUE;
}

static gboolean list_has_only_vi_tip(const GtvKbdList *list) {
    if (!list) return FALSE;
    gboolean has_vi = FALSE;
    for (guint i = 0; i < list->items->len; i++) {
        const GtvKbd *k = &g_array_index(list->items, GtvKbd, i);
        if (!k->tip || !*k->tip) continue;
        gchar *lang = NULL, *rest = NULL;
        if (!tip_split(k->tip, &lang, &rest)) continue;
        LANGID lid = (LANGID)strtoul(lang, NULL, 16);
        gboolean is_tip = rest_is_guid_pair(rest);
        g_free(lang);
        g_free(rest);
        if (lid == GTV_LANG_VI) {
            if (!is_tip) return FALSE;
            has_vi = TRUE;
        }
    }
    return has_vi;
}

const char *gtv_kbd_ps_pre_vi_reset(void) {
    /* First remove vi from WinUserLanguageList so Windows itself clears its
     * vi User Profile / Substitutes mapping; updating vi in-place while
     * 0000042a is still in Preload causes Windows to allocate d001042a and
     * load classic HKL 0x042a042a alongside GoTV. Then purge any remaining
     * 042a registry remnants and unload live 0x042a HKLs on the input desktop.
     * Note: avoid literal double quotes inside this string because ps_run and
     * setup_worker pass the script inside -Command "...". */
    return
        "$cur=Get-WinUserLanguageList;"
        "$hadVi=$false;"
        "for($ci=$cur.Count-1;$ci -ge 0;$ci--){"
        "  if($cur[$ci].LanguageTag -match '^vi'){$cur.RemoveAt($ci);$hadVi=$true}}"
        "if($cur.Count -eq 0){"
        "  $tmp=New-WinUserLanguageList 'en-US';"
        "  $tmp[0].InputMethodTips.Clear();"
        "  [void]$tmp[0].InputMethodTips.Add('0409:00000409');"
        "  Set-WinUserLanguageList $tmp -Force;"
        "}elseif($hadVi){"
        "  Set-WinUserLanguageList $cur -Force;"
        "}"
        "Start-Sleep -Milliseconds 150;"
        "foreach($k in @('HKCU:\\Keyboard Layout\\Preload','HKCU:\\Keyboard Layout\\Substitutes','HKCU:\\Software\\Microsoft\\CTF\\HiddenDummyLayouts')){"
        "  if(Test-Path $k){"
        "    $p=Get-ItemProperty $k;"
        "    foreach($n in $p.PSObject.Properties.Name){"
        "      if($n -match '042a$' -or ([string]$p.$n) -match '042a$'){Remove-ItemProperty -Path $k -Name $n -Force -ErrorAction SilentlyContinue}}}}"
        "foreach($rk in @('HKCU:\\Software\\Microsoft\\CTF\\SortOrder\\AssemblyItem\\0x0000042a','HKCU:\\Software\\Microsoft\\CTF\\Assemblies\\0x0000042a')){"
        "  if(Test-Path $rk){Remove-Item -Path $rk -Recurse -Force -ErrorAction SilentlyContinue}}"
        "$dq=[char]34;"
        "Add-Type -Name Kbd -Namespace Gtv -MemberDefinition ("
        "  '[DllImport('+$dq+'user32.dll'+$dq+')]public static extern IntPtr OpenInputDesktop(uint f,bool i,uint a);'"
        "  +'[DllImport('+$dq+'user32.dll'+$dq+')]public static extern bool SetThreadDesktop(IntPtr h);'"
        "  +'[DllImport('+$dq+'user32.dll'+$dq+')]public static extern bool CloseDesktop(IntPtr h);'"
        "  +'[DllImport('+$dq+'user32.dll'+$dq+')]public static extern IntPtr ActivateKeyboardLayout(IntPtr hkl,uint f);'"
        "  +'[DllImport('+$dq+'user32.dll'+$dq+')]public static extern bool UnloadKeyboardLayout(IntPtr hkl);'"
        "  +'[DllImport('+$dq+'user32.dll'+$dq+')]public static extern int GetKeyboardLayoutList(int n,[Out] IntPtr[] l);'"
        ") -ErrorAction SilentlyContinue;"
        "if(('Gtv.Kbd' -as [type]) -ne $null){"
        "  $hd=[Gtv.Kbd]::OpenInputDesktop(0,$false,0x01FF);"
        "  if($hd -ne [IntPtr]::Zero){[void][Gtv.Kbd]::SetThreadDesktop($hd)}"
        "  [void][Gtv.Kbd]::ActivateKeyboardLayout([IntPtr]0x04090409,0);"
        "  $hl=New-Object IntPtr[] 32;"
        "  $hn=[Gtv.Kbd]::GetKeyboardLayoutList(32,$hl);"
        "  for($hi=0;$hi -lt $hn;$hi++){"
        "    if(($hl[$hi].ToInt64() -band 0xFFFF) -eq 0x042a){[void][Gtv.Kbd]::UnloadKeyboardLayout($hl[$hi])}}"
        "  [void][Gtv.Kbd]::UnloadKeyboardLayout([IntPtr]0x0409042a);"
        "  [void][Gtv.Kbd]::UnloadKeyboardLayout([IntPtr]0x042a042a);"
        "  if($hd -ne [IntPtr]::Zero){[void][Gtv.Kbd]::CloseDesktop($hd)}}";
}

const char *gtv_kbd_ps_post_vi_cleanup(void) {
    return
        "Start-Sleep -Milliseconds 150;"
        "if(('Gtv.Kbd' -as [type]) -ne $null){"
        "  $hd=[Gtv.Kbd]::OpenInputDesktop(0,$false,0x01FF);"
        "  if($hd -ne [IntPtr]::Zero){[void][Gtv.Kbd]::SetThreadDesktop($hd)}"
        "  $hl=New-Object IntPtr[] 32;"
        "  $hn=[Gtv.Kbd]::GetKeyboardLayoutList(32,$hl);"
        "  for($hi=0;$hi -lt $hn;$hi++){"
        "    $hv=$hl[$hi].ToInt64();"
        "    if((($hv -band 0xFFFF) -eq 0x042a) -and (($hv -band 0xFFFF0000L) -eq 0x042a0000L)){[void][Gtv.Kbd]::UnloadKeyboardLayout($hl[$hi])}}"
        "  [void][Gtv.Kbd]::UnloadKeyboardLayout([IntPtr]0x042a042a);"
        "  if($hd -ne [IntPtr]::Zero){[void][Gtv.Kbd]::CloseDesktop($hd)}}";
}

/* The write script rebuilds the list from scratch. InputMethodTips cannot be
 * assigned on a Get-WinUserLanguageList result (it is read-only), so each
 * language is created with New-WinUserLanguageList and its tips are filled
 * in - the same approach input_setup.c relies on.
 *
 * Entries travel inside the script as "langid|tag|tip" and are grouped by
 * LANGID, not by tag text: Windows is happy with both "vi" and "vi-VN", so
 * grouping by tag could rebuild one language as two separate entries. */
static gchar *ps_build_write(const GtvKbdList *list, gboolean vi_tip_only) {
    GString *s = g_string_new(
        "$ErrorActionPreference='Stop';"
        "[Console]::OutputEncoding=[Text.Encoding]::UTF8;"
        "$want=@();");
    kbd_log("write script begin");
    for (guint i = 0; i < list->items->len; i++) {
        const GtvKbd *k = &g_array_index(list->items, GtvKbd, i);
        /* Skip empty tips: a language with no keyboard is what makes
         * Set-WinUserLanguageList fail (and what leaves Win+Space empty). */
        if (!k->tip || !*k->tip) continue;
        LANGID lid = tip_langid(k->tip);
        if (!lid) continue;
        gchar *entry = g_strdup_printf("%04x|%s|%s", (unsigned)lid,
                                       k->lang ? k->lang : "", k->tip);
        /* Single-quoted PowerShell literal: the ONLY escape is '' for a
         * quote, and inside single quotes nothing is expanded or
         * interpolated. Tips carry { } : and the entry adds |, but never a
         * quote. */
        GString *q = g_string_new(NULL);
        for (const gchar *p = entry; *p; p++) {
            if (*p == 0x27) g_string_append(q, "''");
            else g_string_append_c(q, *p);
        }
        g_string_append(s, "$want+='");
        g_string_append(s, q->str);
        g_string_append(s, "';");
        kbd_log("  want: %s", q->str);
        g_string_free(q, TRUE);
        g_free(entry);
    }
    g_string_append(s,
        "$acc=New-Object System.Collections.ArrayList;"
        "$ids=New-Object System.Collections.ArrayList;"
        "foreach($w in $want){"
        "  $p=$w -split '\\|',3;"
        "  if($p.Count -lt 3){continue}"
        "  $lid=$p[0];$tag=$p[1];$tip=$p[2];"
        "  $lang=$null;"
        "  for($i=0;$i -lt $acc.Count;$i++){"
        "    if($ids[$i] -eq $lid){$lang=$acc[$i];break}}"
        "  if($lang -eq $null){"
        "    $tmp=New-WinUserLanguageList $tag;"
        "    $lang=$tmp[0];"
        "    $lang.InputMethodTips.Clear();"
        "    [void]$acc.Add($lang);[void]$ids.Add($lid)}"
        "  if($lang.InputMethodTips -notcontains $tip){$lang.InputMethodTips.Add($tip)}}"
        /* Windows needs at least one language with a keyboard. */
        "if($acc.Count -eq 0){Write-Output 'EMPTY';exit 2}");
    if (vi_tip_only) {
        g_string_append(s, gtv_kbd_ps_pre_vi_reset());
        g_string_append_c(s, ';');
    }
    g_string_append(s, "Set-WinUserLanguageList $acc -Force;");
    if (vi_tip_only) {
        g_string_append(s, gtv_kbd_ps_post_vi_cleanup());
        g_string_append_c(s, ';');
    }
    g_string_append(s,
        /* Read back and echo what Windows actually kept, so the caller can
         * report a rejected keyboard instead of silently losing it. */
        "$res=New-Object System.Collections.ArrayList;"
        "foreach($l in (Get-WinUserLanguageList)){"
        "  foreach($t in @($l.InputMethodTips)){[void]$res.Add($l.LanguageTag + [char]9 + $t)}}"
        "if($res.Count -eq 0){Write-Output 'EMPTY';exit 2}"
        "foreach($r in $res){Write-Output $r}");
    return g_string_free(s, FALSE);
}

gboolean gtv_kbd_list_apply(const GtvKbdList *list, GError **error) {
    gboolean vi_tip_only = list_has_only_vi_tip(list);
    if (vi_tip_only) gtv_kbd_prepare_vi_tip_only();
    gchar *script = ps_build_write(list, vi_tip_only);
    GError *err = NULL;
    gint code = 1;
    gchar *out = ps_run(script, &code, &err);
    g_free(script);
    if (vi_tip_only) gtv_kbd_cleanup_vi_tip_only();
    kbd_log("apply: exit=%d err=%s", code, err ? err->message : "-");
    kbd_log("apply: readback=%s", out ? out : "(none)");
    if (!out) {
        g_propagate_error(error, err);
        return FALSE;
    }
    if (code == 2) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Windows từ chối danh sách mới (không còn bàn phím nào).");
        g_free(out);
        return FALSE;
    }
    if (code != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Không áp dụng được danh sách bàn phím (powershell exit %d).", code);
        g_free(out);
        return FALSE;
    }
    /* Compare with what we asked for: Windows silently drops keyboards it
     * does not accept (that is how an unregistered TIP disappears) and can
     * re-add a language's default layout. Both are reported, neither is
     * silent - the whole point of writing the list back this way. */
    guint asked = 0;
    for (guint i = 0; i < list->items->len; i++) {
        const GtvKbd *k = &g_array_index(list->items, GtvKbd, i);
        if (k->tip && *k->tip) asked++;
    }
    gchar **lines = g_strsplit(out, "\n", -1);
    guint got = 0;
    /* Only "lang<TAB>tip" lines are data. Set-WinUserLanguageList also
     * prints its own WARNING lines, which must not be counted. */
    for (guint i = 0; lines[i]; i++) {
        g_strstrip(lines[i]);
        if (strchr(lines[i], '\t')) got++;
    }
    for (guint i = 0; i < list->items->len; i++) {
        const GtvKbd *k = &g_array_index(list->items, GtvKbd, i);
        if (!k->tip || !*k->tip) continue;
        gboolean found = FALSE;
        for (guint j = 0; lines[j] && !found; j++) {
            gchar *tab = strchr(lines[j], '\t');
            if (!tab) continue;
            if (!g_ascii_strcasecmp(tab + 1, k->tip)) found = TRUE;
        }
        if (!found) {
            gchar *name = gtv_kbd_tip_name(k->tip);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Windows đã bỏ bàn phím \"%s\" — không được Windows chấp nhận.",
                        name ? name : k->tip);
            g_free(name);
            g_strfreev(lines);
            g_free(out);
            return FALSE;
        }
    }
    g_strfreev(lines);
    g_free(out);
    if (asked && got != asked) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Windows tự thêm/bớt bàn phím (bạn chọn %u, còn %u).", asked, got);
        return FALSE;
    }
    kbd_log("apply: ok (%u keyboards)", got);
    return TRUE;
}

GArray *gtv_kbd_current_langids(void) {
    GArray *ids = g_array_new(FALSE, FALSE, sizeof(LANGID));
    GError *err = NULL;
    GtvKbdList *list = gtv_kbd_list_new();
    if (!gtv_kbd_list_load(list, &err)) {
        g_clear_error(&err);
        gtv_kbd_list_free(list);
        return ids;
    }
    gtv_kbd_langids_of(list, ids);
    gtv_kbd_list_free(list);
    return ids;
}

/* The langid encoded in a TIP string ("042a:{...}{...}" -> 0x042a). */
static LANGID tip_langid(const gchar *tip) {
    if (!tip || strlen(tip) < 5 || tip[4] != ':') return 0;
    return (LANGID)strtoul(tip, NULL, 16);
}

void gtv_kbd_langids_of(const GtvKbdList *list, GArray *out) {
    for (guint i = 0; i < list->items->len; i++) {
        const GtvKbd *k = &g_array_index(list->items, GtvKbd, i);
        LANGID id = tip_langid(k->tip);
        if (!id) continue;
        gboolean seen = FALSE;
        for (guint j = 0; j < out->len && !seen; j++)
            seen = g_array_index(out, LANGID, j) == id;
        if (!seen) g_array_append_val(out, id);
    }
}

/* The tag Windows' own language list uses for a langid ("042a" -> "vi-VN").
 * Needed because New-WinUserLanguageList takes a BCP-47 tag, not a langid.
 * EnumSystemLocalesEx + LOCALE_SNAME is the mapping Windows uses. */
typedef struct {
    LANGID  want;
    GPtrArray *out;
} TagSearch;

static BOOL CALLBACK collect_tag(LPWSTR locale, DWORD flags, LPARAM param) {
    TagSearch *ts = (TagSearch *)param;
    LCID lcid = LocaleNameToLCID(locale, 0);
    if ((LANGID)LOWORD(lcid) != ts->want) return TRUE;
    WCHAR sname[64] = {0};
    if (GetLocaleInfoW(lcid, LOCALE_SNAME, sname, 64) > 0 && sname[0]) {
        gchar *tag = g_utf16_to_utf8((const gunichar2 *)sname, -1, NULL, NULL, NULL);
        if (tag) { g_ptr_array_add(ts->out, tag); return FALSE; }  /* stop */
    }
    return TRUE;
}

gchar *gtv_kbd_tag_for_langid(LANGID langid) {
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    TagSearch ts = { langid, out };
    EnumSystemLocalesEx(collect_tag, LOCALE_ALL, (LPARAM)&ts, NULL);
    gchar *tag = (out->len > 0) ? g_strdup(g_ptr_array_index(out, 0)) : NULL;
    g_ptr_array_free(out, TRUE);
    return tag;
}

/* The tag a list already uses for `langid`, so adding a keyboard to a
 * language keeps its existing spelling ("vi" must not become "vi-VN" - the
 * two would rebuild as two separate languages). */
gchar *gtv_kbd_existing_tag(const GtvKbdList *list, LANGID langid) {
    for (guint i = 0; i < list->items->len; i++) {
        const GtvKbd *k = &g_array_index(list->items, GtvKbd, i);
        if (tip_langid(k->tip) == langid && k->lang && *k->lang)
            return g_strdup(k->lang);
    }
    return NULL;
}

void gtv_kbd_list_set_default(GtvKbdList *list) {
    gtv_kbd_list_clear(list);
    gtv_kbd_list_add(list, "en-US", "0409:00000409");
    gtv_kbd_list_add(list, "vi", GTV_TIP_VI);
}

/* ------------------------------------------------------------ config flag */

static gchar *kbd_state_path(const gchar *name) {
    return gtv_user_path(g_get_user_config_dir(), name);
}

gboolean gtv_kbd_is_custom(void) {
    gchar *path = kbd_state_path("keyboard-custom");
    gboolean exists = g_file_test(path, G_FILE_TEST_EXISTS);
    g_free(path);
    return exists;
}

void gtv_kbd_mark_custom(void) {
    gchar *path = kbd_state_path("keyboard-custom");
    g_file_set_contents(path, "1\n", -1, NULL);
    g_free(path);
}

#ifndef TF_IPPMF_FORSESSION
#define TF_IPPMF_FORSESSION 0x20000000
#endif
#ifndef TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE
#define TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE 0x00000004
#endif
#ifndef TF_IPP_FLAG_ENABLED
#define TF_IPP_FLAG_ENABLED 0x00000002
#endif

BOOL gtv_kbd_is_vietnamese_active(void) {
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL is_vi = FALSE;
    BOOL got_profile = FALSE;

    ITfInputProcessorProfileMgr *mgr = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfInputProcessorProfileMgr, (void **)&mgr)) && mgr) {
        TF_INPUTPROCESSORPROFILE active;
        memset(&active, 0, sizeof(active));
        if (SUCCEEDED(mgr->lpVtbl->GetActiveProfile(mgr, &GUID_TFCAT_TIP_KEYBOARD, &active))) {
            CLSID gtv_clsid = GUID_NULL;
            CLSIDFromString(L"{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}", &gtv_clsid);
            is_vi = (active.langid == GTV_LANG_VI && IsEqualGUID(&active.clsid, &gtv_clsid));
            got_profile = TRUE;
        }
        mgr->lpVtbl->Release(mgr);
    }
    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
    if (got_profile) {
        return is_vi;
    }

    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    HKL hkl = GetKeyboardLayout(tid);
    if (!hkl) hkl = GetKeyboardLayout(0);
    return (LOWORD(hkl) == GTV_LANG_VI);
}

#define KBD_HKL_US         ((HKL)(ULONG_PTR)0x04090409)
#define KBD_HKL_VI_SUB     ((HKL)(ULONG_PTR)0x0409042a)
#define KBD_HKL_VI_CLASSIC ((HKL)(ULONG_PTR)0x042a042a)

static void unload_vi_hkls(gboolean unload_substitute) {
    HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (hDesk) SetThreadDesktop(hDesk);

    HKL hkls[32];
    int n = GetKeyboardLayoutList((int)G_N_ELEMENTS(hkls), hkls);
    for (int i = 0; i < n; i++) {
        LANGID lid = LOWORD((ULONG_PTR)hkls[i]);
        WORD dev = HIWORD((ULONG_PTR)hkls[i]);
        if (lid == GTV_LANG_VI && (unload_substitute || dev == GTV_LANG_VI)) {
            UnloadKeyboardLayout(hkls[i]);
        }
    }
    if (unload_substitute) UnloadKeyboardLayout(KBD_HKL_VI_SUB);
    UnloadKeyboardLayout(KBD_HKL_VI_CLASSIC);

    if (hDesk) CloseDesktop(hDesk);
}

void gtv_kbd_prepare_vi_tip_only(void) {
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ITfInputProcessorProfileMgr *mgr = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfInputProcessorProfileMgr, (void **)&mgr)) && mgr) {
        mgr->lpVtbl->ActivateProfile(mgr, TF_PROFILETYPE_KEYBOARDLAYOUT, 0x0409,
                                     &GUID_NULL, &GUID_NULL, KBD_HKL_US,
                                     TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
        mgr->lpVtbl->Release(mgr);
    }
    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }

    ActivateKeyboardLayout(KBD_HKL_US, 0);
    HWND fg = GetForegroundWindow();
    if (fg) {
        SendMessageTimeoutW(fg, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)KBD_HKL_US,
                            SMTO_ABORTIFHUNG, 500, NULL);
    }
    unload_vi_hkls(TRUE);
}

void gtv_kbd_cleanup_vi_tip_only(void) {
    unload_vi_hkls(FALSE);
}

void gtv_kbd_toggle_active(void) {
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    HKL hkl = GetKeyboardLayout(tid);
    if (!hkl) hkl = GetKeyboardLayout(0);
    LANGID current_lang = LOWORD(hkl);

    CLSID gtv_clsid = GUID_NULL;
    CLSIDFromString(L"{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}", &gtv_clsid);

    ITfInputProcessorProfileMgr *mgr = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                 &IID_ITfInputProcessorProfileMgr, (void **)&mgr);
    if (SUCCEEDED(hr) && mgr) {
        TF_INPUTPROCESSORPROFILE active;
        memset(&active, 0, sizeof(active));
        if (SUCCEEDED(mgr->lpVtbl->GetActiveProfile(mgr, &GUID_TFCAT_TIP_KEYBOARD, &active))) {
            if (active.langid == GTV_LANG_VI && IsEqualGUID(&active.clsid, &gtv_clsid)) {
                current_lang = GTV_LANG_VI;
            } else {
                current_lang = 0;
            }
        }
        IEnumTfInputProcessorProfiles *pEnum = NULL;
        if (SUCCEEDED(mgr->lpVtbl->EnumProfiles(mgr, 0, &pEnum)) && pEnum) {
            TF_INPUTPROCESSORPROFILE list[32];
            ULONG count = 0;
            while (count < G_N_ELEMENTS(list) && pEnum->lpVtbl->Next(pEnum, 1, &list[count], NULL) == S_OK) {
                count++;
            }
            pEnum->lpVtbl->Release(pEnum);

            if (count >= 2) {
                ULONG targetIdx = 0;
                gboolean found = FALSE;
                if (current_lang == GTV_LANG_VI) {
                    for (ULONG i = 0; i < count; i++) {
                        if (list[i].langid != GTV_LANG_VI && list[i].langid != 0 &&
                            (list[i].dwFlags & TF_IPP_FLAG_ENABLED)) {
                            targetIdx = i;
                            found = TRUE;
                            break;
                        }
                    }
                    if (!found) {
                        for (ULONG i = 0; i < count; i++) {
                            if (list[i].langid != GTV_LANG_VI && list[i].langid != 0) {
                                targetIdx = i;
                                break;
                            }
                        }
                    }
                } else {
                    /* Prefer the GoTV TIP profile over Windows' substitute US keyboard HKL (type=2). */
                    for (ULONG i = 0; i < count; i++) {
                        if (list[i].langid == GTV_LANG_VI &&
                            IsEqualGUID(&list[i].clsid, &gtv_clsid)) {
                            targetIdx = i;
                            found = TRUE;
                            break;
                        }
                    }
                    if (!found) {
                        for (ULONG i = 0; i < count; i++) {
                            if (list[i].langid == GTV_LANG_VI) {
                                targetIdx = i;
                                break;
                            }
                        }
                    }
                }

                const TF_INPUTPROCESSORPROFILE *next = &list[targetIdx];
                mgr->lpVtbl->ActivateProfile(mgr, next->dwProfileType, next->langid,
                                            &next->clsid, &next->guidProfile, next->hkl,
                                            TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);

                if (fg && next->hkl) {
                    PostMessage(fg, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)next->hkl);
                }
            }
        }
        mgr->lpVtbl->Release(mgr);
    } else {
        if (fg) {
            PostMessage(fg, WM_INPUTLANGCHANGEREQUEST, INPUTLANGCHANGE_FORWARD, 0);
        }
    }

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
}
