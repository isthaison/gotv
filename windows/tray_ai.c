#include "tray.h"
#include "tray_ai.h"
#include "app.h"
#include "update.h"
#include "internal.h"

static CRITICAL_SECTION learned_lock;
static GHashTable *w_learned_words = NULL;
static GHashTable *w_learned_fixes = NULL;
static gchar *pending_typed = NULL;
static gchar *pending_fix = NULL;
static volatile LONG ai_word_generation = 0;
static LONG pending_ai_generation = 0;

void gtv_tray_ai_init(void) {
    InitializeCriticalSection(&learned_lock);
}

void gtv_tray_ai_cleanup(void) {
    g_free(pending_typed);
    g_free(pending_fix);
    if (w_learned_words) g_hash_table_unref(w_learned_words);
    if (w_learned_fixes) g_hash_table_unref(w_learned_fixes);
    DeleteCriticalSection(&learned_lock);
}

static void w_learned_ensure(void) {
    if (!w_learned_words) {
        w_learned_words = gtv_words_table_new();
        gchar *p = gtv_learned_path("learned-words.txt");
        gtv_words_load(w_learned_words, p);
        g_free(p);
    }
    if (!w_learned_fixes) {
        w_learned_fixes = gtv_fixes_table_new();
        gchar *p = gtv_learned_path("learned-corrections.txt");
        gtv_fixes_load(w_learned_fixes, p);
        g_free(p);
    }
}

void gtv_tray_suggest_balloon(const gchar *typed, const gchar *correction) {
    /* Disabled: spelling and typo suggestions are shown directly in the
     * inline TSF candidate popup at the caret while typing, rather than
     * popping up Windows tray toast/balloon notifications after Space. */
    (void)typed;
    (void)correction;
}

static void copy_to_clipboard(const gchar *utf8) {
    if (!utf8 || !g_app.hwnd_main) return;
    glong wlen = 0;
    gunichar2 *wstr = g_utf8_to_utf16(utf8, -1, NULL, &wlen, NULL);
    if (!wstr) return;
    if (OpenClipboard(g_app.hwnd_main)) {
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(gunichar2));
        if (h) {
            gunichar2 *dst = GlobalLock(h);
            if (dst) {
                memcpy(dst, wstr, (wlen + 1) * sizeof(gunichar2));
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            } else {
                GlobalFree(h);
            }
        }
        CloseClipboard();
    }
    g_free(wstr);
}

static void learn_accepted(const gchar *typed, const gchar *fix) {
    EnterCriticalSection(&learned_lock);
    w_learned_ensure();
    gboolean dirty_fix = gtv_fixes_learn(w_learned_fixes, typed, fix);
    gboolean dirty_words = FALSE;
    gchar **words = g_strsplit_set(fix, " \t", -1);
    for (guint i = 0; words[i]; i++) {
        if (gtv_words_learn(w_learned_words, words[i]))
            dirty_words = TRUE;
    }
    g_strfreev(words);
    if (dirty_fix) {
        gchar *p = gtv_learned_path("learned-corrections.txt");
        gtv_fixes_save(w_learned_fixes, p);
        g_free(p);
    }
    if (dirty_words) {
        gchar *p = gtv_learned_path("learned-words.txt");
        gtv_words_save(w_learned_words, p);
        g_free(p);
    }
    LeaveCriticalSection(&learned_lock);
}

static void send_replace_text(glong erase_chars, const gchar *utf8) {
    glong wlen = 0;
    guint16 *wstr = NULL;
    if (utf8 && *utf8) wstr = g_utf8_to_utf16(utf8, -1, NULL, &wlen, NULL);
    if (!wstr) wlen = 0;
    if (erase_chars <= 0 && wlen == 0) {
        g_free(wstr);
        return;
    }

    guint total = (guint)(erase_chars * 2 + wlen * 2);
    INPUT *inputs = g_new0(INPUT, total);
    guint k = 0;
    for (glong i = 0; i < erase_chars; i++) {
        inputs[k].type = INPUT_KEYBOARD;
        inputs[k].ki.wVk = VK_BACK;
        k++;
        inputs[k].type = INPUT_KEYBOARD;
        inputs[k].ki.wVk = VK_BACK;
        inputs[k].ki.dwFlags = KEYEVENTF_KEYUP;
        k++;
    }
    for (glong i = 0; i < wlen; i++) {
        inputs[k].type = INPUT_KEYBOARD;
        inputs[k].ki.wScan = wstr[i];
        inputs[k].ki.dwFlags = KEYEVENTF_UNICODE;
        k++;
        inputs[k].type = INPUT_KEYBOARD;
        inputs[k].ki.wScan = wstr[i];
        inputs[k].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        k++;
    }
    SendInput(total, inputs, sizeof(INPUT));
    g_free(inputs);
    g_free(wstr);
}

void gtv_tray_apply_pending(void) {
    if (!pending_typed || !pending_fix) return;
    gchar *typed = pending_typed;
    gchar *fix = pending_fix;
    pending_typed = pending_fix = NULL;
    if (ai_word_generation == pending_ai_generation
        && g_utf8_validate(typed, -1, NULL) && g_utf8_validate(fix, -1, NULL)
        && g_utf8_strlen(typed, -1) >= 2 && g_utf8_strlen(typed, -1) <= 64) {
        send_replace_text(g_utf8_strlen(typed, -1), fix);
    } else {
        copy_to_clipboard(fix);
    }
    learn_accepted(typed, fix);
    g_free(typed);
    g_free(fix);
}

void gtv_tray_ai_word(gchar *word) {
    if (!word || !*word) {
        g_free(word);
        return;
    }
    InterlockedIncrement(&ai_word_generation);
    g_free(word);
}

gboolean gtv_tray_should_check(const gchar *word) {
    (void)word;
    return FALSE;
}

void gtv_tray_check_spelling_async(const gchar *word) {
    (void)word;
}
