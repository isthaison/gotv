#define main ibus_application_main
#include "../linux/ibus/engine.c"
#undef main
#include <glib/gstdio.h>

static guint commit_count;
static gchar *last_commit;
static void commit_counter(GDBusConnection *connection,const gchar *sender,const gchar *path,
                           const gchar *interface,const gchar *name,GVariant *parameters,gpointer data){
    (void)connection;(void)sender;(void)path;(void)interface;(void)data;
    if(strcmp(name,"CommitText"))return;
    GVariant *serialized=NULL;g_variant_get(parameters,"(v)",&serialized);
    IBusText *text=IBUS_TEXT(ibus_serializable_deserialize(serialized));
    g_object_ref_sink(text);
    commit_count++;
    g_free(last_commit);last_commit=g_strdup(text->text);
    g_object_unref(text);g_variant_unref(serialized);
}
static void pump(void){
    /* Signal delivery round-trips through the test bus daemon, so each
     * drain is followed by a short sleep (same pattern as the AI waits). */
    for(int i=0;i<100;i++){ while(g_main_context_iteration(NULL,FALSE)); g_usleep(5000); }
}
static void pump_until(guint want){
    for(int i=0;i<200 && commit_count<want;i++){ while(g_main_context_iteration(NULL,FALSE)); g_usleep(10000); }
}

int main(int argc,char **argv) {
    g_test_init(&argc,&argv,NULL);
    gchar *directory=g_dir_make_tmp("gotv-ibus-test-XXXXXX",NULL);
    g_assert_nonnull(directory);
    g_setenv("XDG_CONFIG_HOME",directory,TRUE);
    gchar *config_dir=g_build_filename(directory,"gotv",NULL);
    GtvConfig config;gtv_config_load(&config,config_dir);config.spellcheck=FALSE;
    g_assert_true(gtv_config_save(&config,config_dir,NULL));gtv_config_clear(&config);
    GTestDBus *test_bus=g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(test_bus);
    GError *error=NULL;
    GDBusConnection *connection=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(test_bus),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,NULL,NULL,&error);
    g_assert_no_error(error);gtv_init();ibus_init();
    IBusEngine *engine=g_object_new(ibus_gotv_engine_get_type(),"engine-name","gotv",
        "object-path","/org/freedesktop/IBus/Engine/Test","connection",connection,"has-focus-id",TRUE,NULL);
    g_object_ref_sink(engine);
    IBusGoTVEngine *e=(IBusGoTVEngine*)engine;
    guint commit_sub=g_dbus_connection_signal_subscribe(connection,NULL,IBUS_INTERFACE_ENGINE,NULL,
        "/org/freedesktop/IBus/Engine/Test",NULL,G_DBUS_SIGNAL_FLAGS_NONE,commit_counter,NULL,NULL);
    ibus_gotv_engine_focus_in(engine);
    const struct {const gchar *input,*want;gboolean telex;} cases[]={
        {"bawst","bắt",TRUE},{"duocjwd","được",TRUE},{"DUOCJWD","ĐƯỢC",TRUE},
        {"ass","as",TRUE},{"duoc579","được",FALSE},{"[[","[",TRUE},{":smile:",":smile:",TRUE}
    };
    for(guint i=0;i<G_N_ELEMENTS(cases);i++) {
        ibus_gotv_engine_reset(e);e->mode_telex=cases[i].telex;
        for(const gchar *p=cases[i].input;*p;p++) {
            g_assert_true(ibus_gotv_engine_process_key_event(engine,(guint)*p,0,0));
            if(e->n_candidates > 1) {
                g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_Down,0,0));
                g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_Up,0,0));
            }
        }
        g_assert_cmpstr(e->preedit->str,==,cases[i].want);
    }
    g_assert_cmpstr(e->sentence_context->str,==,"");
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_a,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_space,0,0));
    g_assert_cmpstr(e->preedit->str,==,"");
    /* Partial syllables can be marked misspelled but must retain contextual completions. */
    ibus_gotv_engine_reset(e);
    e->spellcheck=TRUE;e->mode_telex=TRUE;e->config.suggest_enabled=TRUE;
    g_string_assign(e->sentence_context,"xin");
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_t,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_h,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_o,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_n,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_g,0,0));
    g_assert_cmpint(e->n_candidates,==,0);
    g_assert_cmpuint(e->suggest_timer,>,0);g_assert_null(e->suggest_cancellable);
    for(guint i=0;i<150 && !e->n_candidates;i++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    g_assert_cmpint(e->n_candidates,>,0);
    g_assert_cmpstr(e->candidates[0],==,"thông");
    e->config.suggest_enabled=FALSE;
    g_assert_cmpint(e->n_candidates,<=,5);
    /* Number keys 1..5 must NOT select candidates in Telex mode (e.g. typing an5). */
    ibus_gotv_engine_reset(e);
    e->mode_telex=TRUE;e->config.suggest_enabled=TRUE;
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_a,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_n,0,0));
    for(guint i=0;i<150 && !e->n_candidates;i++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    ibus_gotv_engine_process_key_event(engine,IBUS_5,0,0);
    g_assert_cmpstr(e->preedit->str,!=,"ảnh");
    e->config.suggest_enabled=FALSE;
    /* Only explicitly accepted dictionary candidates are persisted. */
    g_assert_false(word_valid(e,"kông"));
    e->candidates_from_dict=FALSE;learn_candidate(e,"kông");
    g_assert_false(word_valid(e,"kông"));
    e->candidates_from_dict=TRUE;learn_candidate(e,"kông");
    g_assert_true(word_valid(e,"KÔNG"));
    /* Seed data flags classic typos offline from the first run. */
    g_assert_false(word_valid(e,"hoăc"));
    /* A novel typo passes sync, but once the user accepts the dictionary
     * correction for it, the mapping is remembered and flags it offline. */
    g_assert_true(word_valid(e,"khoog"));
    gchar *saved_preedit=g_strdup(e->preedit->str);
    e->candidates_from_dict=TRUE;e->pending_bad=TRUE;
    e->pending_query=g_strdup("khoog");
    g_string_assign(e->preedit,"khoog");
    learn_candidate(e,"không");
    g_assert_false(word_valid(e,"khoog"));
    g_assert_true(word_valid(e,"không"));
    g_clear_pointer(&e->learned_fixes,g_hash_table_unref);
    g_assert_false(word_valid(e,"khoog"));
    g_string_assign(e->preedit,saved_preedit);g_free(saved_preedit);
    g_clear_pointer(&e->pending_query,g_free);e->pending_bad=FALSE;e->candidates_from_dict=FALSE;
    /* Offline helpers: case-insensitive fix lookup, folded completions. */
    e->candidates_from_dict=TRUE;learn_candidate(e,"thông");e->candidates_from_dict=FALSE;
    gchar *fix=learned_fix_for(e,"HOĂC");
    g_assert_cmpstr(fix,==,"hoặc");g_free(fix);
    g_assert_null(learned_fix_for(e,"việt"));
    GPtrArray *local=g_ptr_array_new_with_free_func(g_free);
    learned_completions(e,"thong",local,5);
    g_assert_cmpuint(local->len,==,2);
    g_assert_cmpstr(g_ptr_array_index(local,0),==,"thông");
    g_assert_cmpstr(g_ptr_array_index(local,1),==,"thống");
    g_ptr_array_unref(local);
    /* Full offline flow: the remembered correction is still suggested. */
    ibus_gotv_engine_reset(e);
    hide_suggest(e,engine);
    e->spellcheck=TRUE;e->config.suggest_enabled=TRUE;
    g_string_assign(e->preedit,"hoăc");
    push_preedit(e,engine,e->preedit->len,TRUE);
    for(guint i=0;i<200 && !e->n_candidates;i++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    g_assert_cmpint(e->n_candidates,==,1);
    g_assert_cmpstr(e->candidates[0],==,"hoặc");
    g_assert_true(e->candidates_from_dict);
    e->config.suggest_enabled=FALSE;
    ibus_gotv_engine_reset(e);
    hide_suggest(e,engine);
    /* User tables replace shipped tables entirely (then reload defaults). */
    gchar *saved_data_dir=g_strdup(g_getenv("GTV_DATA_DIR"));
    gchar *user_macros=g_build_filename(config_dir,"macros.txt",NULL);
    gchar *user_emojis=g_build_filename(config_dir,"emojis.txt",NULL);
    g_assert_true(g_file_set_contents(user_macros,"tq9=Test Quinn\n",-1,NULL));
    g_assert_true(g_file_set_contents(user_emojis,"# custom\n:tq9:=Test Flask\n",-1,NULL));
    g_unsetenv("GTV_DATA_DIR");
    gtv_tables_reload();
    gchar *custom=expand_word("TQ9");
    g_assert_cmpstr(custom,==,"Test Quinn");g_free(custom);
    custom=expand_word("vn");
    g_assert_cmpstr(custom,==,"vn");g_free(custom);
    GPtrArray *csugs=get_emoji_suggestions(":tq");
    g_assert_cmpuint(csugs->len,==,1);
    g_assert_cmpstr(g_ptr_array_index(csugs,0),==,"Test Flask");
    g_ptr_array_unref(csugs);
    g_remove(user_macros);g_remove(user_emojis);
    g_free(user_macros);g_free(user_emojis);
    if(saved_data_dir)g_setenv("GTV_DATA_DIR",saved_data_dir,TRUE);
    g_free(saved_data_dir);
    gtv_tables_reload();
    custom=expand_word("vn");
    g_assert_cmpstr(custom,==,"Việt Nam");g_free(custom);
    /* Test macro expansion on Tab vs Space in IBus */
    ibus_gotv_engine_reset(e);
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_v,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_n,0,0));
    g_assert_cmpstr(e->preedit->str,==,"vn");
    /* Tab expands macro and consumes event */
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_Tab,0,0));
    g_assert_cmpstr(e->preedit->str,==,"");

    /* Space does not expand macro */
    ibus_gotv_engine_reset(e);
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_v,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_n,0,0));
    g_assert_cmpstr(e->preedit->str,==,"vn");
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_space,0,0));
    g_assert_cmpstr(e->preedit->str,==,"");
    /* Tab expands emoji trigger as well */
    ibus_gotv_engine_reset(e);
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_colon,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_D,0,0));
    g_assert_cmpstr(e->preedit->str,==,":D");
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_Tab,0,0));
    g_assert_cmpstr(e->preedit->str,==,"");
    /* Space keeps an emoji trigger literally */
    ibus_gotv_engine_reset(e);
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_colon,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_parenright,0,0));
    g_assert_cmpstr(e->preedit->str,==,":)");
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_space,0,0));
    g_assert_cmpstr(e->preedit->str,==,"");
    /* Enter never expands: it commits the typed text and dismisses the popup.
     * Emoji triggers always keep a suggestion list loaded, so this used to
     * expand ":)" to an emoji instead of committing it. */
    ibus_gotv_engine_reset(e);
    hide_suggest(e,engine);
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_colon,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_parenright,0,0));
    g_assert_cmpstr(e->preedit->str,==,":)");
    g_assert_cmpint(e->n_candidates,>,0);
    pump(); /* drain commits from earlier cases: D-Bus delivery is async */
    guint before=commit_count;g_free(last_commit);last_commit=NULL;
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_Return,0,0));
    g_assert_cmpstr(e->preedit->str,==,"");
    g_assert_cmpint(e->n_candidates,==,0);
    pump_until(before+1);
    g_assert_cmpuint(commit_count,==,before+1);
    g_assert_cmpstr(last_commit,==,":)");
    /* Enter also commits a macro key literally, popup or not. */
    ibus_gotv_engine_reset(e);hide_suggest(e,engine);
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_v,0,0));
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_n,0,0));
    GPtrArray *fake=g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(fake,g_strdup("văn"));
    show_candidates(e,engine,fake);
    g_ptr_array_unref(fake);
    g_assert_cmpstr(e->preedit->str,==,"vn");
    pump();
    before=commit_count;g_free(last_commit);last_commit=NULL;
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_Return,0,0));
    g_assert_cmpstr(e->preedit->str,==,"");
    g_assert_cmpint(e->n_candidates,==,0);
    pump_until(before+1);
    g_assert_cmpuint(commit_count,==,before+1);
    g_assert_cmpstr(last_commit,==,"vn");
    /* Enter must not pick a highlighted suggestion either: Tab owns that. */
    ibus_gotv_engine_reset(e);hide_suggest(e,engine);
    g_string_assign(e->preedit,"khong");
    fake=g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(fake,g_strdup("không"));
    show_candidates(e,engine,fake);
    g_ptr_array_unref(fake);
    pump();
    before=commit_count;g_free(last_commit);last_commit=NULL;
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_Return,0,0));
    g_assert_cmpstr(e->preedit->str,==,"");
    pump_until(before+1);
    g_assert_cmpuint(commit_count,==,before+1);
    g_assert_cmpstr(last_commit,==,"khong");
    g_clear_pointer(&e->learned_words,g_hash_table_unref);
    g_assert_true(word_valid(e,"kông"));
    /* Modifier/lock keys must pass through without committing the syllable. */
    ibus_gotv_engine_reset(e);
    e->spellcheck=FALSE;
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_a,0,0));
    const guint modifiers[]={IBUS_Shift_L,IBUS_Shift_R,IBUS_Caps_Lock,
        IBUS_Shift_Lock,IBUS_Num_Lock,IBUS_Scroll_Lock};
    for(guint i=0;i<G_N_ELEMENTS(modifiers);i++) {
        g_assert_false(ibus_gotv_engine_process_key_event(engine,modifiers[i],0,0));
        g_assert_cmpstr(e->preedit->str,==,"a");
        g_assert_false(ibus_gotv_engine_process_key_event(engine,modifiers[i],0,IBUS_RELEASE_MASK));
        g_assert_cmpstr(e->preedit->str,==,"a");
    }
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_S,0,IBUS_LOCK_MASK));
    g_assert_cmpstr(e->preedit->str,==,"á");
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_b,0,0));
    g_assert_cmpstr(e->preedit->str,==,"áb");
    /* Case follows each resolved key, not the previous word or lock mask. */
    const struct { const gchar *input, *want; guint mask; } case_sequences[]={
        {"Bawst", "Bắt", 0}, {"bawSt", "bắt", IBUS_SHIFT_MASK},
        {"DUOCJWD", "ĐƯỢC", IBUS_LOCK_MASK}, {"duocjwd", "được", 0},
        {"duocjwd", "được", IBUS_LOCK_MASK | IBUS_SHIFT_MASK}
    };
    for(guint i=0;i<G_N_ELEMENTS(case_sequences);i++) {
        ibus_gotv_engine_reset(e);
        for(const gchar *p=case_sequences[i].input;*p;p++)
            g_assert_true(ibus_gotv_engine_process_key_event(engine,*p,0,case_sequences[i].mask));
        g_assert_cmpstr(e->preedit->str,==,case_sequences[i].want);
        g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_space,0,0));
        g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_a,0,0));
        g_assert_cmpstr(e->preedit->str,==,"a");
    }
    /* Mouse click must never multiply commits: interruptions (focus
     * change, client reset storms) keep the composition instead of
     * committing it; only explicit typing keys commit, exactly once. */
    ibus_gotv_engine_reset(e);
    e->mode_telex=TRUE;
    pump(); /* drain commits emitted by earlier cases: D-Bus delivery is async */
    guint base=commit_count;
    focus_in_id(engine,"app-a",NULL);
    for(const gchar *p="xin";*p;p++)
        g_assert_true(ibus_gotv_engine_process_key_event(engine,(guint)*p,0,0));
    g_assert_cmpstr(e->preedit->str,==,"xin");
    /* Click away and back, with client resets in between: no commit,
     * composition intact, typing continues. */
    focus_out_id(engine,"app-a");
    g_assert_cmpstr(e->preedit->str,==,"xin");
    ibus_gotv_engine_reset_cb(engine);
    ibus_gotv_engine_reset_cb(engine);
    g_assert_cmpstr(e->preedit->str,==,"xin");
    focus_in_id(engine,"app-a",NULL);
    g_assert_cmpstr(e->preedit->str,==,"xin");
    g_assert_cmpstr(e->sentence_context->str,==,"");
    pump();
    g_assert_cmpuint(commit_count,==,base);
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_j,0,0));
    g_assert_cmpstr(e->preedit->str,==,"xịn");
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_space,0,0));
    pump_until(base+1);
    g_assert_cmpuint(commit_count,==,base+1);
    g_assert_cmpstr(last_commit,==,"xịn ");
    /* Another input abandons the stash without committing it. */
    focus_in_id(engine,"app-a",NULL);
    for(const gchar *p="xin";*p;p++)
        g_assert_true(ibus_gotv_engine_process_key_event(engine,(guint)*p,0,0));
    focus_out_id(engine,"app-a");
    focus_in_id(engine,"app-b",NULL);
    g_assert_cmpstr(e->preedit->str,==,"");
    pump();
    g_assert_cmpuint(commit_count,==,base+1);
    g_assert_true(ibus_gotv_engine_process_key_event(engine,IBUS_a,0,0));
    g_assert_cmpstr(e->preedit->str,==,"a");
    g_free(last_commit);
    g_dbus_connection_signal_unsubscribe(connection,commit_sub);
    /* Named VNI engine pins its method: config telex must not flip it. */
    ibus_gotv_engine_reset(e);
    e->mode_telex=FALSE; e->mode_pinned=TRUE;
    GtvConfig pinconfig; gtv_config_load(&pinconfig,config_dir);
    pinconfig.mode=GTV_TELEX;
    g_assert_true(gtv_config_save(&pinconfig,config_dir,NULL)); gtv_config_clear(&pinconfig);
    for(const gchar *p="duoc579";*p;p++)
        g_assert_true(ibus_gotv_engine_process_key_event(engine,(guint)*p,0,0));
    g_assert_cmpstr(e->preedit->str,==,"được");
    g_assert_false(e->mode_telex);
    e->mode_telex=TRUE; e->mode_pinned=FALSE;
    /* Ctrl+T is unbound: Ctrl+T must not be swallowed, it falls through. */
    ibus_gotv_engine_reset(e);
    g_assert_false(ibus_gotv_engine_process_key_event(engine,IBUS_t,0,IBUS_CONTROL_MASK));
    g_assert_false(ibus_gotv_engine_process_key_event(engine,IBUS_t,0,IBUS_RELEASE_MASK));
    e->purpose=IBUS_INPUT_PURPOSE_PASSWORD;
    g_assert_false(ibus_gotv_engine_process_key_event(engine,IBUS_t,0,IBUS_CONTROL_MASK));
    e->purpose=IBUS_INPUT_PURPOSE_FREE_FORM;
    g_object_unref(engine);
    g_dbus_connection_close_sync(connection,NULL,NULL);g_object_unref(connection);
    g_test_dbus_down(test_bus);g_object_unref(test_bus);
    const gchar *names[]={"config"};
    for(guint i=0;i<G_N_ELEMENTS(names);i++) {gchar *path=g_build_filename(config_dir,names[i],NULL);g_remove(path);g_free(path);}
    gchar *learned=g_build_filename(config_dir,"learned-words.txt",NULL);g_remove(learned);g_free(learned);
    gchar *fixes=g_build_filename(config_dir,"learned-corrections.txt",NULL);g_remove(fixes);g_free(fixes);
    g_rmdir(config_dir);g_rmdir(directory);g_free(config_dir);g_free(directory);
    return 0;
}
