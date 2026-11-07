#include "internal.h"

/* Kiểm tra đúng/sai ngoại tuyến bằng quy tắc âm tiết (âm đầu, vần, phụ âm
 * cuối, thanh điệu), không dùng danh sách từ cứng. Từ vựng do người dùng
 * chọn từ gợi ý được lưu ở learned-words.txt / learned-corrections.txt
 * (xem ibus/engine.c); gợi ý sửa lỗi do từ điển xử lý (engine/dict.c). */

gboolean is_valid_coda(const char *coda, int coda_len){
    if(coda_len == 0) return TRUE;
    if(coda_len == 1){
        char c = coda[0];
        return (c=='c' || c=='m' || c=='n' || c=='p' || c=='t');
    }
    if(coda_len == 2){
        return ((coda[0]=='c' && coda[1]=='h') ||
                (coda[0]=='n' && coda[1]=='g') ||
                (coda[0]=='n' && coda[1]=='h'));
    }
    return FALSE;
}

gboolean is_valid_onset(const gunichar *ucs, glong first_v){
    if(first_v <= 0) return TRUE;
    if(first_v == 1){
        gunichar c = bare_lower(ucs[0]);
        if(c=='b'||c=='c'||c=='d'||c=='g'||c=='h'||c=='k'||c=='l'||c=='m'||
           c=='n'||c=='p'||c=='r'||c=='s'||c=='t'||c=='v'||c=='x'||ucs[0]==0x0111||ucs[0]==0x0110)
            return TRUE;
        return FALSE;
    }
    if(first_v == 2){
        gunichar c0 = bare_lower(ucs[0]), c1 = bare_lower(ucs[1]);
        if(c0=='c'&&c1=='h') return TRUE;
        if(c0=='g'&&c1=='h') return TRUE;
        if(c0=='g'&&c1=='i') return TRUE;
        if(c0=='k'&&c1=='h') return TRUE;
        if(c0=='n'&&c1=='g') return TRUE;
        if(c0=='n'&&c1=='h') return TRUE;
        if(c0=='p'&&c1=='h') return TRUE;
        if(c0=='q'&&c1=='u') return TRUE;
        if(c0=='t'&&c1=='h') return TRUE;
        if(c0=='t'&&c1=='r') return TRUE;
        return FALSE;
    }
    if(first_v == 3){
        gunichar c0 = bare_lower(ucs[0]), c1 = bare_lower(ucs[1]), c2 = bare_lower(ucs[2]);
        if(c0=='n'&&c1=='g'&&c2=='h') return TRUE;
        return FALSE;
    }
    return FALSE;
}

gboolean spell_word_valid(const char *utf8){
    if(!utf8 || !*utf8) return TRUE;
    glong len = 0;
    gunichar *ucs = g_utf8_to_ucs4(utf8, -1, NULL, &len, NULL);
    if(!ucs) return TRUE;
    if(len < 2){ g_free(ucs); return TRUE; }

    for(glong i=0; i<len; i++){
        if(g_ascii_isdigit((gchar)ucs[i]) || !g_unichar_isalpha(ucs[i])){
            g_free(ucs); return TRUE;
        }
    }

    gboolean has_mark = FALSE;
    glong first_v = -1, last_v = -1;
    int v_count = 0;
    int tone = TONE_NONE;

    for(glong i=0; i<len; i++){
        CharInfo *info = get_info(ucs[i]);
        if(info && (info->diacritic != DIAC_NONE || info->tone != TONE_NONE)){
            has_mark = TRUE;
        }
        if(ucs[i] == 0x0111 || ucs[i] == 0x0110){ has_mark = TRUE; }
        if(is_vowel(ucs[i])){
            if(first_v == -1) first_v = i;
            last_v = i;
            v_count++;
        }
        int t = get_tone(ucs[i]);
        if(t != TONE_NONE){
            if(tone != TONE_NONE && tone != t){ g_free(ucs); return FALSE; }
            tone = t;
        }
    }

    if(!has_mark){
        for(glong i=0; i<len; i++){
            gunichar c = bare_lower(ucs[i]);
            if(c == 'w' || c == 'f' || c == 'j' || c == 'z'){
                g_free(ucs); return FALSE;
            }
        }
        if(v_count == 0){
            /* Allow valid multi-consonant onsets while typing (ch, gh, gi,
             * kh, ng, ngh, nh, ph, qu, th, tr); flag vowel-less typos such
             * as khng, thnh, chnh, vng, trng. */
            gboolean ok_onset = is_valid_onset(ucs, len);
            g_free(ucs);
            return ok_onset;
        }
        glong eff_v = first_v;
        if(first_v == 1 && bare_lower(ucs[0]) == 'g' && bare_lower(ucs[1]) == 'i' && len > 2 && is_vowel(ucs[2])){
            eff_v = 2;
        } else if(first_v == 1 && bare_lower(ucs[0]) == 'q' && bare_lower(ucs[1]) == 'u' && len > 2 && is_vowel(ucs[2])){
            eff_v = 2;
        }
        if(!is_valid_onset(ucs, eff_v)){
            g_free(ucs); return FALSE;
        }
        for(glong i=eff_v; i<=last_v; i++){
            if(!is_vowel(ucs[i])){ g_free(ucs); return FALSE; }
        }
        glong u_coda_len = len - 1 - last_v;
        if(u_coda_len > 2){
            g_free(ucs); return FALSE;
        }
        if(u_coda_len == 2){
            char u_coda[4] = {
                (char)bare_lower(ucs[last_v + 1]),
                (char)bare_lower(ucs[last_v + 2]),
                0
            };
            if(!is_valid_coda(u_coda, 2)){
                g_free(ucs); return FALSE;
            }
        }
        g_free(ucs); return TRUE;
    }

    if(v_count == 0){ g_free(ucs); return FALSE; }

    glong eff_first_v = first_v;
    if(first_v == 1 && bare_lower(ucs[0]) == 'g' && bare_lower(ucs[1]) == 'i' && len > 2 && is_vowel(ucs[2])){
        eff_first_v = 2;
    } else if(first_v == 1 && bare_lower(ucs[0]) == 'q' && bare_lower(ucs[1]) == 'u' && len > 2 && is_vowel(ucs[2])){
        eff_first_v = 2;
    }
    for(glong i=eff_first_v; i<=last_v; i++){
        if(!is_vowel(ucs[i])){ g_free(ucs); return FALSE; }
    }

    if(!is_valid_onset(ucs, eff_first_v)){ g_free(ucs); return FALSE; }

    gunichar first_v_bare = bare_lower(ucs[eff_first_v]);
    gunichar first_c = bare_lower(ucs[0]);
    if(eff_first_v == 1){
        if(first_c == 'k'){
            if(first_v_bare != 'i' && first_v_bare != 'e' && first_v_bare != 'y'){
                g_free(ucs); return FALSE;
            }
        } else if(first_c == 'c'){
            if(first_v_bare == 'i' || first_v_bare == 'e' || first_v_bare == 'y'){
                g_free(ucs); return FALSE;
            }
        }
    } else if(eff_first_v == 2){
        gunichar second_c = bare_lower(ucs[1]);
        if(first_c == 'g' && second_c == 'h'){
            if(first_v_bare != 'i' && first_v_bare != 'e'){ g_free(ucs); return FALSE; }
        } else if(first_c == 'g' && second_c != 'i'){
            if(first_v_bare == 'e'){ g_free(ucs); return FALSE; }
        } else if(first_c == 'n' && second_c == 'g'){
            if(first_v_bare == 'i' || first_v_bare == 'e' || first_v_bare == 'y'){ g_free(ucs); return FALSE; }
        }
    } else if(eff_first_v == 3){
        if(first_v_bare != 'i' && first_v_bare != 'e' && first_v_bare != 'y'){ g_free(ucs); return FALSE; }
    }

    glong coda_len = len - 1 - last_v;
    char coda_buf[16] = {0};
    if(coda_len > 0){
        if(coda_len > 2){ g_free(ucs); return FALSE; }
        for(glong i=0; i<coda_len; i++){
            coda_buf[i] = (char)bare_lower(ucs[last_v + 1 + i]);
        }
        if(!is_valid_coda(coda_buf, (int)coda_len)){ g_free(ucs); return FALSE; }
    }

    if(coda_len > 0){
        gboolean is_voiceless_stop = FALSE;
        if(coda_len == 1 && (coda_buf[0]=='c' || coda_buf[0]=='p' || coda_buf[0]=='t')) is_voiceless_stop = TRUE;
        if(coda_len == 2 && coda_buf[0]=='c' && coda_buf[1]=='h') is_voiceless_stop = TRUE;

        if(is_voiceless_stop){
            if(tone == TONE_HUYEN || tone == TONE_HOI || tone == TONE_NGA){
                g_free(ucs); return FALSE;
            }
        }
    }

    if(coda_len == 0){
        gunichar last_v_bare = bare_lower(ucs[last_v]);
        int last_v_diac = get_diac(ucs[last_v]);
        if(last_v_bare == 'a' && (last_v_diac == DIAC_BREVE || last_v_diac == DIAC_CIRCUMFLEX)){
            g_free(ucs); return FALSE;
        }
    }

    g_free(ucs);
    return TRUE;
}

void add_candidate_unique(GPtrArray *out, const char *cand){
    if(!cand || !*cand) return;
    for(guint k=0; k<out->len; k++){
        if(strcmp((char*)out->pdata[k], cand) == 0) return;
    }
    g_ptr_array_add(out, g_strdup(cand));
}

/* Word suggestions come from the offline dictionary (engine/dict.c) plus
 * learned vocabulary (engine/learn.c), orchestrated by engine/suggest.c.
 * Đúng/sai ngoại tuyến vẫn dùng spell_word_valid() ở trên để
 * không chặn đường xử lý phím. */
