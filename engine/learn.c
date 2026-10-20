#include "internal.h"
#include <glib/gstdio.h>

/* File-backed learned vocabulary shared by all platforms (IBus engine and
 * the Windows hook). Two files in the user config dir:
 *   learned-words.txt       one valid word per line (taught by accepting
 *                           dictionary suggestions),
 *   learned-corrections.txt bad=good pairs (taught by accepting corrections).
 * When the user file does not exist yet, the shipped seed of the same name
 * (resolved with gtv_data_path) is used, so the data works offline from the
 * first run. The first save copies everything into the user file, after
 * which the user file is authoritative and freely editable. Format: UTF-8,
 * '#' comments and blank lines skipped, CRLF tolerated, first entry wins. */

gchar *gtv_word_key(const gchar *word){
    if(!word || !g_utf8_validate(word,-1,NULL))return NULL;
    gchar *lower=g_utf8_strdown(word,-1),*key=g_utf8_normalize(lower,-1,G_NORMALIZE_NFC);g_free(lower);
    if(!*key || g_utf8_strlen(key,-1)>64){g_free(key);return NULL;}
    gunichar first=g_utf8_get_char(key);
    if(!g_unichar_isalpha(first)){g_free(key);return NULL;}
    for(const gchar *p=key;*p;p=g_utf8_next_char(p)){
        gunichar c=g_utf8_get_char(p);
        if(!g_unichar_isalpha(c) && !g_unichar_isdigit(c) && c!='_'){g_free(key);return NULL;}
    }
    return key;
}

gchar *gtv_learned_path(const gchar *filename){
    return gtv_user_path(g_get_user_config_dir(), filename);
}

GHashTable *gtv_words_table_new(void){
    return g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
}
GHashTable *gtv_fixes_table_new(void){
    return g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
}

void gtv_words_parse(GHashTable *set,const gchar *contents){
    if(!set || !contents)return;
    gchar **lines=g_strsplit_set(contents,"\r\n",GTV_LEARNED_WORDS_MAX+1);
    for(guint i=0;lines[i] && i<GTV_LEARNED_WORDS_MAX;i++){
        gchar *line=g_strstrip(lines[i]);
        if(!*line || *line=='#')continue;
        gchar *key=gtv_word_key(line);
        if(key && g_hash_table_size(set)<GTV_LEARNED_WORDS_MAX)g_hash_table_add(set,key);
        else g_free(key);
    }
    g_strfreev(lines);
}
void gtv_fixes_parse(GHashTable *map,const gchar *contents){
    if(!map || !contents)return;
    gchar **lines=g_strsplit_set(contents,"\r\n",GTV_LEARNED_FIXES_MAX+1);
    for(guint i=0;lines[i] && i<GTV_LEARNED_FIXES_MAX;i++){
        gchar *line=g_strstrip(lines[i]);
        if(!*line || *line=='#')continue;
        gchar *sep=strchr(line,'=');
        if(!sep)continue;
        *sep='\0';
        gchar *bad=gtv_word_key(g_strstrip(line)),*good=gtv_word_key(g_strstrip(sep+1));
        if(bad && good && strcmp(bad,good) && g_hash_table_size(map)<GTV_LEARNED_FIXES_MAX)
            g_hash_table_replace(map,bad,good);
        else{g_free(bad);g_free(good);}
    }
    g_strfreev(lines);
}

static gboolean load_any(GHashTable *set,const gchar *user_path,const gchar *seed_name,gboolean fixes){
    gboolean have_user=g_file_test(user_path,G_FILE_TEST_EXISTS);
    gchar *contents=NULL;
    if(have_user && g_file_get_contents(user_path,&contents,NULL,NULL)){
        if(fixes)gtv_fixes_parse(set,contents);else gtv_words_parse(set,contents);
        g_free(contents);
        return TRUE;
    }
    if(!have_user){
        gchar *seed=gtv_data_path(seed_name);
        gboolean ok=g_file_get_contents(seed,&contents,NULL,NULL);
        if(ok){
            if(fixes)gtv_fixes_parse(set,contents);else gtv_words_parse(set,contents);
            g_free(contents);
        }
        g_free(seed);
        return ok;
    }
    return TRUE;
}
gboolean gtv_words_load(GHashTable *set,const gchar *user_path){
    if(!set || !user_path)return FALSE;
    return load_any(set,user_path,"learned-words.txt",FALSE);
}
gboolean gtv_fixes_load(GHashTable *map,const gchar *user_path){
    if(!map || !user_path)return FALSE;
    return load_any(map,user_path,"learned-corrections.txt",TRUE);
}

static gboolean save_any(GHashTable *table,const gchar *path,gboolean fixes){
    if(!table || !path)return FALSE;
    GString *contents=g_string_new("");
    GHashTableIter iter;gpointer k,v;
    g_hash_table_iter_init(&iter,table);
    while(g_hash_table_iter_next(&iter,&k,&v)){
        g_string_append(contents,k);
        if(fixes){g_string_append_c(contents,'=');g_string_append(contents,v);}
        g_string_append_c(contents,'\n');
    }
    gchar *directory=g_path_get_dirname(path);
    gboolean ok=g_mkdir_with_parents(directory,0700)==0
        && g_file_set_contents(path,contents->str,contents->len,NULL);
    g_free(directory);
    g_string_free(contents,TRUE);
    return ok;
}
gboolean gtv_words_save(GHashTable *set,const gchar *path){
    return save_any(set,path,FALSE);
}
gboolean gtv_fixes_save(GHashTable *map,const gchar *path){
    return save_any(map,path,TRUE);
}

gboolean gtv_words_learn(GHashTable *set,const gchar *word){
    if(!set)return FALSE;
    gchar *key=gtv_word_key(word);
    if(!key)return FALSE;
    if(g_hash_table_size(set)>=GTV_LEARNED_WORDS_MAX || g_hash_table_contains(set,key)){g_free(key);return FALSE;}
    g_hash_table_insert(set,key,NULL);
    return TRUE;
}
gboolean gtv_fixes_learn(GHashTable *map,const gchar *bad_word,const gchar *good_word){
    if(!map)return FALSE;
    gchar *bad=gtv_word_key(bad_word),*good=gtv_word_key(good_word);
    if(!bad || !good || !strcmp(bad,good)){g_free(bad);g_free(good);return FALSE;}
    if(g_hash_table_size(map)>=GTV_LEARNED_FIXES_MAX || g_hash_table_contains(map,bad)){g_free(bad);g_free(good);return FALSE;}
    g_hash_table_insert(map,bad,good);
    return TRUE;
}
gchar *gtv_fixes_lookup(GHashTable *map,const gchar *word){
    if(!map)return NULL;
    gchar *key=gtv_word_key(word);
    const gchar *found=key ? g_hash_table_lookup(map,key) : NULL;
    gchar *out=found ? g_strdup(found) : NULL;g_free(key);return out;
}

static gint compare_strings(gconstpointer a,gconstpointer b){
    return strcmp(*(const gchar *const *)a,*(const gchar *const *)b);
}
/* Offline completions from learned vocabulary: exact prefix first, then
 * accent-folded prefix (thong matches thông). Sorted and capped. */
void gtv_learned_completions(GHashTable *set,const gchar *prefix,GPtrArray *out,guint max){
    if(!set || !prefix || !*prefix || !out || max==0)return;
    gchar *lower=gtv_word_key(prefix);
    if(!lower)return;
    gchar *folded_prefix=gtv_fold_accents(lower);
    GPtrArray *exact=g_ptr_array_new(),*folded=g_ptr_array_new();
    GHashTableIter iter;gpointer k;
    g_hash_table_iter_init(&iter,set);
    while(g_hash_table_iter_next(&iter,&k,NULL)){
        const gchar *w=k;
        if(!g_strcmp0(w,lower))continue;
        if(g_str_has_prefix(w,lower))g_ptr_array_add(exact,(gpointer)w);
        else{
            gchar *folded_w=gtv_fold_accents(w);
            gboolean match=folded_w && g_str_has_prefix(folded_w,folded_prefix);
            g_free(folded_w);
            if(match)g_ptr_array_add(folded,(gpointer)w);
        }
    }
    g_ptr_array_sort(exact,compare_strings);g_ptr_array_sort(folded,compare_strings);
    for(guint i=0;i<exact->len && out->len<max;i++)add_candidate_unique(out,exact->pdata[i]);
    for(guint i=0;i<folded->len && out->len<max;i++)add_candidate_unique(out,folded->pdata[i]);
    g_ptr_array_unref(exact);g_ptr_array_unref(folded);g_free(folded_prefix);g_free(lower);
}
