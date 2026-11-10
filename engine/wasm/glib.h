#ifndef GLIB_H_WASM
#define GLIB_H_WASM

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define G_BEGIN_DECLS
#define G_END_DECLS

typedef char gchar;
typedef unsigned char guchar;
typedef int gint;
typedef unsigned int guint;
typedef long glong;
typedef unsigned long gulong;
typedef int gboolean;
typedef uint32_t gunichar;
typedef void* gpointer;
typedef const void* gconstpointer;
typedef size_t gsize;

typedef struct _GError GError;
struct _GError {
    guint domain;
    gint code;
    gchar *message;
};

typedef struct _GString GString;
struct _GString {
    gchar *str;
    gsize len;
    gsize allocated_len;
};

typedef struct _GPtrArray GPtrArray;
struct _GPtrArray {
    gpointer *pdata;
    guint len;
};
void g_ptr_array_add(GPtrArray *array, gpointer data);
typedef struct _GCancellable GCancellable;
typedef void (*GAsyncReadyCallback)(void *source_object, void *res, void *user_data);
typedef struct _GAsyncResult GAsyncResult;

#define TRUE 1
#define FALSE 0
#define NULL ((void*)0)

#define G_N_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))
#define GUINT_TO_POINTER(u) ((gpointer)(uintptr_t)(u))
#define GPOINTER_TO_UINT(p) ((guint)(uintptr_t)(p))

void *g_malloc(size_t size);
void *g_malloc0(size_t size);
void g_free(void *ptr);

#define g_new(type, n) ((type*)g_malloc(sizeof(type)*(n)))
#define g_new0(type, n) ((type*)g_malloc0(sizeof(type)*(n)))

gunichar* g_utf8_to_ucs4(const gchar *str, glong len, glong *items_read, glong *items_written, GError **error);
gchar* g_ucs4_to_utf8(const gunichar *str, glong len, glong *items_read, glong *items_written, GError **error);
gunichar g_utf8_get_char(const gchar *p);
const gchar* g_utf8_next_char(const gchar *p);
gboolean g_utf8_validate(const gchar *str, gsize max_len, const gchar **end);

gunichar g_unichar_tolower(gunichar c);
gunichar g_unichar_toupper(gunichar c);
gboolean g_unichar_isupper(gunichar c);
gboolean g_unichar_isalpha(gunichar c);
gboolean g_unichar_isspace(gunichar c);
gboolean g_unichar_ispunct(gunichar c);
gboolean g_unichar_isalnum(gunichar c);
gboolean g_unichar_ismark(gunichar c);
gboolean g_unichar_isdefined(gunichar c);
#define g_ascii_isdigit(c) ((c) >= '0' && (c) <= '9')

typedef struct {
    gunichar *data;
    guint len;
    guint cap;
} GArray;

#define g_array_index(a, t, i) (((t*)((a)->data))[(i)])
GArray *g_array_new(gboolean zero_terminated, gboolean clear_, guint element_size);
GArray *g_array_sized_new(gboolean zero_terminated, gboolean clear_, guint element_size, guint reserved_size);
void g_array_append_vals(GArray *array, gconstpointer data, guint len);
#define g_array_append_val(a, v) do { gunichar _v = (v); g_array_append_vals((a), &_v, 1); } while (0)
void g_array_set_size(GArray *array, guint length);
void g_array_unref(GArray *array);

typedef struct GHashTable GHashTable;
typedef guint (*GHashFunc)(gconstpointer key);
typedef gboolean (*GEqualFunc)(gconstpointer a, gconstpointer b);
typedef void (*GDestroyNotify)(gpointer data);

GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc key_equal_func);
GHashTable *g_hash_table_new_full(GHashFunc hash_func, GEqualFunc key_equal_func, GDestroyNotify key_destroy_func, GDestroyNotify value_destroy_func);
void g_hash_table_insert(GHashTable *hash_table, gpointer key, gpointer value);
gpointer g_hash_table_lookup(GHashTable *hash_table, gconstpointer key);
guint g_direct_hash(gconstpointer v);
gboolean g_direct_equal(gconstpointer v1, gconstpointer v2);
guint g_str_hash(gconstpointer v);
gboolean g_str_equal(gconstpointer v1, gconstpointer v2);

gchar *g_strdup(const gchar *str);
gchar *g_strdup_printf(const gchar *format, ...);

/* Dummy once init for wasm single thread */
#define g_once_init_enter(location) (*(location) == 0)
#define g_once_init_leave(location, result) (*(location) = (result))

#endif
