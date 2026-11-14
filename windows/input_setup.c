/* Automatic per-user input setup: GoTV is removed from English (the plain
 * US keyboard is restored) and Vietnamese gets GoTV as its ONLY keyboard
 * (badge "VIE"), so Win+Space switches ENG (US) <-> VIE (GoTV).
 *
 * Any other TIP Windows puts under Vietnamese (e.g. the built-in Telex)
 * is removed, otherwise the switcher shows duplicate VIE entries.
 * Idempotent: a setup-revision stamp avoids respawning powershell on every
 * launch. The heavy lifting goes through Set-WinUserLanguageList — the
 * OS-validated path — in a hidden powershell child, because raw registry
 * writes are ignored/dropped by the input stack. */
#include "input_setup.h"
#include "engine.h"
#include "keyboard.h"
#include "version.h"

#include <windows.h>
#include <glib.h>
#include <glib/gstdio.h>

/* TIP strings, same format Windows itself uses, e.g.
 * 042a:{CLSID}{ProfileGUID}. GoTV lives under Vietnamese so the taskbar
 * badge shows "VIE" — distinguishing it from the plain US keyboard ("ENG"). */
#define GTV_TIP_VI "042a:{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}{D4C5B6A7-1E2F-4A3B-8C9D-0E1F2A3B4C5D}"
#define GTV_TIP_EN "0409:{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}{D4C5B6A7-1E2F-4A3B-8C9D-0E1F2A3B4C5D}"
#define GTV_TIP_US "0409:00000409"
#define GTV_INPUT_SETUP_REVISION "10"

static void setup_log(const gchar *fmt, ...) {
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    g_mkdir_with_parents(dir, 0755);
    gchar *path = g_build_filename(dir, "input-setup.log", NULL);
    g_free(dir);
    FILE *f = g_fopen(path, "a");
    g_free(path);
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

static gchar *stamp_path(void) {
    return gtv_user_path(g_get_user_config_dir(), "input-setup.done");
}

static gboolean stamp_current(void) {
    gchar *path = stamp_path();
    gchar *contents = NULL;
    gboolean ok = FALSE;
    if (g_file_get_contents(path, &contents, NULL, NULL) && contents)
        ok = !strcmp(g_strstrip(contents), GTV_INPUT_SETUP_REVISION);
    g_free(contents);
    g_free(path);
    return ok;
}

static void stamp_write(void) {
    gchar *path = stamp_path();
    g_file_set_contents(path, GTV_INPUT_SETUP_REVISION, -1, NULL);
    g_free(path);
}

void gtv_input_setup_mark_done(void) {
    stamp_write();
}

/* One powershell invocation: English keeps the plain US keyboard only
 * (any GoTV TIP left by 0.8.19 is dropped) and Vietnamese gets GoTV as
 * its ONLY keyboard (badge "VIE").
 * Result: exactly 2 switcher entries — ENG US keyboard + VIE GoTV.
 *
 * Note: an existing vi entry whose InputMethodTips is EMPTY cannot be fixed
 * in place — InputMethodTips is a read-only property, so .Add() on the
 * returned collection is a no-op (Remove() appears to work because the list
 * is re-read). Such an entry is what a failed install leaves behind, and it
 * is why vi-VN showed up with no keyboard at all. Any vi entry that is not
 * already exactly [GoTV] is therefore dropped and rebuilt from scratch. */
static gchar *setup_build_script(void) {
    return g_strdup_printf(
        "$ErrorActionPreference='SilentlyContinue';"
        "$l=Get-WinUserLanguageList;"
        "$c=$false;"
        /* English: drop any GoTV TIP, keep the plain US keyboard */
        "foreach($x in $l){"
        "  if($x.LanguageTag -match '^en'){"
        "    for($j=$x.InputMethodTips.Count-1;$j -ge 0;$j--){"
        "      if($x.InputMethodTips[$j] -match 'E3B0C442'){[void]$x.InputMethodTips.RemoveAt($j);$c=$true}}"
        "    if($x.InputMethodTips.Count -eq 0){$x.InputMethodTips.Add('" GTV_TIP_US "');$c=$true}}}"
        /* Drop every vi entry: rebuilt below with GoTV as the only TIP */
        "for($i=$l.Count-1;$i-ge 0;$i--){"
        "  if($l[$i].LanguageTag -match '^vi'){$l.RemoveAt($i);$c=$true}}"
        /* Two-step reset so Windows drops stale 042a Preload/Substitutes/HKLs
         * instead of allocating d001042a / 0x042a042a alongside GoTV. */
        "%s;"
        "$vi=New-WinUserLanguageList 'vi-VN';"
        "$vi[0].InputMethodTips.Clear();"
        "$vi[0].InputMethodTips.Add('" GTV_TIP_VI "');"
        "$l.Add($vi[0]);"
        "$c=$true;"
        "if($c){Set-WinUserLanguageList $l -Force};"
        "%s;"
        /* Verify: English (if present) has no GoTV; vi has GoTV sole */
        "$l2=Get-WinUserLanguageList;"
        "$ok=$true;"
        "$vi2=@($l2 | Where-Object{$_.LanguageTag -match '^vi'});"
        "if($vi2.Count -ne 1){$ok=$false}"
        "elseif($vi2[0].InputMethodTips.Count -ne 1){$ok=$false}"
        "elseif($vi2[0].InputMethodTips[0] -ne '" GTV_TIP_VI "'){$ok=$false}"
        "foreach($x in $l2){"
        "  if($x.LanguageTag -match '^en'){"
        "    foreach($t in $x.InputMethodTips){if($t -match 'E3B0C442'){$ok=$false}}}}"
        "if($ok){exit 0}else{exit 1}",
        gtv_kbd_ps_pre_vi_reset(),
        gtv_kbd_ps_post_vi_cleanup());
}

static gpointer setup_worker(gpointer data) {
    (void)data;
    setup_log("input setup start version=%s", GTV_VERSION);

    WCHAR sysdir[MAX_PATH];
    WCHAR ps_path[MAX_PATH];
    if (GetSystemDirectoryW(sysdir, MAX_PATH))
        swprintf(ps_path, MAX_PATH, L"%ls\\WindowsPowerShell\\v1.0\\powershell.exe", sysdir);
    else
        wcscpy(ps_path, L"powershell.exe");
    if (GetFileAttributesW(ps_path) == INVALID_FILE_ATTRIBUTES)
        wcscpy(ps_path, L"powershell.exe");

    gchar *script = setup_build_script();
    gunichar2 *wscript = g_utf8_to_utf16(script, -1, NULL, NULL, NULL);
    g_free(script);
    if (!wscript) {
        setup_log("script conversion failed");
        return NULL;
    }

    size_t cmdlen = wcslen(ps_path) + wcslen((WCHAR *)wscript) + 256;
    WCHAR *cmd = g_new0(WCHAR, cmdlen);
    /* No profile, non-interactive, hidden window. Do NOT use -ExecutionPolicy Bypass
     * as that triggers corporate EDR/Antivirus heuristics (CL.Downloader!gen74).
     * Get/Set-WinUserLanguageList are built-in cmdlets and do not require bypass. */
    swprintf(cmd, cmdlen,
             L"\"%ls\" -NoProfile -NonInteractive -Command \"%ls\"",
             ps_path, (WCHAR *)wscript);
    g_free(wscript);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    gtv_kbd_prepare_vi_tip_only();

    BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                                  NULL, NULL, &si, &pi);
    g_free(cmd);
    if (!started) {
        setup_log("CreateProcess powershell failed: %lu", (unsigned long)GetLastError());
        return NULL;
    }
    CloseHandle(pi.hThread);
    DWORD wait_rc = WaitForSingleObject(pi.hProcess, 90000);
    DWORD exit_code = 1;
    if (wait_rc == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        setup_log("powershell timed out after 90s");
    } else {
        GetExitCodeProcess(pi.hProcess, &exit_code);
    }
    CloseHandle(pi.hProcess);
    setup_log("powershell done exit=%lu", (unsigned long)exit_code);
    if (exit_code == 0) {
        gtv_kbd_cleanup_vi_tip_only();
        stamp_write();
        setup_log("input setup verified, stamp written");
    } else {
        setup_log("input setup incomplete (machine registration may be missing); will retry");
    }
    return NULL;
}

void gtv_input_setup_ensure_async(void) {
    /* The keyboard manager lets the user own their own list; once they have
     * saved one, this automatic pass must never run again or it would undo
     * their choices on the next launch. */
    if (gtv_kbd_is_custom())
        return;
    if (stamp_current())
        return;
    GThread *th = g_thread_new("gtv-input-setup", setup_worker, NULL);
    if (!th)
        return;
    g_thread_unref(th);
}
