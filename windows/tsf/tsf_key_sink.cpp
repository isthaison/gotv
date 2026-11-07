#include "tsf_service.h"
#include "tsf_suggest.h"
#include "../app.h"

/* Report a committed word to the tray app for the AI typo check. The tray
 * owns suggestions/balloons/learning; TSF just types. Best-effort: the
 * tray may not run (portable use), and must never block typing. */
static void NotifyTrayWord(const char *word) {
    if (!word || !*word) return;
    HWND tray = FindWindowA(GTV_TRAY_WINDOW_CLASS, NULL);
    if (!tray) return;
    COPYDATASTRUCT cds;
    cds.dwData = (ULONG_PTR)GTV_AI_COPYDATA_ID;
    cds.cbData = (DWORD)strlen(word) + 1;
    cds.lpData = (PVOID)word;
    SendMessageTimeoutA(tray, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 500, NULL);
}

STDMETHODIMP CGtvTextService::OnSetFocus(BOOL fForeground)
{
    return S_OK;
}

STDMETHODIMP CGtvTextService::OnPreservedKey(ITfContext *pic, REFGUID rguid, BOOL *pfEaten)
{
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CGtvTextService::OnTestKeyUp(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten)
{
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CGtvTextService::OnKeyUp(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten)
{
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

static inline gboolean is_emoji_starter(gunichar ch) {
    return (ch == ':' || ch == ';' || ch == '<' || ch == '(');
}

static inline gboolean is_telex_shortcut(GtvEngine *engine, gunichar ch) {
    return (engine && engine->mode == GTV_TELEX &&
            (ch == '[' || ch == ']' || ch == '{' || ch == '}'));
}

static inline gboolean can_start_composition(GtvEngine *engine, gunichar ch) {
    return g_unichar_isalpha(ch) || is_emoji_starter(ch) || is_telex_shortcut(engine, ch);
}

static HKL us_layout(void) {
    static HKL layout = NULL;
    if (!layout) layout = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
    return layout ? layout : GetKeyboardLayout(0);
}

static int key_to_unicode(WPARAM wParam, LPARAM lParam, WCHAR *wchars, int cch) {
    BYTE key_state[256];
    GetKeyboardState(key_state);
    return ToUnicodeEx((UINT)wParam, (UINT)((lParam >> 16) & 0xFF),
        key_state, wchars, cch, 0, us_layout());
}

static HRESULT UpdateCompositionUtf8(CGtvTextService *service, ITfContext *pic, const gchar *utf8) {
    glong wlen = 0;
    wchar_t *wtext = (wchar_t*)g_utf8_to_utf16(utf8, -1, NULL, &wlen, NULL);
    if (!wtext) return E_FAIL;
    HRESULT hr = service->UpdateCompositionText(pic, wtext, (int)wlen);
    g_free(wtext);
    return hr;
}

static HRESULT CommitLiteralChar(CGtvTextService *service, ITfContext *pic, gunichar ch) {
    gunichar chars[2] = { ch, 0 };
    gchar *utf8 = g_ucs4_to_utf8(chars, 1, NULL, NULL, NULL);
    if (!utf8) return E_FAIL;
    HRESULT hr = UpdateCompositionUtf8(service, pic, utf8);
    g_free(utf8);
    if (SUCCEEDED(hr)) hr = service->EndComposition(pic, TRUE);
    return hr;
}

static void NotifyWordFromCommit(const gchar *commit) {
    if (!commit || !*commit) return;
    const gchar *end = commit + strlen(commit);
    const gchar *prev = g_utf8_prev_char(end);
    if (prev > commit) {
        gchar *wordonly = g_strndup(commit, prev - commit);
        NotifyTrayWord(wordonly);
        g_free(wordonly);
    }
}

static void UpdateEngineBuffer(CGtvTextService *service, ITfContext *pic,
                               GtvEngine *engine, gboolean end_when_empty) {
    gchar *buf = gtv_engine_buffer(engine);
    if (buf && *buf)
        UpdateCompositionUtf8(service, pic, buf);
    else if (end_when_empty)
        service->EndComposition(pic, FALSE);
    g_free(buf);
}

STDMETHODIMP CGtvTextService::OnTestKeyDown(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;

    // No software on/off: the TIP is active only while selected.
    // Switching to English is a Win+Space keyboard change (Windows owns it).

    // Check modifiers
    if ((GetKeyState(VK_CONTROL) & 0x8000) ||
        (GetKeyState(VK_MENU) & 0x8000) ||
        (GetKeyState(VK_LWIN) & 0x8000) ||
        (GetKeyState(VK_RWIN) & 0x8000)) {
        return S_OK;
    }

    if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_BACK) {
        if (IsComposing()) *pfEaten = TRUE;
        return S_OK;
    }

    if (wParam == VK_SPACE) {
        if (IsComposing()) *pfEaten = TRUE;
        return S_OK;
    }

    if (wParam == VK_TAB) {
        if (IsComposing()) {
            gchar *buf = gtv_engine_buffer(m_pEngine);
            gchar *expansion = expand_macro(buf);
            if (!expansion) expansion = expand_emoji(buf);
            g_free(buf);
            if (expansion) {
                g_free(expansion);
                *pfEaten = TRUE;
                return S_OK;
            }
            /* Inline suggestion highlight takes Tab when no exact match. */
            if (gtv_suggest_visible()) {
                *pfEaten = TRUE;
                return S_OK;
            }
        }
        return S_OK;
    }

    /* Suggestion navigation: Up/Down move (Tab accepts).
     * Number keys 1-5 are NOT eaten so users can type numbers freely (e.g. an5).
     * Eaten only while the popup is visible. */
    if ((wParam == VK_UP || wParam == VK_DOWN) && gtv_suggest_visible()) {
        *pfEaten = TRUE;
        return S_OK;
    }

    // Convert key through US layout: GoTV owns Telex/VNI; Windows' VIE
    // hardware layout must not turn number-row keys into Vietnamese chars.
    WCHAR wchars[4] = {0};
    int count = key_to_unicode(wParam, lParam, wchars, 4);
    if (count == 1 && wchars[0] >= 0x20) {
        *pfEaten = TRUE;
        return S_OK;
    }

    return S_OK;
}

STDMETHODIMP CGtvTextService::OnKeyDown(ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;

    if (!pic || !m_pEngine) return S_OK;

    // Modifiers reset composition
    if ((GetKeyState(VK_CONTROL) & 0x8000) ||
        (GetKeyState(VK_MENU) & 0x8000) ||
        (GetKeyState(VK_LWIN) & 0x8000) ||
        (GetKeyState(VK_RWIN) & 0x8000)) {
        if (IsComposing()) {
            EndComposition(pic, TRUE);
            gtv_engine_reset(m_pEngine);
        }
        gtv_suggest_hide();
        return S_OK;
    }

    // Escape cancels composition (dismisses the suggestion popup first)
    if (wParam == VK_ESCAPE) {
        if (IsComposing()) {
            if (gtv_suggest_visible()) {
                gtv_suggest_hide();
            } else {
                EndComposition(pic, FALSE);
                gtv_engine_reset(m_pEngine);
            }
            *pfEaten = TRUE;
        }
        return S_OK;
    }

    // Return commits the composition literally. The suggestion popup is
    // dismissed, never accepted: suggestions/emoji/macro are Tab-only
    // (or 1-5 / click), so Enter must not swap the typed word out.
    if (wParam == VK_RETURN) {
        if (IsComposing()) {
            gchar *commit = gtv_engine_process(m_pEngine, '\n', NULL);
            if (commit) {
                UpdateCompositionUtf8(this, pic, commit);
                NotifyWordFromCommit(commit);
                g_free(commit);
            }
            EndComposition(pic, TRUE);
            gtv_engine_reset(m_pEngine);
            gtv_suggest_hide();
            *pfEaten = TRUE;
        }
        return S_OK;
    }

    // Tab expands macro/emoji; else accepts the suggestion highlight;
    // if neither, commits buffer and passes Tab through
    if (wParam == VK_TAB) {
        if (IsComposing()) {
            guint bs = 0;
            gchar *macro = gtv_engine_process(m_pEngine, '\t', &bs);
            if (macro) {
                UpdateCompositionUtf8(this, pic, macro);
                EndComposition(pic, TRUE);
                NotifyWordFromCommit(macro);
                g_free(macro);
                gtv_suggest_hide();
                *pfEaten = TRUE;
                return S_OK;
            } else if (gtv_suggest_visible()) {
                gtv_suggest_accept_cursor(this, pic);
                *pfEaten = TRUE;
                return S_OK;
            } else {
                EndComposition(pic, TRUE);
                gtv_engine_reset(m_pEngine);
                gtv_suggest_hide();
                *pfEaten = FALSE;
                return S_OK;
            }
        }
        return S_OK;
    }

    // Suggestion highlight navigation (eaten in OnTestKeyDown; Tab accepts).
    if ((wParam == VK_UP || wParam == VK_DOWN) && gtv_suggest_visible()) {
        gtv_suggest_move_cursor(wParam == VK_DOWN ? 1 : -1);
        *pfEaten = TRUE;
        return S_OK;
    }

    // Backspace handles in-composition backspacing
    if (wParam == VK_BACK) {
        if (IsComposing()) {
            guint bs = 0;
            gtv_engine_process(m_pEngine, '\b', &bs);
            gchar *buf = gtv_engine_buffer(m_pEngine);
            if (!buf || !*buf) {
                EndComposition(pic, FALSE);
            } else {
                UpdateCompositionUtf8(this, pic, buf);
            }
            g_free(buf);
            gtv_suggest_on_key(this, pic);
            *pfEaten = TRUE;
            return S_OK;
        }
        return S_OK;
    }

    // Convert virtual key to Unicode character through US layout.
    WCHAR wchars[4] = {0};
    int count = key_to_unicode(wParam, lParam, wchars, 4);

    if (count != 1 || wchars[0] < 0x20) {
        if (IsComposing()) {
            EndComposition(pic, TRUE);
            gtv_engine_reset(m_pEngine);
        }
        return S_OK;
    }

    gunichar ch = (gunichar)wchars[0];

    // If not composing, only start composition on alphanumeric, emoji starter, or Telex shortcut
    if (!IsComposing()) {
        if (!can_start_composition(m_pEngine, ch)) {
            CommitLiteralChar(this, pic, ch);
            gtv_suggest_hide();
            *pfEaten = TRUE;
            return S_OK;
        }
    }

    // Process character through engine
    guint bs = 0;
    gchar *commit = gtv_engine_process(m_pEngine, ch, &bs);
    if (commit) {
        UpdateCompositionUtf8(this, pic, commit);
        EndComposition(pic, TRUE);
        NotifyWordFromCommit(commit);
        g_free(commit);

        // If engine kept leftover buffer, start fresh composition
        UpdateEngineBuffer(this, pic, m_pEngine, FALSE);
    } else {
        // Update active composition
        UpdateEngineBuffer(this, pic, m_pEngine, TRUE);
    }

    /* Restart the suggestion story (cancels anything stale). */
    gtv_suggest_on_key(this, pic);

    *pfEaten = TRUE;
    return S_OK;
}
