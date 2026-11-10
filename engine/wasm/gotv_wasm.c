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
