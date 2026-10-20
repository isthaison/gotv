/* Offline word suggestions over the bundled/GitHub dictionary plus the
 * user's learned vocabulary. No network, no AI server: everything here
 * runs on worker threads from data already on disk.
 *
 * Two query shapes, both capped at GTV_SUGGEST_MAX:
 *   bad=TRUE   (misspelled word)  -> learned-corrections hit, else
 *                                   dictionary neighbours (edit distance <= 2)
 *   bad=FALSE  (prefix completion) -> learned-words completions first
 *                                   (personal picks win), then dictionary
 *                                   completions to fill up.
 */
#include "internal.h"
#include <gio/gio.h>

#define GTV_SUGGEST_MAX 5

gchar *gtv_fold_accents(const gchar *text){
    gchar *lower=g_utf8_strdown(text,-1),*decomposed=g_utf8_normalize(lower,-1,G_NORMALIZE_NFD);
    GString *folded=g_string_new("");
    for(const gchar *p=decomposed;*p;p=g_utf8_next_char(p)){
        gunichar c=g_utf8_get_char(p);
        if(!g_unichar_ismark(c))g_string_append_unichar(folded,c==0x0111 ? 'd' : c);
    }
    g_free(lower);g_free(decomposed);return g_string_free(folded,FALSE);
}

/* Fresh learned tables per call: the files are tiny (capped), callers stay
 * stateless, and an accepted suggestion is visible on the next keystroke. */
static GHashTable *load_user_words(void){
    GHashTable *set=gtv_words_table_new();
    gchar *path=gtv_learned_path("learned-words.txt");
    if(!gtv_words_load(set,path)){g_hash_table_unref(set);set=NULL;}
    g_free(path);
    return set;
}

static GHashTable *load_user_fixes(void){
    GHashTable *map=gtv_fixes_table_new();
    gchar *path=gtv_learned_path("learned-corrections.txt");
    if(!gtv_fixes_load(map,path)){g_hash_table_unref(map);map=NULL;}
    g_free(path);
    return map;
}

static GPtrArray *suggest(const GtvConfig *config,const gchar *context,const gchar *prefix,gboolean bad){
    GPtrArray *out=g_ptr_array_new_with_free_func(g_free);
    (void)context;
    if(!config || !config->suggest_enabled)return out;
    if(prefix && !g_utf8_validate(prefix,-1,NULL))return out;
    if(!prefix || !*prefix)return out;
    if(config->dicts[0])gtv_dict_set_langs(config->dicts);
    if(bad){
        GHashTable *fixes=load_user_fixes();
        if(fixes){
            gchar *hit=gtv_fixes_lookup(fixes,prefix);
            g_hash_table_unref(fixes);
            if(hit){
                add_candidate_unique(out,hit);
                g_free(hit);
                return out;
            }
        }
        gtv_dict_correct(prefix,out,GTV_SUGGEST_MAX);
    }else{
        GHashTable *words=load_user_words();
        if(words){
            gtv_learned_completions(words,prefix,out,GTV_SUGGEST_MAX);
            g_hash_table_unref(words);
        }
        if(out->len<GTV_SUGGEST_MAX)
            gtv_dict_complete(prefix,out,GTV_SUGGEST_MAX);
    }
    return out;
}

gboolean gtv_dict_available(const GtvConfig *config){
    if(!config || !config->suggest_enabled)return FALSE;
    return gtv_dict_count()>0;
}

GPtrArray *gtv_suggest_combined(const GtvConfig *config,const gchar *context,const gchar *preedit,gboolean bad){return suggest(config,context,preedit,bad);}

typedef struct {
    GtvConfig config;
    gchar *context;
    gchar *preedit;
    gboolean bad;
} SuggestTaskData;

static void suggest_task_data_free(gpointer data) {
    SuggestTaskData *d = data;
    if (!d) return;
    gtv_config_clear(&d->config);
    g_free(d->context);
    g_free(d->preedit);
    g_free(d);
}

static void suggest_worker_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    SuggestTaskData *d = task_data;
    if (g_task_return_error_if_cancelled(task)) return;
    GPtrArray *results = suggest(&d->config, d->context, d->preedit, d->bad);
    if (g_task_return_error_if_cancelled(task)) {
        g_ptr_array_unref(results);
        return;
    }
    g_task_return_pointer(task, results, (GDestroyNotify)g_ptr_array_unref);
}

void gtv_suggest_combined_async(const GtvConfig *config, const gchar *context, const gchar *preedit, gboolean bad,
                               GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
    GTask *task = g_task_new(NULL, cancellable, callback, user_data);
    SuggestTaskData *d = g_new0(SuggestTaskData, 1);
    if (config) {
        d->config = *config;
    }
    d->context = g_strdup(context);
    d->preedit = g_strdup(preedit);
    d->bad = bad;
    g_task_set_task_data(task, d, suggest_task_data_free);
    g_task_run_in_thread(task, suggest_worker_thread);
    g_object_unref(task);
}

GPtrArray *gtv_suggest_combined_finish(GAsyncResult *res, GError **error) {
    g_return_val_if_fail(g_task_is_valid(res, NULL), NULL);
    return g_task_propagate_pointer(G_TASK(res), error);
}
