#include "../engine/internal.h"
#include <glib/gstdio.h>

static void test_stateful(void) {
    GtvConfig config = {.mode=GTV_TELEX,.modern=TRUE,.spellcheck=TRUE};
    GtvEngine *engine=gtv_engine_new(&config);
    const gchar *input="duocjwd";
    for(const gchar *p=input;*p;p++) g_assert_null(gtv_engine_process(engine,*p,NULL));
    gchar *buffer=gtv_engine_buffer(engine);
    g_assert_cmpstr(buffer,==,"được"); g_free(buffer);
    guint backspaces;
    g_assert_null(gtv_engine_process(engine,'\b',&backspaces));
    g_assert_cmpuint(backspaces,==,1);
    gchar *commit=gtv_engine_process(engine,' ',NULL);
    g_assert_cmpstr(commit,==,"đượ ");g_free(commit);
    const gchar *phrases[]={"vn\t",":SMILE:\t","ko\t",NULL};
    const gchar *expected[]={"Việt Nam","😊","không"};
    for(guint i=0;phrases[i];i++) {
        for(const gchar *p=phrases[i];*p;p++) {
            commit=gtv_engine_process(engine,*p,NULL);
            if(commit) {g_assert_cmpstr(commit,==,expected[i]);g_free(commit);}
        }
    }
    /* Space does not expand macro */
    for(const gchar *p="vn ";*p;p++) {
        commit=gtv_engine_process(engine,*p,NULL);
        if(commit) {g_assert_cmpstr(commit,==,"vn ");g_free(commit);}
    }
    gtv_engine_process(engine,'[',NULL);
    buffer=gtv_engine_buffer(engine);g_assert_cmpstr(buffer,==,"ươ");g_free(buffer);
    gtv_engine_process(engine,'[',NULL);
    buffer=gtv_engine_buffer(engine);g_assert_cmpstr(buffer,==,"[");g_free(buffer);
    gtv_engine_reset(engine);
    buffer=gtv_engine_buffer(engine);g_assert_cmpstr(buffer,==,"");g_free(buffer);
    gtv_engine_free(engine);
    g_assert_null(gtv_transform("\xff",GTV_TELEX,TRUE));
}
static void test_config(void) {
    gchar *directory=g_dir_make_tmp("gotv-config-XXXXXX",NULL);
    g_assert_nonnull(directory);
    GtvConfig config,loaded;
    gtv_config_load(&config,directory);
    g_assert_cmpint(config.mode,==,GTV_TELEX);
    g_assert_true(config.modern);g_assert_true(config.spellcheck);
    g_assert_true(config.suggest_enabled);
    config.mode=GTV_VNI;config.modern=FALSE;config.spellcheck=FALSE;config.suggest_enabled=FALSE;
    g_assert_true(gtv_config_save(&config,directory,NULL));
    gtv_config_load(&loaded,directory);
    g_assert_cmpint(loaded.mode,==,GTV_VNI);g_assert_false(loaded.modern);g_assert_false(loaded.spellcheck);
    g_assert_false(loaded.suggest_enabled);
    gtv_config_clear(&loaded);gtv_config_clear(&config);
    gchar *path=g_build_filename(directory,"config",NULL);
    g_assert_true(g_file_set_contents(path,"[suggest]\nenable=true\n",-1,NULL));
    gtv_config_load(&loaded,directory);
    g_assert_true(loaded.suggest_enabled);
    gtv_config_clear(&loaded);
    /* Legacy [ai] section still migrates: explicit enable wins, otherwise
     * provider=ollama implies on, provider=rule implies off. */
    g_assert_true(g_file_set_contents(path,"[ai]\nprovider=rule\n",-1,NULL));
    gtv_config_load(&loaded,directory);
    g_assert_false(loaded.suggest_enabled);
    gtv_config_clear(&loaded);
    g_assert_true(g_file_set_contents(path,"[ai]\nprovider=ollama\n",-1,NULL));
    gtv_config_load(&loaded,directory);
    g_assert_true(loaded.suggest_enabled);
    gtv_config_clear(&loaded);
    g_assert_true(g_file_set_contents(path,"[ai]\nenable=false\nprovider=ollama\n",-1,NULL));
    gtv_config_load(&loaded,directory);
    g_assert_false(loaded.suggest_enabled);
    gtv_config_clear(&loaded);
    g_remove(path);g_free(path);
    path=g_build_filename(directory,"config",NULL);g_remove(path);g_free(path);
    g_rmdir(directory);g_free(directory);
}
static gboolean test_deb_match(const gchar *asset, const gchar *tagver, gpointer data) {
    const gchar *arch = data;
    gchar *prefix = g_strdup_printf("gotv_%s", tagver);
    gboolean ok = g_str_has_prefix(asset, prefix) && g_str_has_suffix(asset, ".deb")
        && (!arch || !*arch || strstr(asset, arch) != NULL);
    g_free(prefix);
    return ok;
}
static void test_update(void) {
    /* Version ordering: numeric parts, missing parts are zero, leading v
     * ignored, release beats prerelease with the same numbers. */
    const struct { const gchar *a, *b; gint sign; } order[] = {
        {"0.3.0", "0.3.0", 0}, {"v0.3.0", "0.3.0", 0}, {"0.3", "0.3.0", 0},
        {"0.3.0", "0.3.1", -1}, {"0.3.1", "0.3.0", 1}, {"0.10.0", "0.9.9", 1},
        {"1.0.0", "0.99.99", 1}, {"0.3.0-rc1", "0.3.0", -1}, {"0.3.0", "0.3.0-rc1", 1},
        {"0.3.0-rc1", "0.3.0-rc2", -1}, {"0.4.0", "0.3.0", 1}, {"0.3.0.0", "0.3.0", 0},
        {"0.3.0.1", "0.3.0", 1}, {"10.0", "9.9.9", 1},
    };
    for (guint i = 0; i < G_N_ELEMENTS(order); i++) {
        gint got = gtv_version_compare(order[i].a, order[i].b);
        gint want = order[i].sign;
        if (want == 0) g_assert_cmpint(got, ==, 0);
        else g_assert_cmpint((got > 0) - (got < 0), ==, want);
        gint rev = gtv_version_compare(order[i].b, order[i].a);
        g_assert_cmpint((rev > 0) - (rev < 0), ==, -want);
    }
    gchar *asset = gtv_update_asset_name("v0.4.0");
    g_assert_cmpstr(asset, ==, "gotv-0.4.0-x64-setup.exe");
    g_free(asset);
    /* Release payload parsing: newer tag + matching asset. */
    const gchar *payload =
        "{\"tag_name\":\"v0.4.0\",\"prerelease\":false,"
        "\"assets\":["
        "{\"name\":\"gotv-0.4.0-x64-setup.exe\","
        " \"browser_download_url\":\"https://github.com/x/y/releases/download/v0.4.0/gotv-0.4.0-x64-setup.exe\","
        " \"uploader\":{\"login\":\"x\"}},"
        "{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"https://example.com/sums\"}]}";
    gchar *tag = NULL, *url = NULL;
    g_assert_cmpint(gtv_update_parse_release(payload, "0.3.0", &tag, &url), ==, GTV_UPDATE_AVAILABLE);
    g_assert_cmpstr(tag, ==, "v0.4.0");
    g_assert_true(g_str_has_suffix(url, "gotv-0.4.0-x64-setup.exe"));
    g_free(tag); g_free(url);
    /* Same or newer current version: no update, no out strings. */
    g_assert_cmpint(gtv_update_parse_release(payload, "0.4.0", &tag, &url), ==, GTV_UPDATE_CURRENT);
    g_assert_null(tag); g_assert_null(url);
    g_assert_cmpint(gtv_update_parse_release(payload, "1.0.0", &tag, &url), ==, GTV_UPDATE_CURRENT);
    /* Newer tag but no matching asset: error, never a blind update. */
    const gchar *noasset = "{\"tag_name\":\"v0.5.0\",\"assets\":[{\"name\":\"notes.txt\",\"browser_download_url\":\"https://example.com/n\"}]}";
    g_assert_cmpint(gtv_update_parse_release(noasset, "0.3.0", &tag, &url), ==, GTV_UPDATE_ERROR);
    g_assert_null(tag); g_assert_null(url);
    /* Malformed payloads. */
    const gchar *bad[] = {"", "{", "{\"tag_name\":123}", "{\"tag_name\":\"\"}", "{\"assets\":[]}", NULL};
    for (guint i = 0; bad[i]; i++)
        g_assert_cmpint(gtv_update_parse_release(bad[i], "0.3.0", &tag, &url), ==, GTV_UPDATE_ERROR);
    g_assert_cmpint(gtv_update_parse_release(NULL, "0.3.0", NULL, NULL), ==, GTV_UPDATE_ERROR);
    g_assert_cmpint(gtv_update_parse_release(payload, NULL, NULL, NULL), ==, GTV_UPDATE_ERROR);
    /* Debian matcher: picks the arch build, ignores exe/foreign arch. */
    const gchar *deb_payload =
        "{\"tag_name\":\"v0.6.1\",\"assets\":["
        "{\"name\":\"gotv-0.6.1-x64-setup.exe\","
        " \"browser_download_url\":\"https://example.com/setup.exe\"},"
        "{\"name\":\"gotv_0.6.1-1_arm64.deb\","
        " \"browser_download_url\":\"https://example.com/arm64.deb\"},"
        "{\"name\":\"gotv_0.6.1-1_amd64.deb\","
        " \"browser_download_url\":\"https://example.com/amd64.deb\"}]}";
    tag = NULL; url = NULL;
    g_assert_cmpint(gtv_update_parse_release_full(deb_payload, "0.6.0",
        test_deb_match, (gpointer)"amd64", &tag, &url), ==, GTV_UPDATE_AVAILABLE);
    g_assert_cmpstr(tag, ==, "v0.6.1");
    g_assert_cmpstr(url, ==, "https://example.com/amd64.deb");
    g_free(tag); g_free(url);
    /* Up to date and wrong-arch cases. */
    g_assert_cmpint(gtv_update_parse_release_full(deb_payload, "v0.6.1",
        test_deb_match, (gpointer)"amd64", &tag, &url), ==, GTV_UPDATE_CURRENT);
    g_assert_null(tag); g_assert_null(url);
    g_assert_cmpint(gtv_update_parse_release_full(deb_payload, "0.6.0",
        test_deb_match, (gpointer)"riscv64", &tag, &url), ==, GTV_UPDATE_ERROR);
    g_assert_null(tag); g_assert_null(url);
    /* NULL matcher is an error, never a blind match. */
    g_assert_cmpint(gtv_update_parse_release_full(deb_payload, "0.6.0",
        NULL, NULL, &tag, &url), ==, GTV_UPDATE_ERROR);
    /* 24h autocheck throttle honours XDG_CONFIG_HOME. */
    gchar *cfgdir = g_dir_make_tmp("gotv-update-XXXXXX", NULL);
    g_assert_nonnull(cfgdir);
    g_setenv("XDG_CONFIG_HOME", cfgdir, TRUE);
    g_assert_true(gtv_update_should_autocheck());
    gtv_update_mark_checked();
    g_assert_false(gtv_update_should_autocheck());
    gchar *stamp = g_build_filename(cfgdir, "gotv", "update-check", NULL);
    gchar *old = g_strdup_printf("%" G_GINT64_FORMAT, g_get_real_time() / 1000000 - 25 * 3600);
    g_assert_true(g_file_set_contents(stamp, old, -1, NULL));
    g_assert_true(gtv_update_should_autocheck());
    g_free(old); g_free(stamp);
    g_free(cfgdir);
}
static void test_spelling(void) {
    g_assert_true(spell_word_valid("được"));
    g_assert_false(spell_word_valid("kông")); /* k chỉ đi với i/e/y */
    /* Thuần quy tắc âm tiết, không danh sách cứng: hoăc/ge hợp cấu trúc nên
     * qua vòng sync; lỗi kiểu này do từ điển sửa và được nhớ vào
     * learned-corrections.txt để gạch đỏ ngay lần sau (xem test_ibus). */
    g_assert_true(spell_word_valid("hoăc"));
    g_assert_false(spell_word_valid("bàc")); /* huyền + phụ âm tắc c */
    g_assert_false(spell_word_valid("vièt")); /* huyền + phụ âm tắc t */
    /* Chưa gõ dấu thì sync luôn cho qua (đang gõ dở/chữ thô); ngh+a sai
     * vẫn do từ điển sửa và được nhớ vào learned-corrections.txt. */
    g_assert_true(spell_word_valid("ngha"));
    g_assert_true(spell_word_valid("ge"));

    /* Disabled suggestions return nothing, even for known typos. */
    GtvConfig config={.suggest_enabled=FALSE};
    GPtrArray *suggestions=gtv_suggest_combined(&config,"","kông",TRUE);
    g_assert_cmpuint(suggestions->len,==,0);
    g_ptr_array_unref(suggestions);

    /* Enabled: the dictionary corrects the classic typo offline. */
    config.suggest_enabled=TRUE;
    suggestions=gtv_suggest_combined(&config,"","kông",TRUE);
    g_assert_cmpuint(suggestions->len,>,0);
    g_assert_cmpstr(g_ptr_array_index(suggestions,0),==,"không");
    g_ptr_array_unref(suggestions);

    suggestions=gtv_suggest_combined(&config,"","Kông",TRUE);
    g_assert_cmpuint(suggestions->len,>,0);
    g_ptr_array_unref(suggestions);

    suggestions=gtv_suggest_combined(&config,"","vièt",TRUE);
    g_assert_cmpuint(suggestions->len,>,0);
    g_ptr_array_unref(suggestions);

    suggestions=gtv_suggest_combined(&config,"","hoăc",TRUE);
    g_assert_cmpuint(suggestions->len,==,1);
    g_assert_cmpstr(g_ptr_array_index(suggestions,0),==,"hoặc");
    g_ptr_array_unref(suggestions);

    /* A correct word is not "corrected". */
    suggestions=gtv_suggest_combined(&config,"","được",TRUE);
    gboolean found=FALSE;
    for(guint i=0;i<suggestions->len;i++)
        if(!strcmp(g_ptr_array_index(suggestions,i),"được")) found=TRUE;
    g_assert_false(found);
    g_ptr_array_unref(suggestions);
}

static void test_dict(void) {
    /* The shipped seed loads offline: version tag, word count, lookups. */
    gtv_dict_set_langs("vi");
    guint vi_count = gtv_dict_count();
    g_assert_cmpuint(vi_count, >, 1000);
    g_assert_true(gtv_dict_tag() && *gtv_dict_tag());
    /* Catalog and per-pack counts (vi, tsql, c, csharp, en). */
    g_assert_cmpuint(gtv_dict_catalog_count(), >=, 5);
    g_assert_nonnull(gtv_dict_catalog_find("tsql"));
    g_assert_nonnull(gtv_dict_catalog_find("t-sql"));
    g_assert_nonnull(gtv_dict_catalog_find("c"));
    g_assert_nonnull(gtv_dict_catalog_find("csharp"));
    g_assert_nonnull(gtv_dict_catalog_find("c#"));
    g_assert_cmpuint(gtv_dict_pack_count("tsql"), >, 100);
    g_assert_cmpuint(gtv_dict_pack_count("c"), >, 100);
    g_assert_cmpuint(gtv_dict_pack_count("csharp"), >, 100);
    g_assert_cmpuint(gtv_dict_pack_count("en"), >, 100);
    /* Selecting multiple packs merges their vocabularies. */
    gtv_dict_set_langs("vi,t-sql,c,c#");
    g_assert_cmpstr(gtv_dict_get_langs(), ==, "vi,tsql,c,csharp");
    g_assert_cmpuint(gtv_dict_count(), >, vi_count);
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    /* T-SQL completions (including underscore identifiers). */
    gtv_dict_complete("nvarch", out, 5);
    g_assert_cmpuint(out->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(out, 0), ==, "nvarchar");
    g_ptr_array_set_size(out, 0);
    gtv_dict_complete("sp_exec", out, 5);
    g_assert_cmpuint(out->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(out, 0), ==, "sp_executesql");
    g_ptr_array_set_size(out, 0);
    /* C keyword / stdint completions. */
    gtv_dict_complete("typede", out, 5);
    g_assert_cmpuint(out->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(out, 0), ==, "typedef");
    g_ptr_array_set_size(out, 0);
    gtv_dict_complete("uint32_", out, 5);
    g_assert_cmpuint(out->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(out, 0), ==, "uint32_t");
    g_ptr_array_set_size(out, 0);
    /* C# keyword / BCL completions. */
    gtv_dict_complete("ienumera", out, 5);
    g_assert_cmpuint(out->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(out, 0), ==, "ienumerable");
    g_ptr_array_set_size(out, 0);
    /* Toggle helper. */
    gchar *toggled = gtv_dict_lang_toggle("vi,tsql", "csharp", TRUE);
    g_assert_cmpstr(toggled, ==, "vi,tsql,csharp");
    g_free(toggled);
    /* Completions are alphabetical; every hit shares the (folded) prefix. */
    gtv_dict_complete("thong", out, 5);
    g_assert_cmpuint(out->len, >, 0);
    g_assert_cmpuint(out->len, <=, 5);
    gboolean found_thong = FALSE;
    for (guint i = 0; i < out->len; i++) {
        gchar *w = g_ptr_array_index(out, i);
        gchar *f = gtv_fold_accents(w);
        g_assert_true(g_str_has_prefix(w, "thong") || (f && g_str_has_prefix(f, "thong")));
        if (!strcmp(w, "thông")) found_thong = TRUE;
        g_free(f);
    }
    g_assert_true(found_thong);
    g_ptr_array_set_size(out, 0);
    /* Corrections: classic typo ranks first via the fixes seed. */
    GPtrArray *fix = gtv_suggest_combined(
        &(GtvConfig){.suggest_enabled = TRUE}, "", "kông", TRUE);
    g_assert_cmpuint(fix->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(fix, 0), ==, "không");
    g_ptr_array_unref(fix);
    /* Pure dictionary correction (accent variants rank alphabetically). */
    gtv_dict_correct("chao", out, 5);
    g_assert_cmpuint(out->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(out, 0), ==, "chào");
    g_ptr_array_unref(out);
    /* A corrupt download never poisons suggestions: unparsable content
     * yields an empty table, and the loader falls back to the seed. */
    gchar *tmp = g_dir_make_tmp("gotv-dict-XXXXXX", NULL);
    g_assert_nonnull(tmp);
    gchar *bad = g_build_filename(tmp, "dict-vi.txt", NULL);
    g_assert_true(g_file_set_contents(bad, "\xff\xfe-not-utf8\n", -1, NULL));
    g_assert_true(g_file_set_contents(bad, "# dict-tag: v9.9.9\n!!!\n", -1, NULL));
    g_remove(bad); g_free(bad);
    g_rmdir(tmp); g_free(tmp);
}
static void test_config_defaults(void) {
    /* Shipped data files drive defaults; user files override them. */
    gchar *empty=g_dir_make_tmp("gotv-defaults-XXXXXX",NULL);
    g_assert_nonnull(empty);
    GtvConfig cfg;
    gtv_config_load(&cfg,empty);
    g_assert_cmpint(cfg.mode,==,GTV_TELEX);
    g_assert_true(cfg.modern);g_assert_true(cfg.spellcheck);
    g_assert_true(cfg.suggest_enabled);
    gtv_config_clear(&cfg);
    gchar *uc=g_build_filename(empty,"config",NULL);
    g_assert_true(g_file_set_contents(uc,"[input]\nmethod=vni\n",-1,NULL));
    g_free(uc);
    gtv_config_load(&cfg,empty);
    g_assert_cmpint(cfg.mode,==,GTV_VNI);
    gtv_config_clear(&cfg);
    gchar *ua=g_build_filename(empty,"config",NULL);
    g_assert_true(g_file_set_contents(ua,"[suggest]\nenable=false\n",-1,NULL));
    g_free(ua);
    gtv_config_load(&cfg,empty);
    g_assert_false(cfg.suggest_enabled);
    gtv_config_clear(&cfg);
    gchar *rc=g_build_filename(empty,"config",NULL);g_remove(rc);g_free(rc);
    g_rmdir(empty);g_free(empty);
}

static void test_learn(void) {
    /* word_key normalization. */
    gchar *k = gtv_word_key("KÔNG");
    g_assert_cmpstr(k, ==, "kông"); g_free(k);
    g_assert_null(gtv_word_key(""));
    g_assert_null(gtv_word_key("a b"));
    g_assert_null(gtv_word_key(NULL));
    /* Roundtrip through real files in a temp dir. */
    gchar *tmp = g_dir_make_tmp("gotv-learn-XXXXXX", NULL);
    g_assert_nonnull(tmp);
    gchar *wp = g_build_filename(tmp, "learned-words.txt", NULL);
    gchar *fp = g_build_filename(tmp, "learned-corrections.txt", NULL);
    GHashTable *words = gtv_words_table_new();
    GHashTable *fixes = gtv_fixes_table_new();
    g_assert_true(gtv_words_learn(words, "Thông"));
    g_assert_false(gtv_words_learn(words, "thông")); /* dup */
    g_assert_false(gtv_words_learn(words, "a b"));
    g_assert_true(gtv_fixes_learn(fixes, "HOĂC", "Hoặc"));
    g_assert_false(gtv_fixes_learn(fixes, "hoăc", "hoặc")); /* dup */
    g_assert_false(gtv_fixes_learn(fixes, "x", "x")); /* identical */
    g_assert_true(gtv_words_save(words, wp));
    g_assert_true(gtv_fixes_save(fixes, fp));
    g_hash_table_unref(words); g_hash_table_unref(fixes);
    words = gtv_words_table_new();
    fixes = gtv_fixes_table_new();
    g_assert_true(gtv_words_load(words, wp));
    g_assert_true(gtv_fixes_load(fixes, fp));
    g_assert_true(g_hash_table_contains(words, "thông"));
    gchar *f = gtv_fixes_lookup(fixes, "HOĂC");
    g_assert_cmpstr(f, ==, "hoặc"); g_free(f);
    g_assert_null(gtv_fixes_lookup(fixes, "việt"));
    /* Completions straight from the shared table. */
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    gtv_learned_completions(words, "thong", out, 5);
    g_assert_cmpuint(out->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(out, 0), ==, "thông");
    g_ptr_array_unref(out);
    g_hash_table_unref(words); g_hash_table_unref(fixes);
    g_remove(wp); g_free(wp);
    g_remove(fp); g_free(fp);
    g_rmdir(tmp); g_free(tmp);
}

/* Every permutation of pending operations must converge to the same syllable. */
static void permute(const gchar *prefix, gchar *keys, guint index, GtvMode mode, gboolean modern, const gchar *want) {
    if(!keys[index]) {
        gchar *input=g_strconcat(prefix,keys,NULL);
        gchar *got=gtv_transform(input,mode,modern);
        if(g_strcmp0(got,want)) g_error("%s (mode=%d,modern=%d) -> %s, expected %s",input,mode,modern,got,want);
        g_free(got);g_free(input);return;
    }
    for(guint i=index;keys[i];i++) {
        gchar swap=keys[index];keys[index]=keys[i];keys[i]=swap;
        permute(prefix,keys,index+1,mode,modern,want);
        swap=keys[index];keys[index]=keys[i];keys[i]=swap;
    }
}
static void test_order_independence(void) {
    const gchar tones[]="sfrxj";
    for(int modern=0;modern<2;modern++) for(int upper=0;upper<2;upper++) for(int tone=1;tone<=5;tone++) {
        gunichar vowel;g_assert_true(lookup_char('o',DIAC_HORN,tone,upper,&vowel));
        GString *want=g_string_new(upper?"ĐƯ":"đư");g_string_append_unichar(want,vowel);g_string_append(want,upper?"C":"c");
        gchar telex[]={upper?'C':'c',upper?'W':'w',upper?'D':'d',upper?g_ascii_toupper(tones[tone-1]):tones[tone-1],0};
        gchar vni[]={upper?'C':'c','7','9',(gchar)('0'+tone),0};
        permute(upper?"DUO":"duo",telex,0,GTV_TELEX,modern,want->str);
        permute(upper?"DUO":"duo",vni,0,GTV_VNI,modern,want->str);
        g_string_free(want,TRUE);
    }
    gchar telex[]="wst",vni[]="81t";
    permute("ba",telex,0,GTV_TELEX,TRUE,"bắt");
    permute("ba",vni,0,GTV_VNI,TRUE,"bắt");
}
static void test_random_input(void) {
    GRand *rng=g_rand_new_with_seed(95);
    const gchar *alphabet="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789[]{}:";
    for(int mode=0;mode<2;mode++) for(int n=0;n<10000;n++) {
        gchar input[65];
        guint length=g_rand_int_range(rng,0,65);
        for(guint i=0;i<length;i++) input[i]=alphabet[g_rand_int_range(rng,0,strlen(alphabet))];
        input[length]=0;
        gchar *out=gtv_transform(input,mode,n%2);
        g_assert_nonnull(out);g_assert_true(g_utf8_validate(out,-1,NULL));
        g_assert_cmpint(g_utf8_strlen(out,-1),<=,length*2);
        g_free(out);
    }
    g_rand_free(rng);
}
static void on_async_suggest_done(GObject *src, GAsyncResult *res, gpointer data){
    gboolean *done = data;
    GError *error = NULL;
    GPtrArray *out = gtv_suggest_combined_finish(res, &error);
    g_assert_no_error(error);
    g_assert_nonnull(out);
    g_assert_cmpuint(out->len, >, 0);
    g_ptr_array_unref(out);
    *done = TRUE;
}

static void test_suggest_combined(void) {
    GtvConfig config = {.suggest_enabled = TRUE};
    GPtrArray *s1 = gtv_suggest_combined(&config, "Tôi ", "kông", TRUE);
    g_assert_nonnull(s1);
    g_assert_cmpuint(s1->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(s1, 0), ==, "không");
    g_ptr_array_unref(s1);

    GPtrArray *s2 = gtv_suggest_combined(&config, "xin", "thong", FALSE);
    g_assert_nonnull(s2);
    g_assert_cmpuint(s2->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(s2, 0), ==, "thông");
    g_ptr_array_unref(s2);

    gboolean done = FALSE;
    gtv_suggest_combined_async(&config, "xin", "thong", FALSE, NULL, on_async_suggest_done, &done);
    while(!done) {
        g_main_context_iteration(NULL, TRUE);
    }
    g_assert_true(done);
}

static void test_macro_and_emoji(void) {
    /* Test expand_word for emojis */
    gchar *e1 = expand_word(":smile:");
    g_assert_cmpstr(e1, ==, "😊");
    g_free(e1);

    gchar *e2 = expand_word(":heart:");
    g_assert_cmpstr(e2, ==, "❤️");
    g_free(e2);

    gchar *e3 = expand_word(":fire:");
    g_assert_cmpstr(e3, ==, "🔥");
    g_free(e3);

    /* Test expand_word for text emoticons */
    gchar *m1 = expand_word(":)");
    g_assert_cmpstr(m1, ==, "😊");
    g_free(m1);

    gchar *m2 = expand_word("<3");
    g_assert_cmpstr(m2, ==, "❤️");
    g_free(m2);

    /* Test macro expansion */
    gchar *mc1 = expand_word("vn");
    g_assert_cmpstr(mc1, ==, "Việt Nam");
    g_free(mc1);

    gchar *mc2 = expand_word("dc");
    g_assert_cmpstr(mc2, ==, "được");
    g_free(mc2);

    /* Test get_emoji_suggestions */
    GPtrArray *sugs = get_emoji_suggestions(":sm");
    g_assert_nonnull(sugs);
    g_assert_cmpuint(sugs->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(sugs, 0), ==, "😊");
    g_ptr_array_unref(sugs);

    /* Test emoticons with uppercase and other starter characters */
    GPtrArray *s_d = get_emoji_suggestions(":D");
    g_assert_nonnull(s_d);
    g_assert_cmpuint(s_d->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(s_d, 0), ==, "😀");
    g_ptr_array_unref(s_d);

    GPtrArray *s_dash_d = get_emoji_suggestions(":-D");
    g_assert_nonnull(s_dash_d);
    g_assert_cmpuint(s_dash_d->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(s_dash_d, 0), ==, "😀");
    g_ptr_array_unref(s_dash_d);

    GPtrArray *s_wink = get_emoji_suggestions(";)");
    g_assert_nonnull(s_wink);
    g_assert_cmpuint(s_wink->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(s_wink, 0), ==, "😉");
    g_ptr_array_unref(s_wink);

    GPtrArray *s_heart = get_emoji_suggestions("<3");
    g_assert_nonnull(s_heart);
    g_assert_cmpuint(s_heart->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(s_heart, 0), ==, "❤️");
    g_ptr_array_unref(s_heart);

    GPtrArray *s_p = get_emoji_suggestions(":P");
    g_assert_nonnull(s_p);
    g_assert_cmpuint(s_p->len, >, 0);
    g_assert_cmpstr(g_ptr_array_index(s_p, 0), ==, "😛");
    g_ptr_array_unref(s_p);

    /* Test emoji stays in buffer until Tab (no auto-expand) */
    GtvConfig cfg = {.mode = GTV_TELEX, .modern = TRUE};
    GtvEngine *eng = gtv_engine_new(&cfg);

    const char *seq = ":smile:";
    gchar *commit = NULL;
    guint bs = 0;
    for(const char *p = seq; *p; p++){
        g_free(commit);
        commit = gtv_engine_process(eng, (gunichar)*p, NULL);
    }
    g_assert_null(commit);
    g_free(commit);
    commit = gtv_engine_process(eng, '\t', &bs);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "😊");
    g_assert_cmpuint(bs, ==, 7);
    g_free(commit);

    /* Test Tab expansion for :) */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, ':', NULL));
    g_assert_null(gtv_engine_process(eng, ')', NULL));
    commit = gtv_engine_process(eng, '\t', &bs);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "😊");
    g_assert_cmpuint(bs, ==, 2);
    g_free(commit);

    /* Test Tab expansion for :-) */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, ':', NULL));
    g_assert_null(gtv_engine_process(eng, '-', NULL));
    g_assert_null(gtv_engine_process(eng, ')', NULL));
    commit = gtv_engine_process(eng, '\t', &bs);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "😊");
    g_assert_cmpuint(bs, ==, 3);
    g_free(commit);

    /* Test Tab expansion for <3 */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, '<', NULL));
    g_assert_null(gtv_engine_process(eng, '3', NULL));
    commit = gtv_engine_process(eng, '\t', &bs);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "❤️");
    g_assert_cmpuint(bs, ==, 2);
    g_free(commit);

    /* Test parenthesized emoji aliases expand on Tab, not automatically */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, '(', NULL));
    g_assert_null(gtv_engine_process(eng, 'y', NULL));
    g_assert_null(gtv_engine_process(eng, ')', NULL));
    commit = gtv_engine_process(eng, '\t', &bs);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "👍");
    g_assert_cmpuint(bs, ==, 3);
    g_free(commit);

    /* Test :D expands on Tab */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, ':', NULL));
    g_assert_null(gtv_engine_process(eng, 'D', NULL));
    commit = gtv_engine_process(eng, '\t', &bs);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "😀");
    g_assert_cmpuint(bs, ==, 2);
    g_free(commit);

    /* Test emoji trigger + Space commits literally (no expansion) */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, ':', NULL));
    g_assert_null(gtv_engine_process(eng, ')', NULL));
    commit = gtv_engine_process(eng, ' ', NULL);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, ":) ");
    g_free(commit);

    /* Test emoji trigger + punctuation commits literally */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, '(', NULL));
    g_assert_null(gtv_engine_process(eng, 'y', NULL));
    g_assert_null(gtv_engine_process(eng, ')', NULL));
    commit = gtv_engine_process(eng, '.', NULL);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "(y).");
    g_free(commit);

    /* Test normal colon after word */
    gtv_engine_reset(eng);
    for(const char *p = "chao"; *p; p++) g_assert_null(gtv_engine_process(eng, *p, NULL));
    commit = gtv_engine_process(eng, ':', NULL);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "chao:");
    g_free(commit);

    /* Test expand_macro and expand_emoji distinct functions */
    gchar *mac = expand_macro("vn");
    g_assert_cmpstr(mac, ==, "Việt Nam");
    g_free(mac);
    g_assert_null(expand_macro("not_a_macro"));
    g_assert_null(expand_macro(":smile:"));
    gchar *emj = expand_emoji(":smile:");
    g_assert_cmpstr(emj, ==, "😊");
    g_free(emj);
    g_assert_null(expand_emoji("vn"));

    /* Test macro does NOT expand automatically on Space */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, 'v', NULL));
    g_assert_null(gtv_engine_process(eng, 'n', NULL));
    commit = gtv_engine_process(eng, ' ', NULL);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "vn ");
    g_free(commit);

    /* Test macro does NOT expand automatically on punctuation */
    gtv_engine_reset(eng);
    g_assert_null(gtv_engine_process(eng, 'v', NULL));
    g_assert_null(gtv_engine_process(eng, 'n', NULL));
    commit = gtv_engine_process(eng, '.', NULL);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "vn.");
    g_free(commit);

    /* Test macro expands on Tab */
    gtv_engine_reset(eng);
    bs = 0;
    g_assert_null(gtv_engine_process(eng, 'v', &bs));
    g_assert_null(gtv_engine_process(eng, 'n', &bs));
    commit = gtv_engine_process(eng, '\t', &bs);
    g_assert_nonnull(commit);
    g_assert_cmpstr(commit, ==, "Việt Nam");
    g_assert_cmpuint(bs, ==, 2);
    g_free(commit);

    /* Test non-macro returns NULL on Tab */
    gtv_engine_reset(eng);
    bs = 0;
    for(const char *p = "chao"; *p; p++) g_assert_null(gtv_engine_process(eng, *p, &bs));
    commit = gtv_engine_process(eng, '\t', &bs);
    g_assert_null(commit);

    gtv_engine_free(eng);
}

static void test_table_manage(void) {
    gtv_tables_reload();
    g_assert_cmpuint(gtv_table_count(TRUE), >, 0);
    gchar *upath = gtv_table_user_path(FALSE);
    gchar *orig = NULL;
    gboolean had = g_file_get_contents(upath, &orig, NULL, NULL);

    guint n0 = gtv_table_count(FALSE);
    /* Invalid entries are rejected, table untouched. */
    g_assert_false(gtv_table_set(FALSE, "", "x"));
    g_assert_false(gtv_table_set(FALSE, "a=b", "x"));
    g_assert_false(gtv_table_set(FALSE, "a b", "x"));
    g_assert_false(gtv_table_set(FALSE, "k", ""));
    g_assert_cmpuint(gtv_table_count(FALSE), ==, n0);
    /* Add, read back, expand. */
    g_assert_true(gtv_table_set(FALSE, "gtvtest", "Go Test"));
    g_assert_cmpuint(gtv_table_count(FALSE), ==, n0 + 1);
    const gchar *k = NULL, *v = NULL;
    g_assert_true(gtv_table_get(FALSE, gtv_table_count(FALSE) - 1, &k, &v));
    g_assert_cmpstr(k, ==, "gtvtest");
    g_assert_cmpstr(v, ==, "Go Test");
    gchar *exp = expand_word("gtvtest");
    g_assert_cmpstr(exp, ==, "Go Test");
    g_free(exp);
    /* Update in place keeps the count. */
    g_assert_true(gtv_table_set(FALSE, "gtvtest", "Go Test 2"));
    g_assert_cmpuint(gtv_table_count(FALSE), ==, n0 + 1);
    /* Save round-trips through the user file. */
    GError *err = NULL;
    g_assert_true(gtv_table_save(FALSE, &err));
    g_assert_no_error(err);
    gchar *disk = NULL;
    g_assert_true(g_file_get_contents(upath, &disk, NULL, NULL));
    g_assert_nonnull(strstr(disk, "gtvtest=Go Test 2"));
    g_free(disk);
    /* Remove restores the literal word. */
    g_assert_true(gtv_table_remove(FALSE, "gtvtest"));
    g_assert_false(gtv_table_remove(FALSE, "gtvtest"));
    exp = expand_word("gtvtest");
    g_assert_cmpstr(exp, ==, "gtvtest");
    g_free(exp);
    g_assert_true(gtv_table_save(FALSE, &err));
    g_assert_no_error(err);
    /* Restore the user's file byte-for-byte. */
    if (had) g_assert_true(g_file_set_contents(upath, orig, -1, NULL));
    else g_remove(upath);
    g_free(orig);
    g_free(upath);
    gtv_tables_reload();
}

static void test_brand_migration(void) {
    gchar *base = g_dir_make_tmp("gotv-migrate-XXXXXX", NULL);
    g_assert_nonnull(base);
    gchar *legacy = g_build_filename(base, "gotiengviet", NULL);
    gchar *legacy_file = g_build_filename(legacy, "config", NULL);
    gchar *legacy_words = g_build_filename(legacy, "learned-words.txt", NULL);
    g_assert_cmpint(g_mkdir_with_parents(legacy, 0700), ==, 0);
    g_assert_true(g_file_set_contents(legacy_file, "old-config", -1, NULL));
    g_assert_true(g_file_set_contents(legacy_words, "custom-word", -1, NULL));
    gchar *next = g_build_filename(base, "gotv", NULL);
    g_assert_cmpint(g_mkdir_with_parents(next, 0700), ==, 0);
    gchar *next_file = g_build_filename(next, "config", NULL);
    g_assert_true(g_file_set_contents(next_file, "new-config", -1, NULL));
    gchar *imported = gtv_user_path(base, "learned-words.txt");
    gchar *contents = NULL;
    g_assert_true(g_file_get_contents(imported, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "custom-word"); g_free(contents);
    g_assert_true(g_file_get_contents(next_file, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "new-config"); g_free(contents);
    g_assert_true(g_file_test(legacy_words, G_FILE_TEST_EXISTS));
    /* A removed GoTV file must not be re-imported on the next lookup. */
    g_assert_cmpint(g_remove(imported), ==, 0);
    gchar *again = gtv_user_path(base, "learned-words.txt");
    g_assert_false(g_file_test(again, G_FILE_TEST_EXISTS));
    gchar *marker = g_build_filename(next, ".legacy-imported", NULL);
    g_remove(marker); g_remove(next_file); g_rmdir(next);
    g_remove(legacy_file); g_remove(legacy_words); g_rmdir(legacy); g_rmdir(base);
    g_free(marker); g_free(again); g_free(imported); g_free(next_file);
    g_free(next); g_free(legacy_words); g_free(legacy_file); g_free(legacy); g_free(base);
}

int main(int argc,char **argv) {
    /* Isolate user dirs: suggestions must come from shipped seeds only,
     * never from the developer's real ~/.config files. */
    gchar *cfghome = g_dir_make_tmp("gotv-test-home-XXXXXX", NULL);
    if (cfghome) { g_setenv("XDG_CONFIG_HOME", cfghome, TRUE); g_free(cfghome); }
    gchar *datahome = g_dir_make_tmp("gotv-test-data-XXXXXX", NULL);
    if (datahome) { g_setenv("XDG_DATA_HOME", datahome, TRUE); g_free(datahome); }
    g_test_init(&argc,&argv,NULL);gtv_init();
    g_test_add_func("/support/stateful",test_stateful);
    g_test_add_func("/support/brand-migration",test_brand_migration);
    g_test_add_func("/support/config",test_config);
    g_test_add_func("/support/update",test_update);
    g_test_add_func("/support/dict",test_dict);
    g_test_add_func("/support/suggest-combined",test_suggest_combined);
    g_test_add_func("/support/spelling",test_spelling);
    g_test_add_func("/support/macro-and-emoji",test_macro_and_emoji);
    g_test_add_func("/support/table-manage",test_table_manage);
    g_test_add_func("/support/learn",test_learn);
    g_test_add_func("/support/config-defaults",test_config_defaults);
    g_test_add_func("/algorithm/order-independence",test_order_independence);
    g_test_add_func("/algorithm/random-input",test_random_input);
    return g_test_run();
}
