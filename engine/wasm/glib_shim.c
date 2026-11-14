#include "glib.h"

/* Simple bump allocator with free-list recycling or standard memory pool */
static uint8_t heap[1024 * 512]; // 512KB static heap in WASM linear memory
static size_t heap_offset = 16;

void *g_malloc(size_t size) {
    // 8-byte align
    size = (size + 7) & ~7;
    if (heap_offset + size > sizeof(heap)) {
        return NULL;
    }
    void *ptr = &heap[heap_offset];
    heap_offset += size;
    return ptr;
}

void *g_malloc0(size_t size) {
    void *ptr = g_malloc(size);
    if (ptr) {
        uint8_t *b = (uint8_t*)ptr;
        for (size_t i = 0; i < size; i++) b[i] = 0;
    }
    return ptr;
}

void g_free(void *ptr) {
    // In our WASM typing lifecycle, allocations are tiny and ephemeral
    (void)ptr;
}

void g_ptr_array_add(GPtrArray *array, gpointer data) {
    (void)array; (void)data;
}

/* String helpers */
size_t strlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

size_t wcslen(const int *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

size_t strlen_wasm(const char *s) {
    return strlen(s);
}

void *memset(void *dest, int val, size_t len) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    while (len--) *d++ = (unsigned char)val;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t len) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (len--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t len) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if ((const unsigned char *)dest < s) {
        while (len--) *d++ = *s++;
    } else {
        d += len;
        s += len;
        while (len--) *--d = *--s;
    }
    return dest;
}

gchar *g_strdup(const gchar *str) {
    if (!str) return NULL;
    size_t len = strlen_wasm(str);
    gchar *res = (gchar*)g_malloc(len + 1);
    if (!res) return NULL;
    for (size_t i = 0; i < len; i++) res[i] = str[i];
    res[len] = '\0';
    return res;
}

static void int_to_str(char *buf, int val) {
    char tmp[16];
    int i = 0;
    int is_neg = 0;
    if (val < 0) {
        is_neg = 1;
        val = -val;
    }
    if (val == 0) tmp[i++] = '0';
    while (val > 0) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    if (is_neg) tmp[i++] = '-';
    int out = 0;
    while (i > 0) {
        buf[out++] = tmp[--i];
    }
    buf[out] = '\0';
}

gchar *g_strdup_printf(const gchar *format, ...) {
    // Specifically for reverse_key: "%u:%d:%d:%d"
    va_list ap;
    va_start(ap, format);
    char buf[64];
    buf[0] = '\0';
    int bi = 0;

    for (int fi = 0; format[fi]; fi++) {
        if (format[fi] == '%' && format[fi+1]) {
            fi++;
            int v = va_arg(ap, int);
            char tmp[16];
            int_to_str(tmp, v);
            for (int k = 0; tmp[k]; k++) buf[bi++] = tmp[k];
        } else {
            buf[bi++] = format[fi];
        }
    }
    buf[bi] = '\0';
    va_end(ap);
    return g_strdup(buf);
}

/* UTF-8 <-> UCS-4 conversion */
gunichar g_utf8_get_char(const gchar *p) {
    const unsigned char *s = (const unsigned char*)p;
    if (s[0] < 0x80) return s[0];
    if ((s[0] & 0xE0) == 0xC0) {
        return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((s[0] & 0xF0) == 0xE0) {
        return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((s[0] & 0xF8) == 0xF0) {
        return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    return s[0];
}

const gchar* g_utf8_next_char(const gchar *p) {
    const unsigned char *s = (const unsigned char*)p;
    if (*s < 0x80) return p + 1;
    if ((*s & 0xE0) == 0xC0) return p + 2;
    if ((*s & 0xF0) == 0xE0) return p + 3;
    if ((*s & 0xF8) == 0xF0) return p + 4;
    return p + 1;
}

gboolean g_utf8_validate(const gchar *str, gsize max_len, const gchar **end) {
    (void)max_len; (void)end;
    return str != NULL;
}

gunichar* g_utf8_to_ucs4(const gchar *str, glong len, glong *items_read, glong *items_written, GError **error) {
    (void)items_read; (void)error;
    if (!str) return NULL;
    size_t count = 0;
    const gchar *p = str;
    while (*p && (len < 0 || (p - str) < len)) {
        count++;
        p = g_utf8_next_char(p);
    }
    gunichar *res = g_new(gunichar, count + 1);
    if (!res) return NULL;
    p = str;
    for (size_t i = 0; i < count; i++) {
        res[i] = g_utf8_get_char(p);
        p = g_utf8_next_char(p);
    }
    res[count] = 0;
    if (items_written) *items_written = (glong)count;
    return res;
}

gchar* g_ucs4_to_utf8(const gunichar *str, glong len, glong *items_read, glong *items_written, GError **error) {
    (void)items_read; (void)error;
    if (!str) return NULL;
    if (len < 0) {
        len = 0;
        while (str[len]) len++;
    }
    // worst case 4 bytes per char + 1 null terminator
    gchar *buf = g_new(gchar, len * 4 + 1);
    size_t out = 0;
    for (glong i = 0; i < len; i++) {
        gunichar c = str[i];
        if (c < 0x80) {
            buf[out++] = (gchar)c;
        } else if (c < 0x800) {
            buf[out++] = (gchar)(0xC0 | (c >> 6));
            buf[out++] = (gchar)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            buf[out++] = (gchar)(0xE0 | (c >> 12));
            buf[out++] = (gchar)(0x80 | ((c >> 6) & 0x3F));
            buf[out++] = (gchar)(0x80 | (c & 0x3F));
        } else {
            buf[out++] = (gchar)(0xF0 | (c >> 18));
            buf[out++] = (gchar)(0x80 | ((c >> 12) & 0x3F));
            buf[out++] = (gchar)(0x80 | ((c >> 6) & 0x3F));
            buf[out++] = (gchar)(0x80 | (c & 0x3F));
        }
    }
    buf[out] = '\0';
    if (items_written) *items_written = (glong)out;
    return buf;
}

/* Unicode Case Mapping */
gunichar g_unichar_tolower(gunichar c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    if (c == 0x0110) return 0x0111; // Đ -> đ
    // Handle Vietnamese uppercase vowels if needed
    // The C core register_char registers both upper and lower,
    // to_lower_g is mostly called on ASCII keys or via bare_lower.
    return c;
}

gunichar g_unichar_toupper(gunichar c) {
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    if (c == 0x0111) return 0x0110; // đ -> Đ
    return c;
}

gboolean g_unichar_isupper(gunichar c) {
    if (c >= 'A' && c <= 'Z') return TRUE;
    if (c == 0x0110) return TRUE;
    return FALSE;
}

gboolean g_unichar_isalpha(gunichar c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return TRUE;
    if (c == 0x0110 || c == 0x0111) return TRUE;
    if (c >= 0x00C0 && c <= 0x1EF9) return TRUE;
    return FALSE;
}

/* GArray implementation */
GArray *g_array_new(gboolean zero_terminated, gboolean clear_, guint element_size) {
    (void)zero_terminated; (void)clear_; (void)element_size;
    GArray *arr = g_new0(GArray, 1);
    arr->cap = 16;
    arr->len = 0;
    arr->data = g_new0(gunichar, arr->cap);
    return arr;
}

GArray *g_array_sized_new(gboolean zero_terminated, gboolean clear_, guint element_size, guint reserved_size) {
    (void)zero_terminated; (void)clear_; (void)element_size;
    GArray *arr = g_new0(GArray, 1);
    arr->cap = reserved_size > 16 ? reserved_size : 16;
    arr->len = 0;
    arr->data = g_new0(gunichar, arr->cap);
    return arr;
}

void g_array_append_vals(GArray *array, gconstpointer data, guint len) {
    if (array->len + len > array->cap) {
        guint new_cap = (array->cap + len) * 2;
        gunichar *new_data = g_new0(gunichar, new_cap);
        for (guint i = 0; i < array->len; i++) new_data[i] = array->data[i];
        array->data = new_data;
        array->cap = new_cap;
    }
    const gunichar *src = (const gunichar*)data;
    for (guint i = 0; i < len; i++) {
        array->data[array->len++] = src[i];
    }
}

void g_array_set_size(GArray *array, guint length) {
    if (length <= array->len) {
        array->len = length;
    } else {
        while (array->len < length) {
            gunichar zero = 0;
            g_array_append_vals(array, &zero, 1);
        }
    }
}

void g_array_unref(GArray *array) {
    // In our static heap, no-op
    (void)array;
}

/* GHashTable implementation (Open addressing, table size 1024) */
#define HASH_TABLE_SIZE 1024
typedef struct {
    gpointer key;
    gpointer value;
    int used;
} HashEntry;

struct GHashTable {
    GHashFunc hash_func;
    GEqualFunc equal_func;
    HashEntry entries[HASH_TABLE_SIZE];
};

GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc key_equal_func) {
    return g_hash_table_new_full(hash_func, key_equal_func, NULL, NULL);
}

GHashTable *g_hash_table_new_full(GHashFunc hash_func, GEqualFunc key_equal_func, GDestroyNotify key_destroy_func, GDestroyNotify value_destroy_func) {
    (void)key_destroy_func; (void)value_destroy_func;
    GHashTable *t = g_new0(GHashTable, 1);
    t->hash_func = hash_func;
    t->equal_func = key_equal_func;
    return t;
}

void g_hash_table_insert(GHashTable *t, gpointer key, gpointer value) {
    guint h = t->hash_func ? t->hash_func(key) : (guint)(uintptr_t)key;
    guint idx = h % HASH_TABLE_SIZE;
    for (guint i = 0; i < HASH_TABLE_SIZE; i++) {
        guint slot = (idx + i) % HASH_TABLE_SIZE;
        if (!t->entries[slot].used || (t->equal_func ? t->equal_func(t->entries[slot].key, key) : t->entries[slot].key == key)) {
            t->entries[slot].key = key;
            t->entries[slot].value = value;
            t->entries[slot].used = 1;
            return;
        }
    }
}

gpointer g_hash_table_lookup(GHashTable *t, gconstpointer key) {
    guint h = t->hash_func ? t->hash_func(key) : (guint)(uintptr_t)key;
    guint idx = h % HASH_TABLE_SIZE;
    for (guint i = 0; i < HASH_TABLE_SIZE; i++) {
        guint slot = (idx + i) % HASH_TABLE_SIZE;
        if (!t->entries[slot].used) return NULL;
        if (t->equal_func ? t->equal_func(t->entries[slot].key, key) : t->entries[slot].key == key) {
            return t->entries[slot].value;
        }
    }
    return NULL;
}

guint g_direct_hash(gconstpointer v) { return (guint)(uintptr_t)v; }
gboolean g_direct_equal(gconstpointer v1, gconstpointer v2) { return v1 == v2; }

guint g_str_hash(gconstpointer v) {
    const char *s = (const char*)v;
    guint h = 5381;
    while (*s) h = ((h << 5) + h) + (guint)(*s++);
    return h;
}

gboolean g_str_equal(gconstpointer v1, gconstpointer v2) {
    const char *s1 = (const char*)v1;
    const char *s2 = (const char*)v2;
    if (s1 == s2) return TRUE;
    if (!s1 || !s2) return FALSE;
    while (*s1 && *s2) {
        if (*s1 != *s2) return FALSE;
        s1++; s2++;
    }
    return *s1 == *s2;
}
