#ifndef GTV_INTERNAL_H
#define GTV_INTERNAL_H
#include "engine.h"
#include "verdir.h"
#include <string.h>
#include <stdio.h>

#define DIAC_NONE 0
#define DIAC_BREVE 1
#define DIAC_CIRCUMFLEX 2
#define DIAC_HORN 3
#define DIAC_STROKE 4

#define TONE_NONE 0
#define TONE_SAC 1
#define TONE_HUYEN 2
#define TONE_HOI 3
#define TONE_NGA 4
#define TONE_NANG 5

typedef struct {
    gunichar bare; // 'a','e','i','o','u','y','d'
    int diacritic;
    int tone;
    gboolean is_upper;
} CharInfo;

gchar* reverse_key(gunichar bare, int dia, int tone, gboolean upper);
void register_char(gunichar ch, gunichar bare, int dia, int tone, gboolean upper);
void init_charset(void);
CharInfo* get_info(gunichar c);
gboolean lookup_char(gunichar bare, int dia, int tone, gboolean upper, gunichar *out);
gboolean is_vowel(gunichar c);
gunichar to_lower_g(gunichar c);
gint get_tone(gunichar c);
gint get_diac(gunichar c);
gunichar bare_lower(gunichar c);
int find_tone_position(GArray *word, gboolean modern);
void remove_all_tones(GArray *word);
gboolean apply_tone_at(GArray *word, int pos, int tone);
gboolean is_valid_coda(const char *coda, int coda_len);
gboolean is_valid_onset(const gunichar *ucs, glong first_v);
gboolean spell_word_valid(const char *utf8);

typedef struct {
    gboolean is_valid;
    char onset[32];
    char nucleus[32];
    char coda[32];
    int tone;
    char tone_name[32];
    char diac_name[64];
} GtvSyllableAnalysis;

gboolean gtv_analyze_syllable(const char *utf8, GtvSyllableAnalysis *out);
void add_candidate_unique(GPtrArray *out, const char *cand);
void auto_promote_diphthong(GArray *buf);
gboolean telex_transform(GArray *buf, gunichar key, gboolean modern);
gboolean vni_transform(GArray *buf, gunichar key, gboolean modern);
GArray* gstring_to_ucs4(GString *s);
void ucs4_to_gstring(GArray *arr, GString *s);
gchar* expand_macro(const char *word);
gchar* expand_emoji(const char *word);
gchar* expand_word(const char *word);
GPtrArray* get_emoji_suggestions(const char *prefix);
void gtv_tables_reload(void);
/* Runtime macro/emoji table management (Settings data UI). Counts/getters
 * read the merged table; set/remove edit memory; save writes the whole
 * table to the user file. Pointers from get() are borrowed. */
guint gtv_table_count(gboolean emoji);
gboolean gtv_table_get(gboolean emoji, guint i, const gchar **key, const gchar **value);
gboolean gtv_table_set(gboolean emoji, const gchar *key, const gchar *value);
gboolean gtv_table_remove(gboolean emoji, const gchar *key);
gchar *gtv_table_user_path(gboolean emoji);
gboolean gtv_table_save(gboolean emoji, GError **error);

gchar *gtv_fold_accents(const gchar *text);
gchar *gtv_data_path(const gchar *name);
/* Bundled + GitHub-updated dictionary (engine/dict.c). */
gchar *gtv_dict_user_path(void);
void gtv_dict_invalidate(void);
gboolean gtv_dict_contains(const gchar *word);
void gtv_dict_complete(const gchar *prefix, GPtrArray *out, guint max);
void gtv_dict_correct(const gchar *word, GPtrArray *out, guint max);
/* File-backed learned vocabulary (engine/learn.c), shared by all platforms. */
#define GTV_LEARNED_WORDS_MAX 10000
#define GTV_LEARNED_FIXES_MAX 2000
gchar *gtv_word_key(const gchar *word);
gchar *gtv_learned_path(const gchar *filename);
GHashTable *gtv_words_table_new(void);
GHashTable *gtv_fixes_table_new(void);
void gtv_words_parse(GHashTable *set, const gchar *contents);
void gtv_fixes_parse(GHashTable *map, const gchar *contents);
gboolean gtv_words_load(GHashTable *set, const gchar *user_path);
gboolean gtv_fixes_load(GHashTable *map, const gchar *user_path);
gboolean gtv_words_save(GHashTable *set, const gchar *path);
gboolean gtv_fixes_save(GHashTable *map, const gchar *path);
gboolean gtv_words_learn(GHashTable *set, const gchar *word);
gboolean gtv_fixes_learn(GHashTable *map, const gchar *bad_word, const gchar *good_word);
gchar *gtv_fixes_lookup(GHashTable *map, const gchar *word);
void gtv_learned_completions(GHashTable *set, const gchar *prefix, GPtrArray *out, guint max);
gboolean gtv_apply_key(GArray *buf, gunichar key, GtvMode mode, gboolean modern);
void gtv_compose(GArray *buf, gunichar key, GtvMode mode, gboolean modern);

GPtrArray *gtv_suggest_combined(const GtvConfig *config, const gchar *context, const gchar *preedit, gboolean bad);
void gtv_suggest_combined_async(const GtvConfig *config, const gchar *context, const gchar *preedit, gboolean bad,
                               GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
GPtrArray *gtv_suggest_combined_finish(GAsyncResult *res, GError **error);
#endif
