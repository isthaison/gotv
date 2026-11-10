#include "internal.h"

/* Macros and emojis live in data files, not C arrays (resolved with
 * gtv_data_path: $GTV_DATA_DIR → user config → /usr/share → ./data).
 * Format: key=value per line (split on first '='), '#' comments, blank
 * lines skipped, UTF-8, first entry wins on duplicates. Order is kept for
 * emoji prefix suggestions. Loaded once per process; gtv_tables_reload()
 * forces a reload (tests). */
#define MACRO_FILE "macros.txt"
#define EMOJI_FILE "emojis.txt"
#define TABLE_CAP 2000

typedef struct { gchar *key; gchar *fold; gchar *value; } TableEntry;

static GPtrArray *macro_entries = NULL;
static GPtrArray *emoji_entries = NULL;
static GHashTable *macro_exact = NULL;
static GHashTable *macro_fold = NULL;
static GHashTable *emoji_exact = NULL;
static GHashTable *emoji_fold = NULL;
static gboolean tables_loaded = FALSE;

static void entry_free(gpointer p){
    TableEntry *e = p;
    g_free(e->key); g_free(e->fold); g_free(e->value); g_free(e);
}

static void table_add(GPtrArray *entries, GHashTable *exact, GHashTable *fold,
                      const gchar *key, const gchar *value){
    if(!key || !*key || !value || !*value || entries->len >= TABLE_CAP) return;
    if(g_hash_table_contains(exact, key)) return;
    TableEntry *e = g_new(TableEntry, 1);
    e->key = g_strdup(key);
    e->fold = g_utf8_strdown(key, -1);
    e->value = g_strdup(value);
    g_ptr_array_add(entries, e);
    g_hash_table_insert(exact, e->key, e->value);
    if(!g_hash_table_contains(fold, e->fold))
        g_hash_table_insert(fold, e->fold, e->value);
}

static void table_load_file(GPtrArray *entries, GHashTable *exact, GHashTable *fold,
                            const gchar *path){
    gchar *contents = NULL;
    if(!path || !g_file_get_contents(path, &contents, NULL, NULL)) return;
    gchar **lines = g_strsplit_set(contents, "\r\n", -1);
    for(guint i = 0; lines[i]; i++){
        gchar *line = g_strstrip(lines[i]);
        if(!*line || *line == '#') continue;
        gchar *sep = strchr(line, '=');
        if(!sep) continue;
        *sep = '\0';
        table_add(entries, exact, fold, g_strstrip(line), g_strstrip(sep + 1));
    }
    g_strfreev(lines); g_free(contents);
}

static void tables_init(void){
    macro_entries = g_ptr_array_new_with_free_func(entry_free);
    emoji_entries = g_ptr_array_new_with_free_func(entry_free);
    macro_exact = g_hash_table_new(g_str_hash, g_str_equal);
    macro_fold = g_hash_table_new(g_str_hash, g_str_equal);
    emoji_exact = g_hash_table_new(g_str_hash, g_str_equal);
    emoji_fold = g_hash_table_new(g_str_hash, g_str_equal);
}

static void tables_load(void){
    if(tables_loaded) return;
    tables_loaded = TRUE;
    tables_init();
    gchar *macros = gtv_data_path(MACRO_FILE);
    gchar *emojis = gtv_data_path(EMOJI_FILE);
    table_load_file(macro_entries, macro_exact, macro_fold, macros);
    table_load_file(emoji_entries, emoji_exact, emoji_fold, emojis);
    g_free(macros); g_free(emojis);
}

void gtv_tables_reload(void){
    if(macro_exact) g_hash_table_destroy(macro_exact);
    if(macro_fold) g_hash_table_destroy(macro_fold);
    if(emoji_exact) g_hash_table_destroy(emoji_exact);
    if(emoji_fold) g_hash_table_destroy(emoji_fold);
    macro_exact = macro_fold = emoji_exact = emoji_fold = NULL;
    if(macro_entries) g_ptr_array_unref(macro_entries);
    if(emoji_entries) g_ptr_array_unref(emoji_entries);
    macro_entries = emoji_entries = NULL;
    tables_loaded = FALSE;
    tables_load();
}

/* Trả về chuỗi mới (caller g_free) nếu là macro, hoặc NULL nếu không khớp */
gchar* expand_macro(const char *word){
    if(!word || !*word) return NULL;
    tables_load();
    gpointer v = g_hash_table_lookup(macro_exact, word);
    if(!v){
        gchar *lower = g_utf8_strdown(word, -1);
        v = g_hash_table_lookup(macro_fold, lower);
        g_free(lower);
    }
    return v ? g_strdup((const gchar *)v) : NULL;
}

/* Trả về chuỗi mới (caller g_free) nếu là emoji, hoặc NULL nếu không khớp */
gchar* expand_emoji(const char *word){
    if(!word || !*word) return NULL;
    tables_load();
    gpointer v = g_hash_table_lookup(emoji_exact, word);
    if(!v){
        gchar *lower = g_utf8_strdown(word, -1);
        v = g_hash_table_lookup(emoji_fold, lower);
        g_free(lower);
    }
    return v ? g_strdup((const gchar *)v) : NULL;
}

/* Trả về chuỗi mới (caller g_free) — đã expand macro/emoji, hoặc bản sao nguyên văn */
gchar* expand_word(const char *word){
    if(!word) return g_strdup("");
    gchar *m = expand_macro(word);
    if(m) return m;
    gchar *e = expand_emoji(word);
    if(e) return e;
    return g_strdup(word);
}

static gint compare_emoji_match(gconstpointer a, gconstpointer b, gpointer user_data) {
    const TableEntry *ea = *(const TableEntry **)a;
    const TableEntry *eb = *(const TableEntry **)b;
    const gchar *q = (const gchar *)user_data;
    gsize q_len = strlen(q);

    // Exact matches
    gboolean a_exact = !strcmp(ea->fold, q);
    gboolean b_exact = !strcmp(eb->fold, q);
    if (a_exact != b_exact) return a_exact ? -1 : 1;

    // Colon-normalized exact matches e.g. :smile: for :smile
    gboolean a_colon_exact = (q[0] == ':' && ea->fold[0] == ':' &&
                              !strncmp(ea->fold + 1, q + 1, q_len - 1) &&
                              ea->fold[q_len] == ':' && ea->fold[q_len + 1] == '\0');
    gboolean b_colon_exact = (q[0] == ':' && eb->fold[0] == ':' &&
                              !strncmp(eb->fold + 1, q + 1, q_len - 1) &&
                              eb->fold[q_len] == ':' && eb->fold[q_len + 1] == '\0');
    if (a_colon_exact != b_colon_exact) return a_colon_exact ? -1 : 1;

    // Shorter code lengths win (e.g. :dog: before :dislike:)
    gsize la = strlen(ea->fold);
    gsize lb = strlen(eb->fold);
    if (la != lb) return (la < lb) ? -1 : 1;

    return strcmp(ea->fold, eb->fold);
}

GPtrArray* get_emoji_suggestions(const char *prefix){
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    if(!prefix || !*prefix) return out;
    tables_load();
    gchar *lower = g_utf8_strdown(prefix, -1);

    GPtrArray *matches = g_ptr_array_new();
    for(guint i = 0; i < emoji_entries->len; i++){
        TableEntry *e = emoji_entries->pdata[i];
        if(!strcmp(e->fold, lower) || g_str_has_prefix(e->fold, lower)){
            g_ptr_array_add(matches, e);
        }
    }

    g_ptr_array_sort_with_data(matches, compare_emoji_match, lower);

    for(guint i = 0; i < matches->len && out->len < 5; i++){
        TableEntry *e = g_ptr_array_index(matches, i);
        add_candidate_unique(out, e->value);
    }
    g_ptr_array_free(matches, TRUE);
    g_free(lower);
    return out;
}

/* --- Runtime table management (Settings data UI on every platform). ---
 * Operates on the merged in-memory table; gtv_table_save() writes the whole
 * table back to the USER file (which then shadows the system file, exactly
 * like a hand edit). Callers must gtv_tables_reload() in their own process
 * to pick the file up; other processes (e.g. Windows TSF hosts) reload on
 * restart. Invalid keys (empty, '=', newline) are rejected. */
static void table_pick(gboolean emoji, GPtrArray **entries, GHashTable **exact,
                       GHashTable **fold, const gchar **filename){
    tables_load();
    if(emoji){
        if(entries) *entries = emoji_entries;
        if(exact) *exact = emoji_exact;
        if(fold) *fold = emoji_fold;
        if(filename) *filename = EMOJI_FILE;
    } else {
        if(entries) *entries = macro_entries;
        if(exact) *exact = macro_exact;
        if(fold) *fold = macro_fold;
        if(filename) *filename = MACRO_FILE;
    }
}

static gboolean key_valid(const gchar *key, const gchar *value){
    if(!key || !*key || !value || !*value) return FALSE;
    if(!g_utf8_validate(key, -1, NULL) || !g_utf8_validate(value, -1, NULL)) return FALSE;
    for(const gchar *p = key; *p; p = g_utf8_next_char(p)){
        gunichar c = g_utf8_get_char(p);
        if(c == '=' || c == '\n' || c == '\r' || g_unichar_isspace(c)) return FALSE;
    }
    for(const gchar *p = value; *p; p = g_utf8_next_char(p)){
        gunichar c = g_utf8_get_char(p);
        if(c == '\n' || c == '\r') return FALSE;
    }
    return TRUE;
}

guint gtv_table_count(gboolean emoji){
    GPtrArray *entries = NULL;
    table_pick(emoji, &entries, NULL, NULL, NULL);
    return entries ? entries->len : 0;
}

gboolean gtv_table_get(gboolean emoji, guint i, const gchar **key, const gchar **value){
    GPtrArray *entries = NULL;
    table_pick(emoji, &entries, NULL, NULL, NULL);
    if(!entries || i >= entries->len) return FALSE;
    TableEntry *e = entries->pdata[i];
    if(key) *key = e->key;
    if(value) *value = e->value;
    return TRUE;
}

gboolean gtv_table_set(gboolean emoji, const gchar *key, const gchar *value){
    GPtrArray *entries = NULL;
    GHashTable *exact = NULL, *fold = NULL;
    table_pick(emoji, &entries, &exact, &fold, NULL);
    if(!entries || !key_valid(key, value)) return FALSE;
    TableEntry *found = NULL;
    for(guint i = 0; i < entries->len; i++){
        TableEntry *e = entries->pdata[i];
        if(!strcmp(e->key, key)){ found = e; break; }
    }
    if(found){
        g_hash_table_remove(exact, found->key);
        g_hash_table_remove(fold, found->fold);
        g_free(found->fold);
        g_free(found->value);
        found->fold = g_utf8_strdown(key, -1);
        found->value = g_strdup(value);
        g_hash_table_insert(exact, found->key, found->value);
        if(!g_hash_table_contains(fold, found->fold))
            g_hash_table_insert(fold, found->fold, found->value);
        return TRUE;
    }
    if(entries->len >= TABLE_CAP) return FALSE;
    table_add(entries, exact, fold, key, value);
    return TRUE;
}

gboolean gtv_table_remove(gboolean emoji, const gchar *key){
    GPtrArray *entries = NULL;
    GHashTable *exact = NULL, *fold = NULL;
    table_pick(emoji, &entries, &exact, &fold, NULL);
    if(!entries || !key || !*key) return FALSE;
    for(guint i = 0; i < entries->len; i++){
        TableEntry *e = entries->pdata[i];
        if(!strcmp(e->key, key)){
            g_hash_table_remove(exact, e->key);
            g_hash_table_remove(fold, e->fold);
            g_ptr_array_remove_index(entries, i);
            return TRUE;
        }
    }
    return FALSE;
}

gchar *gtv_table_user_path(gboolean emoji){
    const gchar *filename = NULL;
    table_pick(emoji, NULL, NULL, NULL, &filename);
    return gtv_user_path(g_get_user_config_dir(), filename);
}

gboolean gtv_table_save(gboolean emoji, GError **error){
    GPtrArray *entries = NULL;
    table_pick(emoji, &entries, NULL, NULL, NULL);
    if(!entries) return FALSE;
    gchar *path = gtv_table_user_path(emoji);
    gchar *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    GString *out = g_string_new("# GoTV user table (managed in Settings; edits here win)\n");
    for(guint i = 0; i < entries->len; i++){
        TableEntry *e = entries->pdata[i];
        g_string_append_printf(out, "%s=%s\n", e->key, e->value);
    }
    gboolean ok = g_file_set_contents(path, out->str, out->len, error);
    g_string_free(out, TRUE);
    g_free(path);
    return ok;
}
