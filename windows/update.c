#include "update.h"
#include "app.h"
#include "tray.h"
#include "version.h"
#include "internal.h"
#include <glib/gstdio.h>
#include <shellapi.h>

typedef struct { HWND notify; gboolean manual; } CheckJob;

static volatile LONG update_in_flight = 0;
static volatile DWORD update_flight_tick = 0;

/* Append-only diagnostic log next to the config, so a silent updater can
 * be diagnosed from %APPDATA%/gotv/update.log. Bounded size. */
static void update_log(const gchar *fmt, ...) {
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    g_mkdir_with_parents(dir, 0755);
    gchar *path = g_build_filename(dir, "update.log", NULL);
    g_free(dir);
    FILE *probe = g_fopen(path, "r");
    if (probe) {
        fseek(probe, 0, SEEK_END);
        if (ftell(probe) > 65536) {
            fclose(probe);
            g_remove(path);
            probe = NULL;
        } else fclose(probe);
    }
    FILE *f = g_fopen(path, "a");
    g_free(path);
    if (!f) return;
    GDateTime *now = g_date_time_new_now_local();
    gchar *ts = now ? g_date_time_format(now, "%Y-%m-%d %H:%M:%S") : g_strdup("?");
    fprintf(f, "[%s] ", ts);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
    g_free(ts);
    if (now) g_date_time_unref(now);
}

/* Latest balloon ownership: the AI typo balloon and the update balloon
 * share one tray slot, so clicks are routed to whoever showed last. */
static volatile LONG update_balloon_owned = 0;

/* Pending update behind the balloon: click downloads and installs it. */
static gchar *pending_tag = NULL;
static gchar *pending_url = NULL;

static gpointer check_worker(gpointer data) {
    CheckJob *job = data;
    gchar *tag = NULL, *url = NULL;
    /* Diagnose the usual "check network" cause before hitting GitHub:
     * missing curl, env override, proxy. All goes to update.log. */
    gchar *curl_path = g_find_program_in_path("curl.exe");
    if (!curl_path) curl_path = g_find_program_in_path("curl");
    update_log("check env url=%s repo=%s https_proxy=%s http_proxy=%s curl=%s",
               g_getenv("GTV_UPDATE_URL") ? g_getenv("GTV_UPDATE_URL") : "-",
               g_getenv("GTV_UPDATE_REPO") ? g_getenv("GTV_UPDATE_REPO") : "-",
               g_getenv("HTTPS_PROXY") ? g_getenv("HTTPS_PROXY") : "-",
               g_getenv("HTTP_PROXY") ? g_getenv("HTTP_PROXY") : "-",
               curl_path ? curl_path : "NOT-FOUND");
    g_free(curl_path);
    GtvUpdateStatus st = gtv_update_check(NULL, GTV_VERSION, &tag, &url);
    update_log("check done manual=%d status=%d tag=%s url=%s",
                   job->manual, (gint)st, tag ? tag : "-", url ? url : "-");
    /* Dictionary refresh rides the same (throttled) check: the dict-vi.txt
     * release asset upgrades the offline word list without an app update. */
    if (gtv_dict_update_check(NULL))
        update_log("dictionary refreshed from GitHub (tag %s)", gtv_dict_tag());
    if (st == GTV_UPDATE_ERROR)
        update_log("check error detail: %s", gtv_update_last_error());
    if (st == GTV_UPDATE_ERROR)
        update_log("hint: run curl manually: curl --fail --location https://api.github.com/repos/isthaison/gotv/releases/latest");
    GtvUpdateResult *res = g_new(GtvUpdateResult, 1);
    res->status = (gint)st;
    res->tag = tag;
    res->url = url;
    if (job->notify)
        PostMessage(job->notify, WM_GTV_UPDATE_RESULT, (WPARAM)job->manual, (LPARAM)res);
    else {
        g_free(tag); g_free(url); g_free(res);
    }
    g_free(job);
    InterlockedExchange(&update_in_flight, 0);
    return NULL;
}

void gtv_update_check_async(HWND notify, gboolean manual) {
    if (manual) {
        /* Manual checks always run; auto-checks honour the 24h throttle. */
    } else if (!gtv_update_should_autocheck()) {
        return;
    } else {
        gtv_update_mark_checked();
    }
    if (InterlockedCompareExchange(&update_in_flight, 1, 0) != 0) {
        /* A previous worker should always reset the slot; reclaim it if
         * it has been stuck longer than any check may take, so one wedged
         * run can never silence the updater until restart. */
        if ((DWORD)(GetTickCount() - update_flight_tick) < 120000) return;
        update_log("stale check slot reclaimed");
        InterlockedExchange(&update_in_flight, 0);
        if (InterlockedCompareExchange(&update_in_flight, 1, 0) != 0) return;
    }
    update_flight_tick = GetTickCount();
    update_log("check start manual=%d version=%s", manual, GTV_VERSION);
    CheckJob *job = g_new0(CheckJob, 1);
    job->notify = notify;
    job->manual = manual;
    GThread *th = g_thread_new("gtv-update-check", check_worker, job);
    if (!th) {
        g_free(job);
        InterlockedExchange(&update_in_flight, 0);
        return;
    }
    g_thread_unref(th);
}

void gtv_update_on_result(GtvUpdateResult *res, gboolean manual) {
    if (!res) return;
    if (res->status == GTV_UPDATE_AVAILABLE && res->tag && res->url) {
        g_free(pending_tag); g_free(pending_url);
        pending_tag = g_strdup(res->tag);
        pending_url = g_strdup(res->url);
        InterlockedExchange(&update_balloon_owned, 1);
        update_log("update available: %s", res->tag);
        if (manual) {
            gchar *u8msg = g_strdup_printf(gtv_i18n("Đã có phiên bản mới %s!\n\nBạn có muốn tải về và cài đặt ngay bây giờ không?",
                                                   "A new version %s is available!\n\nDo you want to download and install it now?"), res->tag);
            gunichar2 *wmsg = g_utf8_to_utf16(u8msg, -1, NULL, NULL, NULL);
            gunichar2 *wtitle = g_utf8_to_utf16(gtv_i18n("Cập nhật GoTV", "GoTV Update"), -1, NULL, NULL, NULL);
            int ret = MessageBoxW(g_app.hwnd_main, (LPCWSTR)wmsg, (LPCWSTR)wtitle, MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
            g_free(u8msg); g_free(wmsg); g_free(wtitle);
            if (ret == IDYES) {
                gtv_update_balloon_clicked();
                g_free(res->tag); g_free(res->url); g_free(res);
                return;
            }
        }
        gchar *msg = g_strdup_printf(gtv_i18n("Có bản mới %s. Nhấn vào đây để tải và cài đặt.",
                                             "New version %s is available. Click here to download and install."), res->tag);
        update_log("balloon shown: update available %s", res->tag);
        gtv_tray_balloon_force(gtv_i18n("GoTV cập nhật", "GoTV Update"), msg);
        g_free(msg);
    } else if (manual) {
        InterlockedExchange(&update_balloon_owned, 0);
        if (res->status == GTV_UPDATE_CURRENT) {
            update_log("check result: up to date %s", GTV_VERSION);
            gchar *u8msg = g_strdup_printf(gtv_i18n("Bạn đang dùng phiên bản mới nhất (%s).",
                                                   "You are using the latest version (%s)."), GTV_VERSION);
            gunichar2 *wmsg = g_utf8_to_utf16(u8msg, -1, NULL, NULL, NULL);
            gunichar2 *wtitle = g_utf8_to_utf16(gtv_i18n("Cập nhật GoTV", "GoTV Update"), -1, NULL, NULL, NULL);
            MessageBoxW(g_app.hwnd_main, (LPCWSTR)wmsg, (LPCWSTR)wtitle, MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            g_free(u8msg); g_free(wmsg); g_free(wtitle);
        } else {
            update_log("check result: check error");
            /* Name the real directory instead of guessing %APPDATA%: glib
             * resolves the user config dir elsewhere on Windows, so the
             * hardcoded path pointed at a folder that does not exist. */
            gchar *dir = gtv_app_config_dir();
            gchar *u8msg = g_strdup_printf(gtv_i18n("Không kiểm tra được bản mới. Có thể do mạng, proxy hoặc "
                                                   "GitHub API đang giới hạn truy cập.\n\nChi tiết trong "
                                                   "%s\\update.log\n(chạy: curl --fail https://api.github.com/repos/isthaison/gotv/releases/latest để kiểm tra).",
                                                   "Unable to check for updates. This may be due to network, proxy or "
                                                   "GitHub API rate limiting.\n\nDetails in "
                                                   "%s\\update.log\n(run: curl --fail https://api.github.com/repos/isthaison/gotv/releases/latest to check)."),
                                          dir ? dir : "?");
            g_free(dir);
            gunichar2 *wmsg = g_utf8_to_utf16(u8msg, -1, NULL, NULL, NULL);
            gunichar2 *wtitle = g_utf8_to_utf16(gtv_i18n("Cập nhật GoTV", "GoTV Update"), -1, NULL, NULL, NULL);
            MessageBoxW(g_app.hwnd_main, (LPCWSTR)wmsg, (LPCWSTR)wtitle, MB_OK | MB_ICONWARNING | MB_TOPMOST);
            g_free(u8msg); g_free(wmsg); g_free(wtitle);
        }
    }
    g_free(res->tag); g_free(res->url); g_free(res);
}

static gpointer download_worker(gpointer data) {
    gchar *url = data;
    gchar *name = g_path_get_basename(url);
    /* Temp dir and username may contain non-ASCII: stay in UTF-16 until
     * the last step, convert only for curl's argv (UTF-8). */
    WCHAR tdir[MAX_PATH];
    gchar *dest = NULL;
    if (GetTempPathW(MAX_PATH, tdir)) {
        gunichar2 *wname = g_utf8_to_utf16(name ? name : "setup.exe", -1, NULL, NULL, NULL);
        WCHAR *wfull = g_new(WCHAR, MAX_PATH + 256);
        swprintf(wfull, MAX_PATH + 256, L"%ls%ls", tdir, (WCHAR*)wname);
        g_free(wname);
        dest = g_utf16_to_utf8(wfull, -1, NULL, NULL, NULL);
        g_free(wfull);
    }
    g_free(name);
    update_log("downloading %s to %s", url, dest ? dest : "null");
    gboolean ok = dest && gtv_update_download(url, dest);
    update_log("download finished: ok=%d dest=%s", ok, dest ? dest : "null");
    g_free(url);
    if (ok && g_app.hwnd_main) {
        PostMessage(g_app.hwnd_main, WM_GTV_UPDATE_DOWNLOADED, 0, (LPARAM)dest);
    } else {
        update_log("download failed or main window missing");
        gtv_tray_balloon_force(gtv_i18n("GoTV cập nhật", "GoTV Update"),
                               gtv_i18n("Tải bản mới thất bại. Vui lòng thử lại sau.", "Failed to download update. Please try again later."));
        g_free(dest);
    }
    return NULL;
}

gboolean gtv_update_balloon_clicked(void) {
    if (InterlockedCompareExchange(&update_balloon_owned, 0, 0) != 1) return FALSE;
    if (!pending_tag || !pending_url) {
        InterlockedExchange(&update_balloon_owned, 0);
        return FALSE;
    }
    /* The balloon slot now belongs to the download progress message. */
    gchar *msg = g_strdup_printf(gtv_i18n("Đang tải bản %s...", "Downloading version %s..."), pending_tag);
    update_log("download start tag=%s", pending_tag);
    gtv_tray_balloon_force(gtv_i18n("GoTV cập nhật", "GoTV Update"), msg);
    g_free(msg);
    gchar *url = pending_url;
    pending_url = NULL;
    g_free(pending_tag);
    pending_tag = NULL;
    GThread *th = g_thread_new("gtv-update-dl", download_worker, url);
    if (!th) {
        g_free(url);
        InterlockedExchange(&update_balloon_owned, 0);
        return TRUE;
    }
    g_thread_unref(th);
    return TRUE;
}

void gtv_update_disown_balloon(void) {
    InterlockedExchange(&update_balloon_owned, 0);
}

/* Launch the downloaded installer.
 *
 * ShellExecuteW alone is not enough: it has answered ERROR_ACCESS_DENIED (5)
 * for every release since v0.8.19 on some hosts, and the old code hid that by
 * retrying without parameters and without logging the outcome -- so the
 * self-update looked like it did nothing and there was no record of why.
 * Try the shell first (it understands Authenticode/MOTW policy), then fall
 * back to CreateProcessW, which is what gtv.sh uses and is known to work.
 *
 * /VERYSILENT rather than /SILENT: /SILENT still shows a progress window, so
 * the "silent" update was never actually silent. The installer log is kept so
 * a failed update is diagnosable instead of silent. */
static BOOL update_launch_installer(const gchar *installer_path) {
    gunichar2 *winstaller = g_utf8_to_utf16(installer_path, -1, NULL, NULL, NULL);
    if (!winstaller) return FALSE;

    /* Inno log into the temp dir so a failed update is diagnosable. */
    WCHAR tdir[MAX_PATH];
    WCHAR wlog[MAX_PATH + 32];
    if (GetTempPathW(MAX_PATH, tdir))
        swprintf(wlog, MAX_PATH + 32, L"/VERYSILENT /NORESTART /LOG=\"%lsgtv-update-install.log\"",
                 (LPCWSTR)tdir);
    else
        wcscpy(wlog, L"/VERYSILENT /NORESTART /LOG=\"gtv-update-install.log\"");

    update_log("installer launching %s flags=%ls", installer_path, (LPCWSTR)wlog);

    /* Use ShellExecuteEx so a launched process is distinguishable from a
     * refused one; ShellExecuteW only yields a <=32 magic value. */
    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = (LPCWSTR)winstaller;
    sei.lpParameters = wlog;
    sei.nShow = SW_HIDE;

    BOOL launched = FALSE;
    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) CloseHandle(sei.hProcess);
        launched = TRUE;
        update_log("ShellExecuteExW launched installer");
    } else {
        update_log("ShellExecuteExW failed gle=%lu, falling back to CreateProcessW",
                   (unsigned long)GetLastError());
    }

    if (!launched) {
        /* Direct launch, detached: the tray quits right after this returns. */
        WCHAR cmd[2 * MAX_PATH + 160];
        swprintf(cmd, 2 * MAX_PATH + 160, L"\"%ls\" %ls",
                 (LPCWSTR)winstaller, (LPCWSTR)wlog);
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        ZeroMemory(&pi, sizeof(pi));
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                           NULL, NULL, &si, &pi)) {
            if (pi.hThread) CloseHandle(pi.hThread);
            if (pi.hProcess) CloseHandle(pi.hProcess);
            launched = TRUE;
            update_log("CreateProcessW launched installer");
        } else {
            update_log("CreateProcessW failed gle=%lu",
                       (unsigned long)GetLastError());
        }
    }

    g_free(winstaller);
    return launched;
}

void gtv_update_on_downloaded(gchar *installer_path) {
    InterlockedExchange(&update_balloon_owned, 0);
    if (!installer_path || !*installer_path) {
        update_log("download failed");
        gtv_tray_balloon_force(gtv_i18n("GoTV cập nhật", "GoTV Update"),
                               gtv_i18n("Tải bản mới thất bại. Thử lại sau.", "Failed to download update. Try again later."));
        g_free(installer_path);
        return;
    }
    gtv_tray_balloon_force(gtv_i18n("GoTV cập nhật", "GoTV Update"),
                           gtv_i18n("Đang cài đặt phiên bản mới...", "Installing new version..."));
    if (!update_launch_installer(installer_path)) {
        update_log("installer could not be launched; see hint below");
        /* Resolve the real temp dir: GetTempPathW can point at
         * C:\Windows\Temp for some accounts, so a hardcoded
         * %LOCALAPPDATA%\Temp would again name a folder that does not
         * hold the log. */
        WCHAR tdir[MAX_PATH];
        gchar *logpath;
        if (GetTempPathW(MAX_PATH, tdir))
            logpath = g_utf16_to_utf8(tdir, -1, NULL, NULL, NULL);
        else
            logpath = g_strdup("");
        gchar *msg = g_strdup_printf(
            gtv_i18n("Không chạy được bộ cài. Mở %sgtv-update-install.log hoặc tải thủ công từ trang Releases.",
                     "Could not launch installer. Check %sgtv-update-install.log or download manually from Releases."),
            logpath ? logpath : "");
        gtv_tray_balloon_force(gtv_i18n("GoTV cập nhật", "GoTV Update"), msg);
        g_free(msg);
        g_free(logpath);
        g_free(installer_path);
        Sleep(500);
        PostQuitMessage(0);
        return;
    }
    g_free(installer_path);
    Sleep(500);
    PostQuitMessage(0);
}
