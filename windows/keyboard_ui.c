/* Keyboard (Win+Space) manager dialog.
 *
 * Two dialogs:
 *   IDD_KEYBOARD_DIALOG  - the user's keyboard list in switcher order, with
 *                          add / remove / move up / move down, plus
 *                          "Mac dinh" to get GoTV's setup back.
 *   IDD_KBD_ADD_DIALOG   - picker for one language's available keyboards.
 *
 * The list order IS the Win+Space order, so moving a row up/down is all it
 * takes to reorder the switcher. Saving writes the whole list back with
 * Set-WinUserLanguageList and reads it back, so a keyboard Windows refuses
 * is reported instead of disappearing silently.
 *
 * Reading and writing both spawn powershell (~1s), so both run on a worker
 * thread that posts the finished job back to the dialog. The dialog only
 * ever touches the list from WM_KBD_DONE, never from the worker. */
#include "keyboard.h"
#include "input_setup.h"
#include "resource.h"
#include "win_utf.h"
#include "engine.h"

#include <windows.h>
#include <commctrl.h>
#include <wchar.h>
#include <glib.h>

/* Private to this file: worker -> dialog. */
#define WM_KBD_DONE (WM_APP + 40)

#define GTV_KBD_MAX_LANGS 64

typedef struct {
    HWND          hwnd;
    GtvKbdList   *list;   /* read target, or the list to save */
    gboolean      is_save;
    gboolean      ok;
    gchar        *error;
} KbdJob;

typedef struct {
    GtvKbdList *working;  /* what the user is editing */
    gboolean    dirty;
    gboolean    busy;    /* a powershell job is in flight */
} KbdState;

static void msg_box(HWND hwnd, const gchar *utf8, UINT flags) {
    gunichar2 *w = g_utf8_to_utf16(utf8 ? utf8 : "", -1, NULL, NULL, NULL);
    gunichar2 *t = g_utf8_to_utf16("GoTV", -1, NULL, NULL, NULL);
    if (w && t)
        MessageBoxW(hwnd, (LPCWSTR)w, (LPCWSTR)t, flags);
    g_free(w);
    g_free(t);
}

static int ask(HWND hwnd, const gchar *utf8) {
    gunichar2 *w = g_utf8_to_utf16(utf8 ? utf8 : "", -1, NULL, NULL, NULL);
    gunichar2 *t = g_utf8_to_utf16("GoTV", -1, NULL, NULL, NULL);
    int r = IDCANCEL;
    if (w && t) r = MessageBoxW(hwnd, (LPCWSTR)w, (LPCWSTR)t, MB_YESNO | MB_ICONQUESTION);
    g_free(w);
    g_free(t);
    return r;
}

static int cmp_langid(gconstpointer a, gconstpointer b) {
    LANGID x = *(const LANGID *)a, y = *(const LANGID *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* --------------------------------------------------------------- workers */

static void job_post(KbdJob *job) {
    /* If the dialog is already gone the post fails and the job is dropped;
     * freeing it here would race the dialog's own free on the happy path. */
    PostMessageW(job->hwnd, WM_KBD_DONE, 0, (LPARAM)job);
}

static void job_free(KbdJob *job) {
    if (!job) return;
    gtv_kbd_list_free(job->list);
    g_free(job->error);
    g_free(job);
}

static gpointer worker_read(gpointer data) {
    KbdJob *job = data;
    GError *err = NULL;
    job->ok = gtv_kbd_list_load(job->list, &err);
    if (!job->ok && err) job->error = g_strdup(err->message);
    g_clear_error(&err);
    job_post(job);
    return NULL;
}

static gpointer worker_write(gpointer data) {
    KbdJob *job = data;
    GError *err = NULL;
    job->ok = gtv_kbd_list_apply(job->list, &err);
    if (!job->ok) {
        if (err) job->error = g_strdup(err->message);
    } else {
        /* Windows groups each language's keyboards together, so the saved
         * order is not necessarily the order the user arranged. Re-read so
         * the list shows what the switcher really ended up with. */
        GError *reload_err = NULL;
        if (!gtv_kbd_list_load(job->list, &reload_err))
            g_clear_error(&reload_err);
    }
    g_clear_error(&err);
    job_post(job);
    return NULL;
}

static void run_job(HWND hwnd, GThreadFunc fn) {
    KbdJob *job = g_new0(KbdJob, 1);
    job->hwnd = hwnd;
    job->list = gtv_kbd_list_new();
    GThread *th = g_thread_new("gtv-kbd", fn, job);
    if (!th) {
        job_free(job);
        return;
    }
    g_thread_unref(th);
}

/* ------------------------------------------------------------------ list */

static void list_columns(HWND list) {
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    struct { const char *text; int width; } cols[] = {
        { "Bàn phím", 112 },
        { "Ngôn ngữ", 92 },
    };
    for (int i = 0; i < 2; i++) {
        gunichar2 *w = g_utf8_to_utf16(cols[i].text, -1, NULL, NULL, NULL);
        LVCOLUMNW col = {0};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)w;
        col.cx = cols[i].width;
        col.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        g_free(w);
    }
}

static void status_set(HWND hwnd, const gchar *utf8) {
    gtv_win_set_dlg_item_text(hwnd, IDC_KBD_STATUS, utf8 ? utf8 : "");
}

static void list_select(HWND list, int row) {
    LVITEMW item = {0};
    item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    item.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&item);
    SendMessageW(list, LVM_ENSUREVISIBLE, (WPARAM)row, FALSE);
}

static void list_fill(HWND hwnd) {
    KbdState *st = (KbdState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    HWND list = GetDlgItem(hwnd, IDC_KBD_LIST);
    int sel = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    if (!st) return;
    for (guint i = 0; i < st->working->items->len; i++) {
        const GtvKbd *k = &g_array_index(st->working->items, GtvKbd, i);
        gchar *name = gtv_kbd_tip_name(k->tip);
        LVITEMW item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        gunichar2 *wn = g_utf8_to_utf16(name ? name : k->tip, -1, NULL, NULL, NULL);
        item.pszText = (LPWSTR)(wn ? wn : L"");
        SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
        g_free(wn);
        g_free(name);
        gunichar2 *wl = g_utf8_to_utf16(k->lang, -1, NULL, NULL, NULL);
        item.iSubItem = 1;
        item.pszText = (LPWSTR)(wl ? wl : L"");
        SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&item);
        g_free(wl);
    }
    if (sel >= 0 && sel < (int)st->working->items->len) list_select(list, sel);
}

static int list_selected(HWND hwnd) {
    return (int)SendMessageW(GetDlgItem(hwnd, IDC_KBD_LIST),
                             LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
}

/* ----------------------------------------------------------- add dialog */

typedef struct {
    GtvKbdList *working;          /* borrowed: appended to directly */
    LANGID      langids[GTV_KBD_MAX_LANGS];
    guint       n_langids;
    GPtrArray  *choices;          /* GtvKbdChoice* for the shown language */
} AddState;

static LANGID add_langid(AddState *st, int index) {
    if (index < 0 || (guint)index >= st->n_langids) return 0;
    return st->langids[index];
}

/* Rows already present in the list are hidden: adding one again is a no-op
 * that only makes the list harder to read. avail_rows is the number of
 * visible rows so a view row can be mapped back to a choice. */
static guint avail_fill(HWND hwnd) {
    AddState *st = (AddState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    HWND list = GetDlgItem(hwnd, IDC_KBD_AVAIL);
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    if (!st) return 0;
    g_clear_pointer(&st->choices, gtv_kbd_choices_free);
    int idx = (int)SendMessageW(GetDlgItem(hwnd, IDC_KBD_LANG), CB_GETCURSEL, 0, 0);
    LANGID langid = add_langid(st, idx);
    if (!langid) return 0;
    st->choices = gtv_kbd_choices_for_langid(langid);
    guint rows = 0;
    for (guint i = 0; i < st->choices->len; i++) {
        const GtvKbdChoice *c = g_ptr_array_index(st->choices, i);
        if (gtv_kbd_list_has(st->working, c->tip)) continue;
        LVITEMW item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = (int)rows;
        gunichar2 *w = g_utf8_to_utf16(c->name ? c->name : c->tip, -1, NULL, NULL, NULL);
        item.pszText = (LPWSTR)(w ? w : L"");
        SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
        g_free(w);
        rows++;
    }
    if (rows) list_select(list, 0);
    return rows;
}

/* Map a visible row back to its GtvKbdChoice. */
static const GtvKbdChoice *avail_pick(AddState *st, int row) {
    if (!st->choices || row < 0) return NULL;
    guint seen = 0;
    for (guint i = 0; i < st->choices->len; i++) {
        const GtvKbdChoice *c = g_ptr_array_index(st->choices, i);
        if (gtv_kbd_list_has(st->working, c->tip)) continue;
        if (seen == (guint)row) return c;
        seen++;
    }
    return NULL;
}

static void on_avail_add(HWND hwnd) {
    AddState *st = (AddState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    int sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_KBD_AVAIL),
                                LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
    const GtvKbdChoice *pick = avail_pick(st, sel);
    if (!pick) return;
    LANGID langid = add_langid(st, (int)SendMessageW(GetDlgItem(hwnd, IDC_KBD_LANG),
                                                      CB_GETCURSEL, 0, 0));
    if (!langid) return;
    /* Reuse the tag the list already uses for this language: Windows accepts
     * both "vi" and "vi-VN", and a new spelling would rebuild the language
     * as a second entry. Only a genuinely new language needs a fresh tag. */
    gchar *lang = gtv_kbd_existing_tag(st->working, langid);
    if (!lang) lang = gtv_kbd_tag_for_langid(langid);
    if (!lang) return;
    gtv_kbd_list_add(st->working, lang, pick->tip);
    g_free(lang);
    /* Stay open so several keyboards can be added in a row. */
    avail_fill(hwnd);
}

static INT_PTR CALLBACK AddDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            AddState *st = (AddState *)lParam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
            gtv_win_set_window_text(hwnd, "GoTV - Thêm bàn phím");

            /* Offer the languages already in the list, plus Vietnamese: that
             * is where GoTV lives, even if the vi language was removed.
             * Derived from the parent list (no extra powershell call). */
            const LANGID VI = 0x042a;
            GArray *ids = g_array_new(FALSE, FALSE, sizeof(LANGID));
            gtv_kbd_langids_of(st->working, ids);
            gboolean has_vi = FALSE;
            for (guint i = 0; i < ids->len; i++)
                if (g_array_index(ids, LANGID, i) == VI) has_vi = TRUE;
            if (!has_vi) g_array_append_val(ids, VI);
            g_array_sort(ids, (GCompareFunc)cmp_langid);

            HWND combo = GetDlgItem(hwnd, IDC_KBD_LANG);
            int vi_row = -1;
            for (guint i = 0; i < ids->len && i < GTV_KBD_MAX_LANGS; i++) {
                LANGID id = g_array_index(ids, LANGID, i);
                WCHAR name[48] = {0};
                GetLocaleInfoW((LCID)id, LOCALE_SENGLANGUAGE, name, 48);
                if (!name[0]) _snwprintf(name, 48, L"0x%04x", (unsigned)id);
                if (id == VI) { wcscat_s(name, 48, L" (GoTV)"); vi_row = (int)st->n_langids; }
                st->langids[st->n_langids++] = id;
                gchar *u8 = g_utf16_to_utf8((const gunichar2 *)name, -1, NULL, NULL, NULL);
                gunichar2 *w = g_utf8_to_utf16(u8 ? u8 : "", -1, NULL, NULL, NULL);
                if (w) { SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)w); g_free(w); }
                g_free(u8);
            }
            g_array_free(ids, TRUE);

            /* Default to Vietnamese: the language GoTV types in. */
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)(vi_row >= 0 ? vi_row : 0), 0);
            if (SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR)
                SendMessageW(combo, CB_SETCURSEL, 0, 0);

            list_columns(GetDlgItem(hwnd, IDC_KBD_AVAIL));
            SendMessageW(GetDlgItem(hwnd, IDC_KBD_AVAIL), LVM_SETCOLUMNWIDTH, 0, 226);
            avail_fill(hwnd);
            return TRUE;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_KBD_LANG && HIWORD(wParam) == CBN_SELCHANGE) {
                avail_fill(hwnd);
                return TRUE;
            }
            if (id == IDC_KBD_AVAIL_ADD) { on_avail_add(hwnd); return TRUE; }
            if (id == IDCANCEL || id == IDOK) { EndDialog(hwnd, IDOK); return TRUE; }
            break;
        }
        case WM_NOTIFY: {
            /* Double-click a row = add it, like the Windows language list. */
            LPNMHDR hdr = (LPNMHDR)lParam;
            if (hdr && hdr->idFrom == IDC_KBD_AVAIL && hdr->code == NM_DBLCLK) {
                on_avail_add(hwnd);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------ main dialog */

static void set_busy(HWND hwnd, gboolean busy) {
    static const int ids[] = { IDC_KBD_UP, IDC_KBD_DOWN, IDC_KBD_REMOVE,
                               IDC_KBD_ADD, IDC_KBD_DEFAULT, IDC_KBD_CLOSE };
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++)
        EnableWindow(GetDlgItem(hwnd, ids[i]), !busy);
}

static void on_move(HWND hwnd, gint delta) {
    KbdState *st = (KbdState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    int sel = list_selected(hwnd);
    gint target = sel + delta;
    if (sel < 0 || target < 0 || target >= (gint)st->working->items->len) return;
    gtv_kbd_list_move(st->working, (guint)sel, (guint)target);
    st->dirty = TRUE;
    list_fill(hwnd);
}

static void on_remove(HWND hwnd) {
    KbdState *st = (KbdState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    int sel = list_selected(hwnd);
    if (sel < 0) return;
    if (st->working->items->len <= 1) {
        msg_box(hwnd,
                gtv_i18n("Cần ít nhất một bàn phím.\n"
                         "Windows không cho phép Win+Space không có gì để chuyển.",
                         "At least one keyboard is required.\n"
                         "Windows does not allow an empty keyboard list."),
                MB_OK | MB_ICONWARNING);
        return;
    }
    g_array_remove_index(st->working->items, sel);
    st->dirty = TRUE;
    list_fill(hwnd);
}

static void on_default(HWND hwnd) {
    KbdState *st = (KbdState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    if (ask(hwnd,
            gtv_i18n("Khôi phục mặc định của GoTV?\n\n"
                     "Anh (US) + Tiếng Việt (GoTV).\n"
                     "Các bàn phím khác bạn đã thêm sẽ bị gỡ.",
                     "Restore GoTV defaults?\n\n"
                     "English (US) + Vietnamese (GoTV).\n"
                     "Other keyboards you added will be removed.")) != IDYES)
        return;
    gtv_kbd_list_set_default(st->working);
    st->dirty = TRUE;
    list_fill(hwnd);
    status_set(hwnd, gtv_i18n("Đã đưa về mặc định — bấm \"Đóng\" để lưu.",
                              "Restored to defaults — click \"Close\" to save."));
}

static void on_add(HWND hwnd) {
    KbdState *st = (KbdState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    /* The picker appends to the working list directly, so snapshot it to be
     * able to tell afterwards whether anything was actually added. */
    GtvKbdList *before = gtv_kbd_list_new();
    gtv_kbd_list_copy(before, st->working);
    AddState add = {0};
    add.working = st->working;
    DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_KBD_ADD_DIALOG),
                   hwnd, AddDlgProc, (LPARAM)&add);
    g_clear_pointer(&add.choices, gtv_kbd_choices_free);
    if (!gtv_kbd_list_equals(before, st->working)) {
        st->dirty = TRUE;
        status_set(hwnd, gtv_i18n("Đã thêm bàn phím — bấm \"Đóng\" để lưu.",
                                  "Keyboard added — click \"Close\" to save."));
    }
    gtv_kbd_list_free(before);
    list_fill(hwnd);
}

static void on_close(HWND hwnd) {
    KbdState *st = (KbdState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    /* set_busy disables the buttons, but WM_CLOSE still arrives (X button,
     * Alt+F4): ignore it rather than start a second concurrent save. */
    if (st && st->busy) return;
    if (st && st->dirty) {
        if (ask(hwnd,
                gtv_i18n("Lưu danh sách bàn phím mới?\n\n"
                         "Chọn \"Có\" để lưu, \"Không\" để bỏ qua.",
                         "Save new keyboard list?\n\n"
                         "Select \"Yes\" to save, \"No\" to discard.")) != IDYES) {
            EndDialog(hwnd, IDCANCEL);
            return;
        }
        /* Release any active 0x042a HKL on the UI thread before the worker
         * resets and unloads stale Vietnamese layouts around Set-WinUserLanguageList. */
        if (LOWORD((ULONG_PTR)GetKeyboardLayout(0)) == 0x042a) {
            ActivateKeyboardLayout((HKL)(ULONG_PTR)0x04090409, 0);
        }
        /* Save a snapshot so the worker never touches the dialog's list, and
         * keep the dialog open (buttons disabled) until WM_KBD_DONE. */
        KbdJob *job = g_new0(KbdJob, 1);
        job->hwnd = hwnd;
        job->list = gtv_kbd_list_new();
        gtv_kbd_list_copy(job->list, st->working);
        job->is_save = TRUE;
        st->busy = TRUE;
        set_busy(hwnd, TRUE);
        status_set(hwnd, gtv_i18n("Đang lưu danh sách bàn phím...", "Saving keyboard list..."));
        GThread *th = g_thread_new("gtv-kbd-save", worker_write, job);
        if (th) g_thread_unref(th);
        else {
            job_free(job);
            st->busy = FALSE;
            set_busy(hwnd, FALSE);
            status_set(hwnd, gtv_i18n("Không tạo được luồng lưu.", "Could not create save worker."));
        }
        return;
    }
    EndDialog(hwnd, IDOK);
}

static INT_PTR CALLBACK KbdDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            struct { int id; const char *text; } labels[] = {
                { IDC_KBD_LBL_CURRENT, gtv_i18n("Danh sách bàn phím (thứ tự Win+Space)", "Keyboard list (Win+Space order)") },
                { IDC_KBD_UP, gtv_i18n("Lên", "Up") },
                { IDC_KBD_DOWN, gtv_i18n("Xuống", "Down") },
                { IDC_KBD_REMOVE, gtv_i18n("Xóa", "Remove") },
                { IDC_KBD_ADD, gtv_i18n("Thêm...", "Add...") },
                { IDC_KBD_DEFAULT, gtv_i18n("Mặc định", "Default") },
                { IDC_KBD_CLOSE, gtv_i18n("Đóng", "Close") },
            };
            gtv_win_set_window_text(hwnd, gtv_i18n("GoTV - Bàn phím", "GoTV - Keyboards"));
            for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
                gtv_win_set_dlg_item_text(hwnd, labels[i].id, labels[i].text);

            KbdState *st = g_new0(KbdState, 1);
            st->working = gtv_kbd_list_new();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

            list_columns(GetDlgItem(hwnd, IDC_KBD_LIST));
            SendMessageW(GetDlgItem(hwnd, IDC_KBD_LIST), LVM_SETCOLUMNWIDTH, 0, 112);
            SendMessageW(GetDlgItem(hwnd, IDC_KBD_LIST), LVM_SETCOLUMNWIDTH, 1, 92);

            set_busy(hwnd, TRUE);
            status_set(hwnd, gtv_i18n("Đang đọc danh sách bàn phím...", "Reading keyboard list..."));
            run_job(hwnd, worker_read);

            RECT rc, desk;
            GetWindowRect(hwnd, &rc);
            GetWindowRect(GetDesktopWindow(), &desk);
            SetWindowPos(hwnd, HWND_TOP,
                         (desk.right - (rc.right - rc.left)) / 2,
                         (desk.bottom - (rc.bottom - rc.top)) / 2,
                         0, 0, SWP_NOSIZE);
            return TRUE;
        }

        case WM_KBD_DONE: {
            KbdJob *job = (KbdJob *)lParam;
            KbdState *st = (KbdState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (job && st && job->is_save) {
                st->busy = FALSE;
                if (job->ok) {
                    /* The user now owns the list: remember that so the
                     * automatic first-run setup stops overwriting it. */
                    gtv_kbd_mark_custom();
                    gtv_input_setup_mark_done();
                    gtv_kbd_list_copy(st->working, job->list);
                    st->dirty = FALSE;
                    list_fill(hwnd);
                    status_set(hwnd, gtv_i18n("Đã lưu danh sách bàn phím.", "Keyboard list saved."));
                    set_busy(hwnd, FALSE);
                } else {
                    status_set(hwnd, job->error ? job->error : gtv_i18n("Không lưu được.", "Failed to save."));
                    msg_box(hwnd,
                            job->error ? job->error
                                       : gtv_i18n("Không áp dụng được danh sách bàn phím.", "Could not apply keyboard list."),
                            MB_OK | MB_ICONWARNING);
                    set_busy(hwnd, FALSE);
                }
                job_free(job);
                return TRUE;
            }
            if (st && job) {
                st->busy = FALSE;
                if (job->ok) {
                    gtv_kbd_list_copy(st->working, job->list);
                    st->dirty = FALSE;
                    list_fill(hwnd);
                    status_set(hwnd, "");
                } else {
                    status_set(hwnd, job->error ? job->error
                                               : gtv_i18n("Không đọc được danh sách bàn phím.", "Could not read keyboard list."));
                }
                set_busy(hwnd, FALSE);
            }
            job_free(job);
            return TRUE;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_KBD_UP) { on_move(hwnd, -1); return TRUE; }
            if (id == IDC_KBD_DOWN) { on_move(hwnd, 1); return TRUE; }
            if (id == IDC_KBD_REMOVE) { on_remove(hwnd); return TRUE; }
            if (id == IDC_KBD_ADD) { on_add(hwnd); return TRUE; }
            if (id == IDC_KBD_DEFAULT) { on_default(hwnd); return TRUE; }
            if (id == IDC_KBD_CLOSE || id == IDCANCEL || id == IDOK) {
                on_close(hwnd);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            on_close(hwnd);
            return TRUE;
        case WM_DESTROY: {
            KbdState *st = (KbdState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (st) {
                gtv_kbd_list_free(st->working);
                g_free(st);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            break;
        }
    }
    return FALSE;
}

void gtv_keyboard_show(HWND parent) {
    DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_KEYBOARD_DIALOG),
               parent, KbdDlgProc);
}