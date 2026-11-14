/* IBus adapter for the shared C input engine. */
#include <ibus.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include "../engine/internal.h"

#include <stdarg.h>
/* Per-user cache dir: /tmp collides between users (a root-owned file would
 * silence everyone else's logging), so each UID gets its own debug log. */
static char *debug_path = NULL;
static void debug_log(const char *fmt, ...){
    if(!debug_path){
        gchar *dir = gtv_user_path(g_get_user_cache_dir(), NULL);
        g_mkdir_with_parents(dir, 0755);
        debug_path = g_build_filename(dir, "debug.log", NULL);
        g_free(dir);
    }
    FILE *f = fopen(debug_path, "a");
    if(!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

/* IBus Engine definition */
typedef struct _GoTVEngine IBusGoTVEngine;
typedef struct _GoTVEngineClass IBusGoTVEngineClass;
struct _GoTVEngine {
    IBusEngine parent;
    GString *preedit;
    GString *sentence_context;
    gboolean mode_telex;
    /* Named Telex/VNI engines pin their method; the generic engine
     * follows the config file instead. */
    gboolean mode_pinned;
    gboolean modern;
    gboolean spellcheck;
    guint purpose;
    gchar *focus_id;
    gchar *client_id;
    /* Unfinished composition kept across a mouse click away from the
     * input, with the focus it belongs to. focus_in restores it only
     * for the same input; a different input abandons it. */
    gchar *preedit_focus_id;
    gchar **candidates;
    int n_candidates;
    int cand_cursor;
    GtvConfig config;
    GCancellable *suggest_cancellable;
    gchar *pending_query;
    guint suggest_timer;
    GHashTable *learned_words;
    gchar *learned_path;
    GHashTable *learned_fixes;
    gchar *learned_fixes_path;
    gboolean pending_bad;
    gboolean candidates_from_dict;
};
struct _GoTVEngineClass { IBusEngineClass parent; };
G_DEFINE_TYPE(IBusGoTVEngine, ibus_gotv_engine, IBUS_TYPE_ENGINE)

/* Learned vocabulary lives in engine/learn.c now (shared with Windows);
 * the wrappers below only add lazy loading from the user config dir. */
static void load_learned_words(IBusGoTVEngine *e){
    if(e->learned_words)return;
    e->learned_words=gtv_words_table_new();
    if(!e->learned_path)e->learned_path=gtv_learned_path("learned-words.txt");
    gtv_words_load(e->learned_words,e->learned_path);
}
static void load_learned_fixes(IBusGoTVEngine *e){
    if(e->learned_fixes)return;
    e->learned_fixes=gtv_fixes_table_new();
    if(!e->learned_fixes_path)e->learned_fixes_path=gtv_learned_path("learned-corrections.txt");
    gtv_fixes_load(e->learned_fixes,e->learned_fixes_path);
}
static void save_learned_fixes(IBusGoTVEngine *e){
    if(!gtv_fixes_save(e->learned_fixes,e->learned_fixes_path))debug_log("[dictionary] save failed\n");
}
static void learn_fix(IBusGoTVEngine *e,const gchar *typed,const gchar *correction){
    load_learned_fixes(e);
    if(gtv_fixes_learn(e->learned_fixes,typed,correction))save_learned_fixes(e);
}
static gchar *learned_fix_for(IBusGoTVEngine *e,const gchar *word){
    load_learned_fixes(e);
    return gtv_fixes_lookup(e->learned_fixes,word);
}
static void learned_completions(IBusGoTVEngine *e,const gchar *prefix,GPtrArray *out,guint max){
    load_learned_words(e);
    gtv_learned_completions(e->learned_words,prefix,out,max);
}
static G_GNUC_UNUSED gboolean word_valid(IBusGoTVEngine *e,const gchar *word){
    load_learned_words(e);load_learned_fixes(e);gchar *key=gtv_word_key(word);
    gboolean known=key && g_hash_table_contains(e->learned_words,key);
    gboolean mistyped=key && g_hash_table_contains(e->learned_fixes,key);
    /* Explicitly accepted words win over the typo map: accepting a candidate
     * asserts its validity, while the map only records past corrections. */
    if(known) { g_free(key); return TRUE; }
    if(mistyped) { g_free(key); return FALSE; }
    if(key && gtv_dict_contains(key)) { g_free(key); return TRUE; }
    g_free(key);
    return spell_word_valid(word);
}
static void learn_candidate(IBusGoTVEngine *e,const gchar *candidate){
    if(!e->candidates_from_dict)return;
    /* Remember the dictionary-taught correction so the typo is flagged instantly
     * next time, fully offline. Only in correction mode (pending_bad) with
     * single-word candidates, so mere completions are never marked as typos. */
    if(e->pending_bad && e->pending_query && e->preedit && e->preedit->len &&
       !strcmp(e->pending_query,e->preedit->str))
        learn_fix(e,e->pending_query,candidate);
    load_learned_words(e);gboolean changed=FALSE;
    gchar **words=g_strsplit_set(candidate," \t",-1);
    for(guint i=0;words[i] && g_hash_table_size(e->learned_words)<10000;i++){
        gchar *key=gtv_word_key(words[i]);if(key)changed=g_hash_table_add(e->learned_words,key) || changed;
    }
    g_strfreev(words);if(!changed)return;
    GString *contents=g_string_new("");GHashTableIter iter;gpointer key;
    g_hash_table_iter_init(&iter,e->learned_words);
    while(g_hash_table_iter_next(&iter,&key,NULL)){g_string_append(contents,key);g_string_append_c(contents,'\n');}
    gchar *directory=g_path_get_dirname(e->learned_path);g_mkdir_with_parents(directory,0700);g_free(directory);
    if(!g_file_set_contents(e->learned_path,contents->str,contents->len,NULL))debug_log("[dictionary] save failed\n");
    g_string_free(contents,TRUE);
}

static void commit_and_remember(IBusEngine *engine,IBusText *text){
    debug_log("[commit] '%s'\n", text->text);
    ibus_engine_commit_text(engine,text);
}
static void update_context(IBusGoTVEngine *e, const char *committed){
    if(!e->sentence_context || !committed || !*committed) return;
    if(strpbrk(committed, ".!?;\n")){
        g_string_assign(e->sentence_context, "");
        return;
    }
    if(e->sentence_context->len > 0) g_string_append_c(e->sentence_context, ' ');
    g_string_append(e->sentence_context, committed);
    if(e->sentence_context->len > 120){
        gchar **tokens = g_strsplit_set(e->sentence_context->str, " ", -1);
        guint n = g_strv_length(tokens);
        if(n > 3){
            gchar *trimmed = g_strjoin(" ", tokens[n-3], tokens[n-2], tokens[n-1], NULL);
            g_string_assign(e->sentence_context, trimmed);
            g_free(trimmed);
        }
        g_strfreev(tokens);
    }
}

static void clear_candidates(IBusGoTVEngine *e){
    if(e->suggest_timer){g_source_remove(e->suggest_timer);e->suggest_timer=0;}
    if(e->suggest_cancellable){
        g_cancellable_cancel(e->suggest_cancellable);
        g_clear_object(&e->suggest_cancellable);
    }
    g_clear_pointer(&e->pending_query, g_free);
    e->pending_bad=FALSE;
    if(e->candidates){
        for(int i=0;i<e->n_candidates;i++) g_free(e->candidates[i]);
        g_free(e->candidates);
        e->candidates=NULL;
    }
    e->n_candidates=0;
    e->cand_cursor=0;
}
static void hide_suggest(IBusGoTVEngine *e, IBusEngine *engine){
    clear_candidates(e);
    ibus_engine_hide_lookup_table(engine);
}

static void show_candidates(IBusGoTVEngine *e, IBusEngine *engine, GPtrArray *sugs){
    e->candidates_from_dict=FALSE;
    if(e->candidates){
        for(int i=0;i<e->n_candidates;i++) g_free(e->candidates[i]);
        g_free(e->candidates);
        e->candidates=NULL;
    }
    e->n_candidates=0;
    e->cand_cursor=0;
    if(!sugs || sugs->len == 0){
        ibus_engine_hide_lookup_table(engine);
        return;
    }
    e->n_candidates=(int)sugs->len;
    e->cand_cursor=0;
    e->candidates=g_new(gchar*, sugs->len);
    IBusLookupTable *table=ibus_lookup_table_new(5, 0, TRUE, FALSE);
    g_object_ref_sink(table); /* update_lookup_table consumes floating references. */
    for(guint i=0;i<sugs->len;i++){
        e->candidates[i]=g_strdup((char*)sugs->pdata[i]);
        ibus_lookup_table_append_candidate(table, ibus_text_new_from_string(e->candidates[i]));
    }
    ibus_engine_update_lookup_table(engine, table, TRUE);
    g_object_unref(table);
}

static void on_suggest_ready(GObject *source, GAsyncResult *res, gpointer user_data){
    IBusGoTVEngine *e = user_data;
    GError *error = NULL;
    GPtrArray *sugs = gtv_suggest_combined_finish(res, &error);
    if(error){
        g_clear_error(&error);
        g_object_unref(e);
        return;
    }
    if(sugs && g_task_get_cancellable(G_TASK(res))==e->suggest_cancellable && e->pending_query && g_strcmp0(e->preedit->str, e->pending_query) == 0 && e->preedit->len > 0){
        if(sugs->len > 0){
            show_candidates(e, (IBusEngine*)e, sugs);
            e->candidates_from_dict=TRUE;
        }else{
            /* Gợi ý từ bảng từ đã học (chỉ gợi ý hoàn thành từ, không cảnh báo hay sửa lỗi âm tiết) */
            GPtrArray *local=g_ptr_array_new_with_free_func(g_free);
            learned_completions(e, e->pending_query, local, 5);
            show_candidates(e, (IBusEngine*)e, local);
            g_ptr_array_unref(local);
        }
    }
    if(sugs) g_ptr_array_unref(sugs);
    g_object_unref(e);
}

static gboolean request_suggestions(gpointer data){
    IBusGoTVEngine *e=data;e->suggest_timer=0;
    e->pending_bad=FALSE;
    e->suggest_cancellable=g_cancellable_new();
    gtv_suggest_combined_for_app_async(&e->config,e->client_id,e->sentence_context->str,e->preedit->str,FALSE,
        e->suggest_cancellable,on_suggest_ready,g_object_ref(e));
    return G_SOURCE_REMOVE;
}

/* Đẩy preedit không gạch đỏ — giao diện phẳng sạch sẽ tuyệt đối */
static void push_preedit(IBusGoTVEngine *e, IBusEngine *engine, guint cursor, gboolean visible){
    glong plen=g_utf8_strlen(e->preedit->str, -1);
    debug_log("[push_preedit] str='%s' plen=%ld visible=%d\n", e->preedit->str, plen, visible);
    gboolean is_emoji=(e->preedit->str[0]==':' || e->preedit->str[0]==';' || e->preedit->str[0]=='<' || e->preedit->str[0]=='(');
    IBusText *t=ibus_text_new_from_string(e->preedit->str);
    /* Tuyệt đối không gán thuộc tính IBUS_ATTR_UNDERLINE_ERROR hay bất kỳ gạch chân đỏ nào */
    ibus_engine_update_preedit_text_with_mode(engine, t, (guint)plen, visible, IBUS_ENGINE_PREEDIT_COMMIT);
    if(plen > 0 && visible){
        ibus_engine_show_preedit_text(engine);
    } else {
        ibus_engine_hide_preedit_text(engine);
    }

    if(e->suggest_timer){g_source_remove(e->suggest_timer);e->suggest_timer=0;}
    if(e->suggest_cancellable){
        g_cancellable_cancel(e->suggest_cancellable);
        g_clear_object(&e->suggest_cancellable);
    }
    g_clear_pointer(&e->pending_query, g_free);
    e->pending_bad=FALSE;

    if(plen == 0 || !visible){
        hide_suggest(e, engine);
        return;
    }

    if(is_emoji && plen >= 2){
        GPtrArray *sugs = get_emoji_suggestions(e->preedit->str);
        show_candidates(e, engine, sugs);
        if(sugs) g_ptr_array_free(sugs, TRUE);
        return;
    }

    show_candidates(e,engine,NULL);
    if(e->config.suggest_enabled && e->purpose!=IBUS_INPUT_PURPOSE_PASSWORD && e->purpose!=IBUS_INPUT_PURPOSE_PIN){
        e->pending_query=g_strdup(e->preedit->str);
        e->suggest_timer=g_timeout_add_full(G_PRIORITY_DEFAULT,350,request_suggestions,g_object_ref(e),g_object_unref);
    }
}

static void ibus_gotv_engine_reset(IBusGoTVEngine *e){
    if(e->suggest_timer){g_source_remove(e->suggest_timer);e->suggest_timer=0;}
    if(e->suggest_cancellable){
        g_cancellable_cancel(e->suggest_cancellable);
        g_clear_object(&e->suggest_cancellable);
    }
    g_clear_pointer(&e->pending_query, g_free);
    e->pending_bad=FALSE;
    if(e->preedit) g_string_assign(e->preedit,"");
    ibus_engine_hide_preedit_text((IBusEngine*)e);
}
// Tray đổi method khi đang gõ không gây focus_in, nên reload config theo mtime mỗi phím
static void reload_engine_config(IBusGoTVEngine *e){
    gchar *directory = gtv_user_path(g_get_user_config_dir(), NULL);
    gtv_config_clear(&e->config);
    gtv_config_load(&e->config, directory);
    if(!e->mode_pinned)
        e->mode_telex = (e->config.mode == GTV_TELEX);
    e->modern = e->config.modern;
    e->spellcheck = e->config.spellcheck;
    g_free(directory);
}
static time_t cfg_mtime_cache = 0;
static void reload_config_if_changed(IBusGoTVEngine *e){
    gchar *path = gtv_user_path(g_get_user_config_dir(), "config");
    struct stat st;
    if(stat(path, &st) == 0 && st.st_mtime != cfg_mtime_cache){
        cfg_mtime_cache = st.st_mtime;
        reload_engine_config(e);
    }
    g_free(path);
}
static gboolean is_modifier_key(guint keyval){
    switch(keyval){
        case IBUS_Shift_L: case IBUS_Shift_R:
        case IBUS_Control_L: case IBUS_Control_R:
        case IBUS_Alt_L: case IBUS_Alt_R:
        case IBUS_Super_L: case IBUS_Super_R:
        case IBUS_Hyper_L: case IBUS_Hyper_R:
        case IBUS_Meta_L: case IBUS_Meta_R:
        case IBUS_Caps_Lock: case IBUS_Shift_Lock:
        case IBUS_Num_Lock: case IBUS_Scroll_Lock:
        case IBUS_ISO_Level3_Shift: case IBUS_Mode_switch:
            return TRUE;
        default:
            return FALSE;
    }
}
static gboolean ibus_gotv_engine_process_key_event(IBusEngine *engine, guint keyval, guint keycode, guint modifiers){
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    reload_config_if_changed(e);
    debug_log("[key] kv=0x%x ('%c') kc=%u mod=0x%x preedit='%s' purpose=%u\n",
              keyval, (keyval>32 && keyval<127)?(char)keyval:' ', keycode, modifiers, e->preedit->str, e->purpose);
    if(modifiers & IBUS_FORWARD_MASK)return FALSE;
    if(modifiers & IBUS_RELEASE_MASK){
        return FALSE;
    }
    gboolean bare_key=!(modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK | IBUS_MOD4_MASK | IBUS_SHIFT_MASK));
    (void)bare_key;
    /* Let the desktop handle Shift/locks/modifiers without committing a partial word.
     * Character case comes from keyval, already resolved by the keyboard layout. */
    if(is_modifier_key(keyval)) return FALSE;
    // Phím tắt Ctrl/Alt/Super (Ctrl+C/V/X/Z, Ctrl+S...) — commit chữ đang dở rồi nhường cho app
    if(modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK | IBUS_HYPER_MASK | IBUS_META_MASK)){
        if(e->preedit->len>0){
            IBusText *t=ibus_text_new_from_string(e->preedit->str);
            commit_and_remember(engine,t);
            ibus_gotv_engine_reset(e);
            IBusText *empty=ibus_text_new_from_string("");
            ibus_engine_update_preedit_text(engine,empty,0,FALSE);
            hide_suggest(e, engine);
        }
        return FALSE;
    }
    // Trường mật khẩu (Password / PIN): Không can thiệp preedit, nhường phím trực tiếp
    if(e->purpose == IBUS_INPUT_PURPOSE_PASSWORD || e->purpose == IBUS_INPUT_PURPOSE_PIN){
        return FALSE;
    }
    // Esc: hủy preedit nếu đang gõ dở
    if(keyval == IBUS_Escape){
        if(e->preedit->len > 0){
            ibus_gotv_engine_reset(e);
            IBusText *empty = ibus_text_new_from_string("");
            ibus_engine_update_preedit_text(engine, empty, 0, FALSE);
            hide_suggest(e, engine);
            return TRUE;
        }
        hide_suggest(e, engine);
        return FALSE;
    }
    if(keyval == '.' || keyval == '?' || keyval == '!' || keyval == ';' || keyval == '\n'){
        if(e->sentence_context) g_string_assign(e->sentence_context, "");
    }
    // Tab mở rộng macro/emoji nếu khớp; hoặc chọn gợi ý nếu bảng gợi ý đang hiện
    if(keyval==IBUS_Tab || keyval==IBUS_KP_Tab || keyval==IBUS_ISO_Left_Tab){
        if(e->preedit->len>0){
            gchar *expansion = expand_macro(e->preedit->str);
            if(!expansion) expansion = expand_emoji(e->preedit->str);
            if(expansion){
                update_context(e, expansion);
                IBusText *t=ibus_text_new_from_string(expansion);
                g_free(expansion);
                commit_and_remember(engine,t);
                ibus_gotv_engine_reset(e);
                IBusText *empty=ibus_text_new_from_string("");
                ibus_engine_update_preedit_text(engine,empty,0,FALSE);
                hide_suggest(e, engine);
                return TRUE;
            }
        }
        if(e->n_candidates>0 && e->candidates){
            learn_candidate(e,e->candidates[e->cand_cursor]);
            g_string_assign(e->preedit, e->candidates[e->cand_cursor]);
            push_preedit(e, engine, e->preedit->len, TRUE);
            hide_suggest(e, engine);
            return TRUE;
        }
        if(e->preedit->len>0){
            update_context(e, e->preedit->str);
            IBusText *t=ibus_text_new_from_string(e->preedit->str);
            commit_and_remember(engine,t);
            ibus_gotv_engine_reset(e);
            IBusText *empty=ibus_text_new_from_string("");
            ibus_engine_update_preedit_text(engine,empty,0,FALSE);
            hide_suggest(e, engine);
            return FALSE;
        }
        return FALSE;
    }
    // Up/Down di chuyển con trỏ gợi ý, Esc bỏ bảng gợi ý (chọn bằng Tab).
    // Phím số 1..5 không bắt để tránh nuốt số khi gõ (vd: an5, test1...).
    // Enter commit nguyên chữ đang gõ.
    if(e->n_candidates>0 && e->candidates){
        if(keyval==IBUS_Up || keyval==IBUS_Down){
            if(keyval==IBUS_Down) e->cand_cursor=(e->cand_cursor+1)%e->n_candidates;
            else e->cand_cursor=(e->cand_cursor+e->n_candidates-1)%e->n_candidates;
            IBusLookupTable *table=ibus_lookup_table_new(5, (guint)e->cand_cursor, TRUE, FALSE);
            g_object_ref_sink(table);
            for(int i=0;i<e->n_candidates;i++)
                ibus_lookup_table_append_candidate(table, ibus_text_new_from_string(e->candidates[i]));
            ibus_engine_update_lookup_table(engine, table, TRUE);
            g_object_unref(table);
            return TRUE;
        }
        if(keyval==IBUS_Escape){
            hide_suggest(e, engine);
            return TRUE;
        }
    }
    // Backspace
    if(keyval==IBUS_BackSpace){
        if(e->preedit->len>0){
            // remove last utf8 char
            gchar *prev = g_utf8_prev_char(e->preedit->str + e->preedit->len);
            g_string_truncate(e->preedit, prev - e->preedit->str);
            push_preedit(e, engine, e->preedit->len, TRUE);
            return TRUE;
        }
        return FALSE;
    }
    if(keyval==IBUS_space){
        if(e->preedit->len>0){
            /* Tab-only expansion: Space commits literally, never expands. */
            gchar *commit = g_strdup(e->preedit->str);
            update_context(e, commit);
            gchar *with_space = g_strdup_printf("%s ",commit);
            IBusText *t=ibus_text_new_from_string(with_space);
            commit_and_remember(engine,t);
            g_free(commit); g_free(with_space);
            ibus_gotv_engine_reset(e);
            IBusText *empty=ibus_text_new_from_string("");
            ibus_engine_update_preedit_text(engine,empty,0,FALSE);
            hide_suggest(e, engine);
            return TRUE;
        }
        return FALSE;
    }
    // All printable keys use the same C composer as the CLI and tests.
    gunichar key = ibus_keyval_to_unicode(keyval);
    if(key >= 0x20 && g_unichar_isprint(key)){
        GtvConfig config = {.mode = e->mode_telex ? GTV_TELEX : GTV_VNI,
                            .modern = e->modern, .spellcheck = FALSE};
        GtvEngine *composer = gtv_engine_new(&config);
        g_array_unref(composer->buffer);
        composer->buffer = gstring_to_ucs4(e->preedit);
        gchar *commit = gtv_engine_process(composer, key, NULL);
        if(commit){
            update_context(e, commit);
            commit_and_remember(engine, ibus_text_new_from_string(commit));
            g_free(commit);
            ibus_gotv_engine_reset(e);
            ibus_engine_update_preedit_text(engine, ibus_text_new_from_string(""), 0, FALSE);
            hide_suggest(e, engine);
        } else {
            ucs4_to_gstring(composer->buffer, e->preedit);
            push_preedit(e, engine, e->preedit->len, TRUE);
        }
        gtv_engine_free(composer);
        return TRUE;
    }
    // Enter -> commit literally, kể cả khi bảng gợi ý đang hiện.
    // Nhánh này nằm sau khối xử lý 1..5/Up/Down nên Enter không còn bị bảng
    // gợi ý "cướp": gợi ý/emoji/macro chỉ nhận bằng Tab, số 1-5 hoặc click.
    if(keyval==IBUS_Return || keyval==IBUS_KP_Enter){
        if(e->preedit->len>0){
            gchar *word=g_strdup(e->preedit->str);
            update_context(e, word);
            IBusText *t=ibus_text_new_from_string(word);
            g_free(word);
            commit_and_remember(engine,t);
            ibus_gotv_engine_reset(e);
            IBusText *empty=ibus_text_new_from_string("");
            ibus_engine_update_preedit_text(engine,empty,0,FALSE);
            hide_suggest(e, engine);
            return TRUE;
        }
        return FALSE;
    }
    // Other keys: commit preedit literally and forward (Tab-only expansion)
    if(e->preedit->len>0){
        gchar *word=g_strdup(e->preedit->str);
        update_context(e, word);
        IBusText *t=ibus_text_new_from_string(word);
        g_free(word);
        commit_and_remember(engine,t);
        ibus_gotv_engine_reset(e);
        IBusText *empty=ibus_text_new_from_string("");
        ibus_engine_update_preedit_text(engine,empty,0,FALSE);
        hide_suggest(e, engine);
    }
    return FALSE;
}
/* Không đăng ký IBus property: chuyển Telex/VNI chỉ qua tray app, để menu
 * input source của GNOME gọn (không hiện dòng Telex/VNI thừa). */
static void update_mode_property(IBusEngine *engine){
    (void)engine;
}

static void register_engine_properties(IBusEngine *engine){
    (void)engine;
}

static void ibus_gotv_engine_property_activate(IBusEngine *engine, const gchar *prop_name, guint prop_state){
    (void)prop_state;
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    if(g_strcmp0(prop_name, "mode") == 0){
        e->mode_telex = !e->mode_telex;
        gchar *path = gtv_user_path(g_get_user_config_dir(), "config");
        char *dir = g_path_get_dirname(path);
        g_mkdir_with_parents(dir, 0755);
        g_free(dir);
        GKeyFile *kf = g_key_file_new();
        g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
        g_key_file_set_string(kf, "input", "method", e->mode_telex ? "telex" : "vni");
        gsize len = 0;
        gchar *data = g_key_file_to_data(kf, &len, NULL);
        if(data){
            g_file_set_contents(path, data, (gssize)len, NULL);
            g_free(data);
        }
        g_key_file_free(kf);
        g_free(path);
    }
}

static void ibus_gotv_engine_enable(IBusEngine *engine){
    ibus_engine_get_surrounding_text(engine,NULL,NULL,NULL);
    register_engine_properties(engine);
}
static void ibus_gotv_engine_focus_in(IBusEngine *engine){
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    ibus_engine_get_surrounding_text(engine,NULL,NULL,NULL);
    debug_log("[focus_in] engine=%p id='%s' preedit='%s'\n", engine,
              e->focus_id ? e->focus_id : "", e->preedit ? e->preedit->str : "");
    reload_engine_config(e);
    e->purpose=IBUS_INPUT_PURPOSE_FREE_FORM;
    /* A stashed composition stays in the buffer and re-renders on the
     * next keystroke; nothing is redrawn here, so a client that already
     * consumed the text can never see a ghost duplicate. */
    clear_candidates(e);
    update_mode_property(engine);
    // Đồng bộ cache mtime để reload_config_if_changed không load lại ngay
    gchar *path = gtv_user_path(g_get_user_config_dir(), "config");
    struct stat st;
    if(stat(path, &st) == 0) cfg_mtime_cache = st.st_mtime;
    g_free(path);
}
/* Hide the lookup table only when one is actually shown: reset/focus
 * storms must not ping-pong D-Bus hide/show signals with the client. */
static void clear_lookup_quietly(IBusGoTVEngine *e, IBusEngine *engine){
    if(e->n_candidates>0) hide_suggest(e, engine);
    else clear_candidates(e);
}
/* Keep an unfinished composition across an interruption (mouse click,
 * client reset storm) instead of committing it. Only explicit typing
 * keys ever commit, exactly once per keystroke. The stash re-renders
 * on the next keystroke in the same input; moving to another input
 * abandons it. */
static void stash_composition(IBusGoTVEngine *e, IBusEngine *engine, const gchar *id){
    g_free(e->preedit_focus_id);
    e->preedit_focus_id=g_strdup(id ? id : e->focus_id);
    if(e->preedit && e->preedit->len>0)
        ibus_engine_hide_preedit_text(engine);
    clear_lookup_quietly(e, engine);
}
static void focus_out_id(IBusEngine *engine, const gchar *id){
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    debug_log("[focus_out] engine=%p id='%s' preedit='%s'\n", engine,
              id ? id : "", e->preedit ? e->preedit->str : "");
    if(e->sentence_context) g_string_assign(e->sentence_context, "");
    stash_composition(e, engine, id);
}
static void ibus_gotv_engine_focus_out(IBusEngine *engine){
    focus_out_id(engine, NULL);
}
typedef void GtvX11Display;
typedef unsigned long GtvX11Window;
typedef unsigned long GtvX11Atom;
typedef int (*GtvX11ErrorFn)(GtvX11Display *, void *);

static struct {
    GtvX11Display *(*open)(const char *);
    int (*close)(GtvX11Display *);
    GtvX11Window (*root)(GtvX11Display *);
    GtvX11Atom (*atom)(GtvX11Display *, const char *, int);
    int (*prop)(GtvX11Display *, GtvX11Window, GtvX11Atom, long, long, int,
                GtvX11Atom, GtvX11Atom *, int *, unsigned long *, unsigned long *, unsigned char **);
    int (*free_mem)(void *);
    GtvX11ErrorFn (*set_err)(GtvX11ErrorFn);
} x11;

static int x11_ignore_error(GtvX11Display *dpy, void *ev) { (void)dpy; (void)ev; return 0; }

static gboolean x11_ensure_loaded(void) {
    static gsize once = 0;
    if (g_once_init_enter(&once)) {
        void *lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
        const struct { const char *name; void *slot; } syms[] = {
            {"XOpenDisplay", &x11.open}, {"XCloseDisplay", &x11.close},
            {"XDefaultRootWindow", &x11.root}, {"XInternAtom", &x11.atom},
            {"XGetWindowProperty", &x11.prop}, {"XFree", &x11.free_mem},
            {"XSetErrorHandler", &x11.set_err},
        };
        gboolean ok = (lib != NULL);
        for (guint i = 0; ok && i < G_N_ELEMENTS(syms); i++) {
            void *fn = dlsym(lib, syms[i].name);
            if (fn) memcpy(syms[i].slot, &fn, sizeof(fn));
            else ok = FALSE;
        }
        if (!ok) memset(&x11, 0, sizeof(x11));
        g_once_init_leave(&once, 1);
    }
    return x11.open != NULL;
}

static unsigned char *x11_prop(GtvX11Display *dpy, GtvX11Window win, const char *name,
                               int want_fmt, unsigned long words, unsigned long *nitems) {
    GtvX11Atom atom = x11.atom(dpy, name, 1), type = 0;
    int fmt = 0;
    unsigned long n = 0, after = 0;
    unsigned char *buf = NULL;
    if (!atom || x11.prop(dpy, win, atom, 0, (long)words, 0, 0L, &type, &fmt, &n, &after, &buf) != 0 || !buf)
        return NULL;
    if (fmt != want_fmt || n == 0) { x11.free_mem(buf); return NULL; }
    if (nitems) *nitems = n;
    return buf;
}

static gchar *x11_active_app_id(void) {
    if (!g_getenv("DISPLAY") || !x11_ensure_loaded()) return NULL;
    GtvX11Display *dpy = x11.open(NULL);
    if (!dpy) return NULL;

    GtvX11ErrorFn prev_err = x11.set_err(x11_ignore_error);
    gchar *result = NULL;
    unsigned long n = 0;
    unsigned char *p = x11_prop(dpy, x11.root(dpy), "_NET_ACTIVE_WINDOW", 32, 1, NULL);
    GtvX11Window win = p ? *(unsigned long *)p : 0;
    if (p) x11.free_mem(p);

    if (win && (p = x11_prop(dpy, win, "_NET_WM_PID", 32, 1, NULL))) {
        gchar *path = g_strdup_printf("/proc/%lu/comm", *(unsigned long *)p);
        gchar *comm = NULL;
        if (g_file_get_contents(path, &comm, NULL, NULL) && comm) {
            result = gtv_app_dict_normalize_id(comm);
            g_free(comm);
        }
        g_free(path);
        x11.free_mem(p);
    }
    if (!result && win && (p = x11_prop(dpy, win, "WM_CLASS", 8, 256, &n))) {
        const char *inst = (const char *)p;
        size_t len0 = strnlen(inst, n);
        const char *cls = (len0 + 1 < n) ? (inst + len0 + 1) : NULL;
        result = gtv_app_dict_normalize_id((cls && *cls) ? cls : inst);
        x11.free_mem(p);
    }

    x11.set_err(prev_err);
    x11.close(dpy);
    return result;
}

static gboolean is_generic_ibus_client(const gchar *norm) {
    static const char * const generic[] = {
        "xim", "wayland", "fake", "gnome-shell", "qt", "ibus-gtk", "qibus", "gtk-im", "gtk2-im", "gtk3-im", "gtk4-im"
    };
    if (!norm || !*norm) return TRUE;
    for (guint i = 0; i < G_N_ELEMENTS(generic); i++)
        if (g_str_has_prefix(norm, generic[i])) return TRUE;
    return FALSE;
}

static gchar *gnome_focus_app_id(void) {
    const gchar *rdir = g_get_user_runtime_dir();
    if (!rdir) return NULL;
    gchar *path = g_build_filename(rdir, "gotv-focus", NULL);
    gchar *raw = NULL;
    gchar *res = (g_file_get_contents(path, &raw, NULL, NULL) && raw) ? gtv_app_dict_normalize_id(raw) : NULL;
    g_free(raw);
    g_free(path);
    return res;
}

static gchar *resolve_linux_client_id(const gchar *client) {
    gchar *norm = (client && *client) ? gtv_app_dict_normalize_id(client) : NULL;
    if (norm && !is_generic_ibus_client(norm)) return norm;
    gboolean is_xim = (norm && !g_strcmp0(norm, "xim"));
    g_free(norm);
    gchar *gnome_app = gnome_focus_app_id();
    if (gnome_app) return gnome_app;
    return (!g_getenv("WAYLAND_DISPLAY") || is_xim) ? x11_active_app_id() : NULL;
}

static void focus_in_id(IBusEngine *engine, const gchar *id, const gchar *client){
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    g_free(e->focus_id);
    e->focus_id=g_strdup(id);
    g_free(e->client_id);
    e->client_id=resolve_linux_client_id(client);
    debug_log("[focus_in_id] engine=%p id='%s' client='%s' resolved='%s'\n",
              engine, id ? id : "", client ? client : "", e->client_id ? e->client_id : "");
    if(e->preedit && e->preedit->len>0 && e->focus_id && *e->focus_id
       && e->preedit_focus_id && *e->preedit_focus_id
       && g_strcmp0(e->focus_id, e->preedit_focus_id)!=0){
        /* Definitely another input (both ids known and different):
         * abandon what was left behind. Anything less certain keeps
         * the buffer so a double-fired bare focus_in can never wipe
         * live typing. */
        ibus_gotv_engine_reset(e);
        g_clear_pointer(&e->preedit_focus_id, g_free);
        IBusText *empty=ibus_text_new_from_string("");
        ibus_engine_update_preedit_text(engine,empty,0,FALSE);
        ibus_engine_hide_preedit_text(engine);
    }
    ibus_gotv_engine_focus_in(engine);
}
static void ibus_gotv_engine_reset_cb(IBusEngine *engine){
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    debug_log("[reset_cb] engine=%p preedit='%s'\n", engine, e->preedit ? e->preedit->str : "");
    if(e->sentence_context) g_string_assign(e->sentence_context, "");
    if(!e->preedit || !e->preedit->len){
        /* Nothing stashed: stay silent so storms of empty resets can
         * never amplify into D-Bus hide/show ping-pong. */
        clear_lookup_quietly(e, engine);
        return;
    }
    /* A client reset is just another interruption (click, widget churn):
     * stash, never commit. Committing here both duplicates text on click
     * and shreds words when resets land mid-composition. */
    stash_composition(e, engine, e->focus_id);
}
static void ibus_gotv_engine_disable(IBusEngine *engine){
    ibus_gotv_engine_focus_out(engine);
}
static void ibus_gotv_engine_set_capabilities(IBusEngine *engine, guint caps){
    (void)engine; (void)caps;
}
static void ibus_gotv_engine_set_content_type(IBusEngine *engine, guint purpose, guint hints){
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    (void)hints;
    debug_log("[set_content_type] engine=%p purpose=%u hints=%u\n", engine, purpose, hints);
    e->purpose = purpose;
}
static void ibus_gotv_engine_candidate_clicked(IBusEngine *engine, guint index, guint button, guint state){
    (void)button; (void)state;
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    if(index < (guint)e->n_candidates && e->candidates){
        learn_candidate(e,e->candidates[index]);
        g_string_assign(e->preedit, e->candidates[index]);
        gchar *word=expand_emoji(e->preedit->str);
        if(!word) word=g_strdup(e->preedit->str);
        update_context(e, word);
        IBusText *t=ibus_text_new_from_string(word);
        g_free(word);
        commit_and_remember(engine,t);
        ibus_gotv_engine_reset(e);
        IBusText *empty=ibus_text_new_from_string("");
        ibus_engine_update_preedit_text(engine,empty,0,FALSE);
        hide_suggest(e, engine);
    }
}
static void ibus_gotv_engine_finalize(GObject *object){
    IBusGoTVEngine *e=(IBusGoTVEngine*)object;
    clear_candidates(e);
    gtv_config_clear(&e->config);
    g_clear_pointer(&e->learned_words,g_hash_table_unref);g_free(e->learned_path);
    g_clear_pointer(&e->learned_fixes,g_hash_table_unref);g_free(e->learned_fixes_path);
    g_free(e->focus_id);g_free(e->client_id);g_free(e->preedit_focus_id);
    if(e->preedit) g_string_free(e->preedit,TRUE);
    if(e->sentence_context) g_string_free(e->sentence_context,TRUE);
    G_OBJECT_CLASS(ibus_gotv_engine_parent_class)->finalize(object);
}
static void ibus_gotv_engine_class_init(IBusGoTVEngineClass *klass){
    G_OBJECT_CLASS(klass)->finalize=ibus_gotv_engine_finalize;
    IBusEngineClass *ec=IBUS_ENGINE_CLASS(klass);
    ec->process_key_event=ibus_gotv_engine_process_key_event;
    ec->focus_in_id=focus_in_id;
    ec->focus_out_id=focus_out_id;
    ec->focus_in=ibus_gotv_engine_focus_in;
    ec->focus_out=ibus_gotv_engine_focus_out;
    ec->reset=ibus_gotv_engine_reset_cb;
    ec->disable=ibus_gotv_engine_disable;
    ec->enable=ibus_gotv_engine_enable;
    ec->set_capabilities=ibus_gotv_engine_set_capabilities;
    ec->set_content_type=ibus_gotv_engine_set_content_type;
    ec->candidate_clicked=ibus_gotv_engine_candidate_clicked;
    ec->property_activate=ibus_gotv_engine_property_activate;
}
static void ibus_gotv_engine_init(IBusGoTVEngine *e){
    e->preedit=g_string_new("");
    e->sentence_context=g_string_new("");
    e->modern=TRUE;
    e->purpose=IBUS_INPUT_PURPOSE_FREE_FORM;
    reload_engine_config(e);
}
static IBusBus *bus=NULL;
static IBusFactory *factory=NULL;
static gint engine_id = 0;
static IBusEngine* create_engine_cb(IBusFactory *f, const gchar *engine_name, gpointer user_data){
    gchar *path = g_strdup_printf("/org/freedesktop/IBus/Engine/%d", ++engine_id);
    GDBusConnection *conn = ibus_bus_get_connection(bus);
    IBusEngine *engine = g_object_new(ibus_gotv_engine_get_type(), "engine-name",engine_name,"object-path",path,"connection",conn,"has-focus-id",TRUE,NULL);
    g_free(path);
    if(!engine) return NULL;
    g_object_ref_sink(engine);
    IBusGoTVEngine *ue = (IBusGoTVEngine*)engine;
    // Engine Telex/VNI riêng ghim kiểu gõ theo tên, engine chung theo config
    reload_engine_config(ue);
    return engine;
}
/* Crash handling - chỉ dùng glib hệ thống */
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
static void crash_handler(int sig){
    void *bt[32]; int n = backtrace(bt, 32);
    gchar *dir = gtv_user_path(g_get_user_cache_dir(), NULL);
    g_mkdir_with_parents(dir, 0755);
    gchar *log = g_build_filename(dir, "crash.log", NULL);
    FILE *f = fopen(log, "a");
    if(f){
        time_t now=time(NULL);
        fprintf(f, "\n=== CRASH %s signal %d (%s) ===\n", ctime(&now), sig, strsignal(sig));
        backtrace_symbols_fd(bt, n, fileno(f));
        fclose(f);
    }
    g_free(dir); g_free(log);
    // Ghi ra stderr để apport bắt
    fprintf(stderr, "GoTV crash signal %d, log saved\n", sig);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
    // Thử reset preedit thay vì abort cứng
    _exit(1); // để ibus-daemon tự restart engine (watchdog)
}
static void install_crash_handlers(void){
    struct sigaction sa; memset(&sa,0,sizeof(sa));
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    // Bỏ qua SIGPIPE
    signal(SIGPIPE, SIG_IGN);
}

static void bus_connected_cb(IBusBus *b, gpointer user_data){
    IBusComponent *c = NULL;
    if(g_file_test("/usr/share/ibus/component/gotv.xml", G_FILE_TEST_EXISTS)){
        c = ibus_component_new_from_file("/usr/share/ibus/component/gotv.xml");
    }
    if(!c){
        c = ibus_component_new("org.freedesktop.IBus.GoTV","GoTV Engine (thuần hệ thống)","0.9.15","GPL","GoTV Project","https://github.com/isthaison/gotv","/usr/libexec/ibus-engine-gotv --ibus","gotv");
        IBusEngineDesc *d = ibus_engine_desc_new_varargs(
            "name", "gotv",
            "longname", "GoTV",
            "description", "GoTV: Telex/VNI - github.com/isthaison/gotv",
            "language", "vi",
            "license", "GPL",
            "author", "GoTV",
            "icon", "gotv",
            "layout", "us",
            "setup", "/usr/libexec/ibus-setup-gotv",
            NULL);
        ibus_component_add_engine(c, d);
    }
    ibus_bus_register_component(bus, c);
    if(!factory){
        factory=ibus_factory_new(ibus_bus_get_connection(bus));
        g_signal_connect(factory, "create-engine", G_CALLBACK(create_engine_cb), NULL);
        ibus_factory_add_engine(factory, "gotv", ibus_gotv_engine_get_type());
    }
    ibus_bus_request_name(bus,"org.freedesktop.IBus.GoTV",0);
}
int main(int argc, char **argv){
    install_crash_handlers();
    gtv_init();
    if(argc>1 && strcmp(argv[1],"--ibus")==0){
        ibus_init();
        bus=ibus_bus_new();
        g_signal_connect(bus,"connected",G_CALLBACK(bus_connected_cb),NULL);
        // Nếu bus đã connected sẵn (trường hợp restart nhanh), tạo factory ngay
        if(ibus_bus_is_connected(bus)){
            bus_connected_cb(bus, NULL);
        }
        g_main_loop_run(g_main_loop_new(NULL,FALSE));
    } else {
        printf("GoTV IBus engine - Telex/VNI thuần hệ thống\n");
        printf("Test: as->á, dd->đ, hoaf->hoà\n");
        // quick test using telex_transform
        GString *s=g_string_new("chao");
        GArray *buf=gstring_to_ucs4(s);
        telex_transform(buf,'f',TRUE);
        GString *out=g_string_new("");
        ucs4_to_gstring(buf,out);
        printf("chao+f => %s (expect chào)\n",out->str);
        g_string_free(s,TRUE); g_string_free(out,TRUE); g_array_free(buf,TRUE);
    }
    return 0;
}
