#include "glib.h"
#include "internal.h"

void gtv_init(void) {
    static int initialized = 0;
    if (!initialized) {
        init_charset();
        initialized = 1;
    }
}

gchar *gtv_transform(const gchar *input, GtvMode mode, gboolean modern) {
    gtv_init();
    if (!input) return NULL;
    GArray *buf = g_array_new(FALSE, FALSE, sizeof(gunichar));
    for (const gchar *p = input; *p; p = g_utf8_next_char(p)) {
        gunichar key = g_utf8_get_char(p);
        gtv_compose(buf, key, mode, modern);
    }
    gchar *result = buf->len ? g_ucs4_to_utf8((gunichar *)buf->data, buf->len, NULL, NULL, NULL) : g_strdup("");
    g_array_unref(buf);
    return result;
}

// Exported C functions for WebAssembly
__attribute__((export_name("gtv_wasm_init")))
void gtv_wasm_init(void) {
    gtv_init();
}

__attribute__((export_name("gtv_wasm_malloc")))
void *gtv_wasm_malloc(int size) {
    return g_malloc(size);
}

__attribute__((export_name("gtv_wasm_free")))
void gtv_wasm_free(void *ptr) {
    g_free(ptr);
}

/* Transform full string: takes UTF-8 input string, returns pointer to static result buffer */
static char transform_result_buf[4096];

__attribute__((export_name("gtv_wasm_transform")))
const char *gtv_wasm_transform(const char *input, int mode, int modern) {
    gtv_init();
    if (!input) return "";
    gchar *res = gtv_transform(input, (GtvMode)mode, (gboolean)modern);
    if (!res) return "";
    
    // Copy into static return buffer
    int i = 0;
    while (res[i] && i < (int)sizeof(transform_result_buf) - 1) {
        transform_result_buf[i] = res[i];
        i++;
    }
    transform_result_buf[i] = '\0';
    g_free(res);
    return transform_result_buf;
}

__attribute__((export_name("gtv_wasm_compose_key")))
const char *gtv_wasm_compose_key(const char *word, gunichar key, int mode, int modern) {
    gtv_init();
    GArray *buf = g_array_new(FALSE, FALSE, sizeof(gunichar));
    if (word && *word) {
        glong len = 0;
        gunichar *ucs = g_utf8_to_ucs4(word, -1, NULL, &len, NULL);
        if (ucs) {
            g_array_append_vals(buf, ucs, len);
            g_free(ucs);
        }
    }
    gtv_compose(buf, key, (GtvMode)mode, (gboolean)modern);
    gchar *res = buf->len ? g_ucs4_to_utf8((gunichar *)buf->data, buf->len, NULL, NULL, NULL) : g_strdup("");
    g_array_unref(buf);

    int i = 0;
    while (res[i] && i < (int)sizeof(transform_result_buf) - 1) {
        transform_result_buf[i] = res[i];
        i++;
    }
    transform_result_buf[i] = '\0';
    g_free(res);
    return transform_result_buf;
}

static char syllable_json_buf[1024];

static void buf_append(char *dest, int *pos, int max_len, const char *src) {
    if (!src) return;
    while (*src && *pos < max_len - 1) {
        dest[(*pos)++] = *src++;
    }
    dest[*pos] = '\0';
}

static void buf_append_int(char *dest, int *pos, int max_len, int val) {
    char tmp[16];
    int ti = 0;
    if (val == 0) tmp[ti++] = '0';
    while (val > 0) {
        tmp[ti++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (ti > 0 && *pos < max_len - 1) {
        dest[(*pos)++] = tmp[--ti];
    }
    dest[*pos] = '\0';
}

__attribute__((export_name("gtv_wasm_analyze_syllable")))
const char *gtv_wasm_analyze_syllable(const char *word) {
    gtv_init();
    if (!word || !*word) return "{\"valid\":false}";
    GtvSyllableAnalysis a;
    if (!gtv_analyze_syllable(word, &a) || !a.is_valid) {
        return "{\"valid\":false}";
    }

    int p = 0;
    syllable_json_buf[0] = '\0';
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), "{\"valid\":true,\"word\":\"");
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), word);
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), "\",\"onset\":\"");
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), a.onset);
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), "\",\"nucleus\":\"");
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), a.nucleus);
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), "\",\"coda\":\"");
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), a.coda);
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), "\",\"tone\":");
    buf_append_int(syllable_json_buf, &p, sizeof(syllable_json_buf), a.tone);
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), ",\"toneName\":\"");
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), a.tone_name);
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), "\",\"diacName\":\"");
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), a.diac_name);
    buf_append(syllable_json_buf, &p, sizeof(syllable_json_buf), "\"}");

    return syllable_json_buf;
}
