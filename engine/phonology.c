#include "internal.h"

/* Select a tone-bearing vowel from the nucleus. Shape priority and vowel-pair
 * policies are data, shared by every key mapping and every typing order. */
int find_tone_position(GArray *word, gboolean modern) {
    if (!word->len) return -1;
    int *positions = g_new(int, word->len);
    guint count = 0;
    for (guint i=0;i<word->len;i++)
        if (is_vowel(g_array_index(word,gunichar,i))) positions[count++] = i;
    if (!count) { g_free(positions); return -1; }
    if (count > 1 && positions[0] == 1) {
        gunichar onset=bare_lower(g_array_index(word,gunichar,0));
        gunichar vowel=bare_lower(g_array_index(word,gunichar,1));
        if ((onset == 'q' && vowel == 'u') || (onset == 'g' && vowel == 'i')) {
            memmove(positions,positions+1,(--count)*sizeof(int));
        }
    }
    static const int shape_priority[] = {0,1,2,1,0};
    int best=0, result=positions[0];
    for (guint i=0;i<count;i++) {
        int priority=shape_priority[get_diac(g_array_index(word,gunichar,positions[i]))];
        if (priority && priority >= best) {best=priority;result=positions[i];}
    }
    if (!best && count > 1) {
        gboolean closed=positions[count-1] < (int)word->len-1;
        guint target=count == 3 ? 1 : 0;
        if (count == 2) {
            gunichar first=bare_lower(g_array_index(word,gunichar,positions[0]));
            gunichar last=bare_lower(g_array_index(word,gunichar,positions[1]));
            static const struct {gunichar first,last; gboolean modern_only;} pairs[] = {
                {'o','a',TRUE},{'o','e',TRUE},{'u','y',TRUE},
                {'i','e',FALSE},{'y','e',FALSE},{'u','o',FALSE},{'o','o',FALSE}
            };
            target=closed ? 1 : 0;
            for (guint i=0;i<G_N_ELEMENTS(pairs);i++)
                if (first == pairs[i].first && last == pairs[i].last && (modern || !pairs[i].modern_only)) target=1;
        }
        result=positions[target];
    }
    g_free(positions);
    return result;
}

void remove_all_tones(GArray *word){
    for(guint i=0;i<word->len;i++){
        gunichar c=g_array_index(word,gunichar,i);
        int tone=get_tone(c);
        if(tone!=TONE_NONE){
            CharInfo *info=get_info(c);
            gunichar nc;
            if(lookup_char(info->bare, info->diacritic, TONE_NONE, info->is_upper, &nc))
                g_array_index(word,gunichar,i)=nc;
        }
    }
}
gboolean apply_tone_at(GArray *word, int pos, int tone){
    if(pos<0||pos>=(int)word->len) return FALSE;
    gunichar c=g_array_index(word,gunichar,pos);
    CharInfo *info=get_info(c);
    if(!info||info->bare=='d') return FALSE;
    gunichar nc;
    if(!lookup_char(info->bare, info->diacritic, tone, info->is_upper, &nc)) return FALSE;
    g_array_index(word,gunichar,pos)=nc;
    return TRUE;
}

gboolean gtv_analyze_syllable(const char *utf8, GtvSyllableAnalysis *out) {
    if (!out) return FALSE;
    memset(out, 0, sizeof(*out));
    if (!utf8 || !*utf8) return FALSE;

    glong len = 0;
    gunichar *ucs = g_utf8_to_ucs4(utf8, -1, NULL, &len, NULL);
    if (!ucs || len <= 0) {
        if (ucs) g_free(ucs);
        return FALSE;
    }

    // Reject non-letters immediately (e.g. colons, punctuation, digits, emojis)
    for (glong i = 0; i < len; i++) {
        gunichar c = ucs[i];
        if (!g_unichar_isalpha(c)) {
            g_free(ucs);
            return FALSE;
        }
    }

    // Find vowels and tone/diacritics
    glong first_v = -1;
    glong last_v = -1;
    int tone = TONE_NONE;
    int diac_mask = 0;

    for (glong i = 0; i < len; i++) {
        gunichar c = ucs[i];
        if (is_vowel(c)) {
            /* u in qu is part of onset when another vowel follows */
            if (i == 1 && bare_lower(ucs[0]) == 'q' && bare_lower(ucs[i]) == 'u' &&
                i + 1 < len && is_vowel(ucs[i + 1])) {
                // skip
            } else {
                if (first_v == -1) first_v = i;
                last_v = i;
            }
        }
        int t = get_tone(c);
        if (t != TONE_NONE) tone = t;
        int d = get_diac(c);
        if (d == DIAC_BREVE) diac_mask |= 1;
        if (d == DIAC_CIRCUMFLEX) diac_mask |= 2;
        if (d == DIAC_HORN) diac_mask |= 4;
        if (d == DIAC_STROKE || c == 0x0110 || c == 0x0111) diac_mask |= 8;
    }

    // If no vowels found: check if it's a valid onset in progress
    if (first_v == -1) {
        if (is_valid_onset(ucs, len)) {
            out->is_valid = TRUE;
            gchar *u_str = g_ucs4_to_utf8(ucs, len, NULL, NULL, NULL);
            if (u_str) {
                strncpy(out->onset, u_str, sizeof(out->onset) - 1);
                g_free(u_str);
            }
            strcpy(out->nucleus, "-");
            strcpy(out->coda, "-");
            strcpy(out->tone_name, "Không dấu (Ngang)");
            strcpy(out->diac_name, (diac_mask & 8) ? "Gạch ngang (đ)" : "Không có");
            g_free(ucs);
            return TRUE;
        }
        g_free(ucs);
        return FALSE;
    }

    // Check onset validity
    if (first_v > 0 && !is_valid_onset(ucs, first_v)) {
        g_free(ucs);
        return FALSE;
    }

    // All characters between first_v and last_v must be vowels
    for (glong i = first_v; i <= last_v; i++) {
        if (!is_vowel(ucs[i])) {
            g_free(ucs);
            return FALSE;
        }
    }

    // Check coda validity
    glong coda_len = len - last_v - 1;
    if (coda_len > 2) {
        g_free(ucs);
        return FALSE;
    }
    char coda_ascii[3] = {0};
    for (glong i = 0; i < coda_len; i++) {
        gunichar c = bare_lower(ucs[last_v + 1 + i]);
        if (c > 127) {
            g_free(ucs);
            return FALSE;
        }
        coda_ascii[i] = (char)c;
    }
    if (!is_valid_coda(coda_ascii, coda_len)) {
        g_free(ucs);
        return FALSE;
    }

    out->is_valid = TRUE;
    out->tone = tone;

    // Build onset string
    if (first_v > 0) {
        gchar *s = g_ucs4_to_utf8(ucs, first_v, NULL, NULL, NULL);
        if (s) { strncpy(out->onset, s, sizeof(out->onset) - 1); g_free(s); }
    } else {
        strcpy(out->onset, "(Trống)");
    }

    // Build nucleus string
    gchar *n_str = g_ucs4_to_utf8(ucs + first_v, last_v - first_v + 1, NULL, NULL, NULL);
    if (n_str) { strncpy(out->nucleus, n_str, sizeof(out->nucleus) - 1); g_free(n_str); }

    // Build coda string
    if (coda_len > 0) {
        gchar *c_str = g_ucs4_to_utf8(ucs + last_v + 1, coda_len, NULL, NULL, NULL);
        if (c_str) { strncpy(out->coda, c_str, sizeof(out->coda) - 1); g_free(c_str); }
    } else {
        strcpy(out->coda, "(Trống)");
    }

    // Tone name
    static const char *tone_names[] = {
        "Không dấu (Ngang)", "Sắc (´)", "Huyền (`)", "Hỏi (?)", "Ngã (~)", "Nặng (.)"
    };
    if (tone >= 0 && tone <= 5) {
        strncpy(out->tone_name, tone_names[tone], sizeof(out->tone_name) - 1);
    } else {
        strcpy(out->tone_name, "Không dấu (Ngang)");
    }

    // Diacritic name
    out->diac_name[0] = '\0';
    if (diac_mask & 1) strcat(out->diac_name, "Trăng (ă), ");
    if (diac_mask & 2) strcat(out->diac_name, "Mũ (â/ê/ô), ");
    if (diac_mask & 4) strcat(out->diac_name, "Móc (ơ/ư), ");
    if (diac_mask & 8) strcat(out->diac_name, "Gạch ngang (đ), ");
    size_t dlen = strlen(out->diac_name);
    if (dlen >= 2 && out->diac_name[dlen - 2] == ',') {
        out->diac_name[dlen - 2] = '\0';
    } else if (dlen == 0) {
        strcpy(out->diac_name, "Không có");
    }

    g_free(ucs);
    return TRUE;
}
