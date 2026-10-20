/* Bundled + GitHub-updated multi-language / keyword dictionaries.
 *
 * Two layers feed word suggestions, both fully offline:
 *   1. data/dict-<id>.txt seeds shipped with the app (vi, tsql, c, csharp,
 *      en), refreshed from GitHub releases (or raw main fallback) into the
 *      user data dir;
 *   2. the user's own learned-words.txt (personal picks win).
 *
 * File format (both copies): UTF-8, '#' comments and blank lines ignored,
 * one word per line, plus an optional `# dict-tag: <release>` header that
 * marks which release published the file. Every entry is re-validated
 * with gtv_word_key() on load, so a corrupt download can never poison
 * suggestions: it is simply ignored.
 */
#include "internal.h"
#include <gio/gio.h>
#include <glib/gstdio.h>

#define GTV_DICT_NAME "dict-vi.txt"
#define GTV_DICT_TAG_PREFIX "# dict-tag:"

static const GtvDictInfo dict_catalog[] = {
    { "vi",     "dict-vi.txt",     "Tiếng Việt" },
    { "tsql",   "dict-tsql.txt",   "T-SQL (SQL Server)" },
    { "c",      "dict-c.txt",      "C / C++" },
    { "csharp", "dict-csharp.txt", "C# (.NET)" },
    { "en",     "dict-en.txt",     "Tiếng Anh (English)" },
};

typedef struct {
    gchar *word;   /* NFC lowercase display form */
    gchar *folded; /* accent-folded for prefix/edit matching */
} DictEntry;

typedef struct {
    GPtrArray *entries; /* DictEntry*, sorted by folded */
    gchar *path;        /* combined signature / path of loaded files */
    gchar *tag;         /* dict-tag header ("" when absent) */
    gint64 mtime;
    goffset size;
} DictStore;

static GMutex dict_lock;
static DictStore *dict_cache = NULL;
static gchar active_langs[128] = "vi";

guint gtv_dict_catalog_count(void) {
    return (guint)G_N_ELEMENTS(dict_catalog);
}

const GtvDictInfo *gtv_dict_catalog_get(guint index) {
    if (index >= G_N_ELEMENTS(dict_catalog)) return NULL;
    return &dict_catalog[index];
}

const GtvDictInfo *gtv_dict_catalog_find(const gchar *id) {
    if (!id || !*id) return NULL;
    if (!g_ascii_strcasecmp(id, "t-sql") || !g_ascii_strcasecmp(id, "sql"))
        id = "tsql";
    else if (!g_ascii_strcasecmp(id, "c#") || !g_ascii_strcasecmp(id, "cs"))
        id = "csharp";
    else if (!g_ascii_strcasecmp(id, "c++") || !g_ascii_strcasecmp(id, "cpp"))
        id = "c";
    for (guint i = 0; i < G_N_ELEMENTS(dict_catalog); i++) {
        if (!g_ascii_strcasecmp(id, dict_catalog[i].id))
            return &dict_catalog[i];
    }
    return NULL;
}

static void normalize_langs(const gchar *langs, gchar *out, gsize out_size) {
    gboolean enabled[G_N_ELEMENTS(dict_catalog)] = { FALSE };
    gboolean any = FALSE;
    if (langs && *langs) {
        gchar **tokens = g_strsplit_set(langs, ",; \t\r\n", -1);
        for (guint t = 0; tokens && tokens[t]; t++) {
            gchar *tok = g_strstrip(tokens[t]);
            if (!*tok) continue;
            const GtvDictInfo *info = gtv_dict_catalog_find(tok);
            if (!info) continue;
            for (guint i = 0; i < G_N_ELEMENTS(dict_catalog); i++) {
                if (info == &dict_catalog[i]) {
                    enabled[i] = TRUE;
                    any = TRUE;
                }
            }
        }
        g_strfreev(tokens);
    }
    if (!any) enabled[0] = TRUE; /* default to "vi" */
    GString *buf = g_string_new("");
    for (guint i = 0; i < G_N_ELEMENTS(dict_catalog); i++) {
        if (!enabled[i]) continue;
        if (buf->len > 0) g_string_append_c(buf, ',');
        g_string_append(buf, dict_catalog[i].id);
    }
    g_strlcpy(out, buf->str, out_size);
    g_string_free(buf, TRUE);
}

gboolean gtv_dict_lang_enabled(const gchar *langs, const gchar *id) {
    const GtvDictInfo *target = gtv_dict_catalog_find(id);
    if (!target) return FALSE;
    gchar norm[128];
    normalize_langs((langs && *langs) ? langs : active_langs, norm, sizeof(norm));
    gchar **tokens = g_strsplit(norm, ",", -1);
    gboolean found = FALSE;
    for (guint i = 0; tokens && tokens[i]; i++) {
        if (!g_strcmp0(tokens[i], target->id)) {
            found = TRUE;
            break;
        }
    }
    g_strfreev(tokens);
    return found;
}

gchar *gtv_dict_lang_toggle(const gchar *langs, const gchar *id, gboolean enable) {
    const GtvDictInfo *target = gtv_dict_catalog_find(id);
    gchar norm[128];
    normalize_langs((langs && *langs) ? langs : active_langs, norm, sizeof(norm));
    if (!target) return g_strdup(norm);
    GString *buf = g_string_new("");
    for (guint i = 0; i < G_N_ELEMENTS(dict_catalog); i++) {
        const gchar *cid = dict_catalog[i].id;
        gboolean on = (!g_strcmp0(cid, target->id))
            ? enable
            : gtv_dict_lang_enabled(norm, cid);
        if (!on) continue;
        if (buf->len > 0) g_string_append_c(buf, ',');
        g_string_append(buf, cid);
    }
    if (buf->len == 0) g_string_assign(buf, "vi");
    return g_string_free(buf, FALSE);
}

void gtv_dict_set_langs(const gchar *langs) {
    gchar norm[128];
    normalize_langs(langs, norm, sizeof(norm));
    g_mutex_lock(&dict_lock);
    if (strcmp(active_langs, norm) != 0) {
        g_strlcpy(active_langs, norm, sizeof(active_langs));
        if (dict_cache) {
            if (dict_cache->entries) g_ptr_array_unref(dict_cache->entries);
            g_free(dict_cache->path);
            g_free(dict_cache->tag);
            g_free(dict_cache);
            dict_cache = NULL;
        }
    }
    g_mutex_unlock(&dict_lock);
}

const gchar *gtv_dict_get_langs(void) {
    return active_langs;
}

static void dict_entry_free(gpointer p) {
    DictEntry *e = p;
    if (!e) return;
    g_free(e->word);
    g_free(e->folded);
    g_free(e);
}

static void dict_store_free(DictStore *ds) {
    if (!ds) return;
    if (ds->entries) g_ptr_array_unref(ds->entries);
    g_free(ds->path);
    g_free(ds->tag);
    g_free(ds);
}

static gint entry_cmp_folded(gconstpointer a, gconstpointer b) {
    const DictEntry *ea = *(const DictEntry * const *)a;
    const DictEntry *eb = *(const DictEntry * const *)b;
    int c = strcmp(ea->folded, eb->folded);
    if (c != 0) return c;
    return strcmp(ea->word, eb->word);
}

static void dict_parse_into(const gchar *path, DictStore *ds) {
    gchar *contents = NULL;
    if (!path || !g_file_get_contents(path, &contents, NULL, NULL)) return;
    gchar **lines = g_strsplit_set(contents, "\r\n", -1);
    g_free(contents);
    for (guint i = 0; lines[i]; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (!*line) continue;
        if (*line == '#') {
            if (g_str_has_prefix(line, GTV_DICT_TAG_PREFIX)) {
                gchar *tag = g_strstrip((gchar *)line + strlen(GTV_DICT_TAG_PREFIX));
                if (*tag && (!ds->tag || !*ds->tag || gtv_version_compare(tag, ds->tag) > 0)) {
                    g_free(ds->tag);
                    ds->tag = g_strdup(tag);
                }
            }
            continue;
        }
        gchar *key = gtv_word_key(line);
        if (!key) continue;
        DictEntry *e = g_new0(DictEntry, 1);
        e->word = key;
        e->folded = gtv_fold_accents(key);
        if (!e->folded) {
            dict_entry_free(e);
            continue;
        }
        g_ptr_array_add(ds->entries, e);
    }
    g_strfreev(lines);
}

static void dict_finalize_entries(DictStore *ds) {
    if (!ds || !ds->entries) return;
    g_ptr_array_sort(ds->entries, entry_cmp_folded);
    for (guint i = 1; i < ds->entries->len; ) {
        DictEntry *prev = ds->entries->pdata[i - 1];
        DictEntry *cur = ds->entries->pdata[i];
        if (!strcmp(prev->word, cur->word))
            g_ptr_array_remove_index(ds->entries, i);
        else
            i++;
    }
}

/* Parse one dictionary file. Returns entries + tag (both possibly empty).
 * Corrupt lines are skipped, never fatal. */
static DictStore *dict_parse_file(const gchar *path) {
    DictStore *ds = g_new0(DictStore, 1);
    ds->entries = g_ptr_array_new_with_free_func(dict_entry_free);
    ds->path = g_strdup(path ? path : "");
    ds->tag = g_strdup("");
    dict_parse_into(path, ds);
    dict_finalize_entries(ds);
    return ds;
}

static gchar *dict_pack_user_path(const gchar *filename) {
    return gtv_user_path(g_get_user_data_dir(), filename);
}

gchar *gtv_dict_user_path(void) {
    return dict_pack_user_path(GTV_DICT_NAME);
}

/* Load one pack (downloaded copy wins when non-empty; otherwise shipped seed). */
static DictStore *dict_load_one_pack(const GtvDictInfo *info) {
    gchar *user = dict_pack_user_path(info->filename);
    DictStore *ds = NULL;
    if (g_file_test(user, G_FILE_TEST_EXISTS)) {
        ds = dict_parse_file(user);
        if (ds->entries->len == 0) {
            dict_store_free(ds);
            ds = NULL;
        }
    }
    g_free(user);
    if (!ds) {
        gchar *seed = gtv_data_path(info->filename);
        ds = dict_parse_file(seed);
        g_free(seed);
    }
    return ds;
}

/* Load and merge all enabled packs in active_langs. */
static DictStore *dict_load_locked(void) {
    DictStore *merged = g_new0(DictStore, 1);
    merged->entries = g_ptr_array_new_with_free_func(dict_entry_free);
    merged->path = g_strdup("");
    merged->tag = g_strdup("");
    for (guint i = 0; i < G_N_ELEMENTS(dict_catalog); i++) {
        if (!gtv_dict_lang_enabled(active_langs, dict_catalog[i].id))
            continue;
        DictStore *one = dict_load_one_pack(&dict_catalog[i]);
        if (!one) continue;
        if (one->tag && *one->tag &&
            (!*merged->tag || gtv_version_compare(one->tag, merged->tag) > 0)) {
            g_free(merged->tag);
            merged->tag = g_strdup(one->tag);
        }
        for (guint j = 0; j < one->entries->len; j++) {
            DictEntry *src = one->entries->pdata[j];
            DictEntry *copy = g_new0(DictEntry, 1);
            copy->word = g_strdup(src->word);
            copy->folded = g_strdup(src->folded);
            g_ptr_array_add(merged->entries, copy);
        }
        dict_store_free(one);
    }
    dict_finalize_entries(merged);
    return merged;
}

static gchar *dict_build_signature_locked(gboolean *out_any_stat) {
    GString *sig = g_string_new(active_langs);
    gboolean any_stat = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(dict_catalog); i++) {
        if (!gtv_dict_lang_enabled(active_langs, dict_catalog[i].id))
            continue;
        gchar *user = dict_pack_user_path(dict_catalog[i].filename);
        gchar *seed = NULL;
        const gchar *path = NULL;
        if (g_file_test(user, G_FILE_TEST_EXISTS)) {
            path = user;
        } else {
            seed = gtv_data_path(dict_catalog[i].filename);
            path = seed;
        }
        GStatBuf st;
        if (path && g_stat(path, &st) == 0) {
            any_stat = TRUE;
            g_string_append_printf(sig, "|%s:%" G_GINT64_FORMAT ":%" G_GOFFSET_FORMAT,
                path, (gint64)st.st_mtime, (goffset)st.st_size);
        } else {
            g_string_append_printf(sig, "|%s:missing", dict_catalog[i].id);
        }
        g_free(user);
        g_free(seed);
    }
    if (out_any_stat) *out_any_stat = any_stat;
    return g_string_free(sig, FALSE);
}

/* Process-wide cached table; reloads when the backing files or active
 * language packs change. Returned pointer is borrowed. */
const DictStore *gtv_dict_get(void) {
    g_mutex_lock(&dict_lock);
    gboolean have_stat = FALSE;
    gchar *sig = dict_build_signature_locked(&have_stat);
    if (!dict_cache || !have_stat || !dict_cache->path || strcmp(dict_cache->path, sig) != 0) {
        dict_store_free(dict_cache);
        dict_cache = NULL;
        if (have_stat) {
            dict_cache = dict_load_locked();
            g_free(dict_cache->path);
            dict_cache->path = g_strdup(sig);
        }
    }
    g_free(sig);
    const DictStore *out = dict_cache;
    g_mutex_unlock(&dict_lock);
    return out;
}

void gtv_dict_invalidate(void) {
    g_mutex_lock(&dict_lock);
    dict_store_free(dict_cache);
    dict_cache = NULL;
    g_mutex_unlock(&dict_lock);
}

guint gtv_dict_count(void) {
    const DictStore *ds = gtv_dict_get();
    return ds && ds->entries ? ds->entries->len : 0;
}

const gchar *gtv_dict_tag(void) {
    const DictStore *ds = gtv_dict_get();
    return (ds && ds->tag && *ds->tag) ? ds->tag : "";
}

guint gtv_dict_pack_count(const gchar *id) {
    const GtvDictInfo *info = gtv_dict_catalog_find(id);
    if (!info) return 0;
    DictStore *ds = dict_load_one_pack(info);
    guint n = ds && ds->entries ? ds->entries->len : 0;
    dict_store_free(ds);
    return n;
}

gchar *gtv_dict_pack_tag(const gchar *id) {
    const GtvDictInfo *info = gtv_dict_catalog_find(id);
    if (!info) return g_strdup("");
    DictStore *ds = dict_load_one_pack(info);
    gchar *tag = g_strdup((ds && ds->tag) ? ds->tag : "");
    dict_store_free(ds);
    return tag;
}

/* Completions: accent-exact prefix first, then accent-folded prefix
 * (thong matches thông), each capped; mirrors gtv_learned_completions. */
void gtv_dict_complete(const gchar *prefix, GPtrArray *out, guint max) {
    if (!prefix || !*prefix || !out || max == 0) return;
    gchar *lower = gtv_word_key(prefix);
    if (!lower) return;
    gchar *folded_prefix = gtv_fold_accents(lower);
    if (!folded_prefix) {
        g_free(lower);
        return;
    }
    const DictStore *ds = gtv_dict_get();
    if (ds && ds->entries) {
        for (guint i = 0; i < ds->entries->len && out->len < max; i++) {
            DictEntry *e = ds->entries->pdata[i];
            if (!strcmp(e->word, lower)) continue;
            if (g_str_has_prefix(e->word, lower))
                add_candidate_unique(out, e->word);
        }
        for (guint i = 0; i < ds->entries->len && out->len < max; i++) {
            DictEntry *e = ds->entries->pdata[i];
            if (!strcmp(e->word, lower)) continue;
            if (!g_str_has_prefix(e->word, lower) &&
                g_str_has_prefix(e->folded, folded_prefix))
                add_candidate_unique(out, e->word);
        }
    }
    g_free(folded_prefix);
    g_free(lower);
}

/* Byte Levenshtein with early exit past cap. Folded forms are lowercase
 * ASCII-ish, so byte distance tracks character distance. */
static guint edit_distance_cap(const gchar *a, const gchar *b, guint cap) {
    gsize na = strlen(a), nb = strlen(b);
    if (na > nb + cap || nb > na + cap) return cap + 1;
    /* Two rows; lengths are short words (<= 64 by gtv_word_key). */
    guint prev[65], cur[65];
    if (na > 64 || nb > 64) return cap + 1;
    for (gsize j = 0; j <= nb; j++) prev[j] = (guint)j;
    for (gsize i = 1; i <= na; i++) {
        cur[0] = (guint)i;
        guint row_min = cur[0];
        for (gsize j = 1; j <= nb; j++) {
            guint cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            guint del = prev[j] + 1, ins = cur[j - 1] + 1, sub = prev[j - 1] + cost;
            guint m = del < ins ? del : ins;
            m = m < sub ? m : sub;
            cur[j] = m;
            if (m < row_min) row_min = m;
        }
        if (row_min > cap) return cap + 1;
        memcpy(prev, cur, (nb + 1) * sizeof(guint));
    }
    return prev[nb];
}

typedef struct {
    const gchar *word;
    guint dist;
    gsize len_diff;
} CorrectCand;

static gint correct_cmp(gconstpointer a, gconstpointer b) {
    const CorrectCand *ca = a, *cb = b;
    if (ca->dist != cb->dist) return ca->dist < cb->dist ? -1 : 1;
    if (ca->len_diff != cb->len_diff) return ca->len_diff < cb->len_diff ? -1 : 1;
    return strcmp(ca->word, cb->word);
}

/* Corrections for a misspelled word: folded edit distance <= 2, ranked by
 * distance, then length closeness, then alphabetically. Accent variants
 * (distance 0, different word: hoa -> hòa) count as candidates. */
void gtv_dict_correct(const gchar *word, GPtrArray *out, guint max) {
    if (!word || !*word || !out || max == 0) return;
    gchar *lower = gtv_word_key(word);
    if (!lower) return;
    gchar *folded = gtv_fold_accents(lower);
    if (!folded) {
        g_free(lower);
        return;
    }
    gboolean has_non_ascii = FALSE;
    for (const gchar *p = lower; *p; p++) {
        if ((guchar)*p >= 0x80) {
            has_non_ascii = TRUE;
            break;
        }
    }
    gsize want_len = strlen(folded);
    const DictStore *ds = gtv_dict_get();
    if (ds && ds->entries) {
        GArray *cands = g_array_new(FALSE, FALSE, sizeof(CorrectCand));
        gboolean has_dist_zero = FALSE;
        for (guint i = 0; i < ds->entries->len; i++) {
            DictEntry *e = ds->entries->pdata[i];
            if (!strcmp(e->word, lower)) continue;
            gsize elen = strlen(e->folded);
            gsize diff = elen > want_len ? elen - want_len : want_len - elen;
            if (diff > 2) continue;
            guint d = edit_distance_cap(folded, e->folded, 2);
            if (d <= 2) {
                if (d == 0) has_dist_zero = TRUE;
                CorrectCand c = { e->word, d, diff };
                g_array_append_val(cands, c);
            }
        }
        g_array_sort(cands, correct_cmp);
        for (guint i = 0; i < cands->len && out->len < max; i++) {
            CorrectCand c = g_array_index(cands, CorrectCand, i);
            if (has_non_ascii && has_dist_zero && c.dist > 0) continue;
            add_candidate_unique(out, c.word);
        }
        g_array_unref(cands);
    }
    g_free(folded);
    g_free(lower);
}

static gboolean dict_asset_match_name(const gchar *asset_name, const gchar *tag_version, gpointer user_data) {
    (void)tag_version;
    const gchar *wanted = user_data;
    return wanted && !g_strcmp0(asset_name, wanted);
}

/* Download or refresh one dictionary pack by ID ("vi", "tsql", "c", "csharp", "en").
 * Checks the latest GitHub release asset first, then falls back to raw main
 * on GitHub so newly added packs can be downloaded immediately. */
gboolean gtv_dict_update_pack(const gchar *repo, const gchar *id) {
    const GtvDictInfo *info = gtv_dict_catalog_find(id);
    if (!info) return FALSE;
    const gchar *eff_repo = (repo && *repo) ? repo : GTV_GITHUB_REPO;
    gchar *tag = NULL, *url = NULL;
    GtvUpdateStatus st = gtv_update_check_full(eff_repo, "0.0.0", dict_asset_match_name,
                                               (gpointer)info->filename, &tag, &url);
    if (st != GTV_UPDATE_AVAILABLE || !url) {
        g_free(tag);
        g_free(url);
        tag = NULL;
        url = g_strdup_printf("https://raw.githubusercontent.com/%s/main/data/%s",
                              eff_repo, info->filename);
    }
    gchar *dest = dict_pack_user_path(info->filename);
    gboolean had_user = g_file_test(dest, G_FILE_TEST_EXISTS);
    gchar *tmp = g_strdup_printf("%s.dict-dl", dest);
    gboolean ok = gtv_update_download(url, tmp);
    g_free(tag);
    g_free(url);
    if (!ok) {
        g_free(tmp);
        g_free(dest);
        return FALSE;
    }
    DictStore *dl = dict_parse_file(tmp);
    gboolean promote = FALSE;
    if (dl->entries->len > 0 && dl->tag && *dl->tag) {
        gchar *cur = gtv_dict_pack_tag(info->id);
        int cmp = (cur && *cur) ? gtv_version_compare(dl->tag, cur) : 1;
        promote = had_user ? (cmp > 0) : (cmp >= 0);
        g_free(cur);
    }
    dict_store_free(dl);
    if (promote) {
        gchar *dir = g_path_get_dirname(dest);
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
        if (g_rename(tmp, dest) == 0) {
            g_free(tmp);
            g_free(dest);
            gtv_dict_invalidate();
            return TRUE;
        }
    }
    g_remove(tmp);
    g_free(tmp);
    g_free(dest);
    return FALSE;
}

/* Refresh all enabled dictionary packs from GitHub. */
gboolean gtv_dict_update_check(const gchar *repo) {
    gchar langs[128];
    g_mutex_lock(&dict_lock);
    g_strlcpy(langs, active_langs, sizeof(langs));
    g_mutex_unlock(&dict_lock);
    gboolean any_updated = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(dict_catalog); i++) {
        if (!gtv_dict_lang_enabled(langs, dict_catalog[i].id))
            continue;
        if (gtv_dict_update_pack(repo, dict_catalog[i].id))
            any_updated = TRUE;
    }
    return any_updated;
}
