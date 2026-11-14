#include "engine.h"
#include <glib/gstdio.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* Keep the legacy directory as a backup. Existing GoTV files always win.
 * A marker avoids re-importing files intentionally removed after migration. */
static gboolean copy_legacy_tree(const gchar *source, const gchar *dest) {
    GDir *dir = g_dir_open(source, 0, NULL);
    if (!dir) return FALSE;
    gboolean ok = g_mkdir_with_parents(dest, 0700) == 0;
    const gchar *entry;
    while (ok && (entry = g_dir_read_name(dir))) {
        gchar *src = g_build_filename(source, entry, NULL);
        gchar *dst = g_build_filename(dest, entry, NULL);
        if (g_file_test(src, G_FILE_TEST_IS_SYMLINK)) {
            /* Do not follow paths outside the user's legacy directory. */
        } else if (g_file_test(src, G_FILE_TEST_IS_DIR)) {
            ok = copy_legacy_tree(src, dst);
        } else if (!g_file_test(dst, G_FILE_TEST_EXISTS)) {
            GFile *from = g_file_new_for_path(src), *to = g_file_new_for_path(dst);
            GError *error = NULL;
            ok = g_file_copy(from, to, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
            if (!ok && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS)) ok = TRUE;
            g_clear_error(&error);
            g_object_unref(from); g_object_unref(to);
        }
        g_free(src); g_free(dst);
    }
    g_dir_close(dir);
    return ok;
}

G_LOCK_DEFINE_STATIC(user_migration);
gchar *gtv_user_path(const gchar *base, const gchar *name) {
#ifdef G_OS_WIN32
    const gchar *override = g_getenv("XDG_CONFIG_HOME");
    if (override && *override && base && strcmp(base, g_get_user_config_dir()) == 0) {
        base = override;
    }
#endif
    gchar *dir = g_build_filename(base, "gotv", NULL);
    gchar *legacy = g_build_filename(base, "gotiengviet", NULL);
    gchar *marker = g_build_filename(dir, ".legacy-imported", NULL);
    G_LOCK(user_migration);
    if (!g_file_test(marker, G_FILE_TEST_EXISTS) && g_file_test(legacy, G_FILE_TEST_IS_DIR)) {
        if (copy_legacy_tree(legacy, dir)) {
            if (!g_file_set_contents(marker, "GoTV\n", -1, NULL))
                g_warning("GoTV: cannot mark migration complete in %s", dir);
        } else {
            g_warning("GoTV: incomplete legacy import from %s; original files preserved", legacy);
        }
    }
    G_UNLOCK(user_migration);
    gchar *path = name ? g_build_filename(dir, name, NULL) : g_strdup(dir);
    g_free(marker); g_free(legacy); g_free(dir);
    return path;
}

#ifdef G_OS_WIN32
/* This DLL's own CLSID key (must match windows/tsf/tsf_defs.h). */
#define GTV_TSF_CLSID_KEY L"Software\\Classes\\CLSID\\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}\\InprocServer32"

/* Module/registry paths may contain non-ASCII (usernames!): stay in
 * UTF-16, convert once to UTF-8 for glib. */
static gchar *module_dir(HMODULE mod) {
    WCHAR wpath[MAX_PATH];
    if (!GetModuleFileNameW(mod, wpath, MAX_PATH)) return NULL;
    gchar *u8 = g_utf16_to_utf8(wpath, -1, NULL, NULL, NULL);
    if (!u8) return NULL;
    gchar *dir = g_path_get_dirname(u8);
    g_free(u8);
    return dir;
}
/* data/<name> next to dir, else one/two levels up (dev layouts). */
static gchar *bundled_below(const gchar *dir, const gchar *name) {
    gchar *bundled = g_build_filename(dir, "data", name, NULL);
    if (g_file_test(bundled, G_FILE_TEST_EXISTS)) return bundled;
    g_free(bundled);
    bundled = g_build_filename(dir, "..", "data", name, NULL);
    if (g_file_test(bundled, G_FILE_TEST_EXISTS)) return bundled;
    g_free(bundled);
    bundled = g_build_filename(dir, "..", "..", "data", name, NULL);
    if (g_file_test(bundled, G_FILE_TEST_EXISTS)) return bundled;
    g_free(bundled);
    return NULL;
}
#endif

/* Shared data-file resolution: $GTV_DATA_DIR (tests/dev override) →
 * user config dir → /usr/share/gotv (installed) → ./data. */
gchar *gtv_data_path(const gchar *name){
    const gchar *env = g_getenv("GTV_DATA_DIR");
    if(env && *env) return g_build_filename(env, name, NULL);
    gchar *user = gtv_user_path(g_get_user_config_dir(), name);
    if(g_file_test(user, G_FILE_TEST_EXISTS)) return user;
    g_free(user);
#ifdef G_OS_WIN32
    /* Check next to the running module (gtv_tsf.dll or gotv.exe) */
    {
        HMODULE hDll = NULL;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCWSTR)gtv_data_path, &hDll) || !hDll) {
            /* Versioned engine first, then the stable stub (same dir as
             * current.txt), then the legacy single-dir layout. */
            hDll = GetModuleHandleW(L"gtv_engine.dll");
            if (!hDll) hDll = GetModuleHandleW(L"gtv_tsf.dll");
        }
        if (hDll) {
            gchar *dlldir = module_dir(hDll);
            if (dlldir) {
                gchar *bundled = bundled_below(dlldir, name);
                g_free(dlldir);
                if (bundled) return bundled;
            }
        }
    }
    /* Portable install: data/ next to the .exe (when running gotv.exe) */
    {
        gchar *exedir = module_dir(NULL);
        if (exedir) {
            gchar *bundled = bundled_below(exedir, name);
            g_free(exedir);
            if (bundled) return bundled;
        }
    }
    /* Registry lookup for registered InprocServer32 directory */
    {
        HKEY hKey = NULL;
        LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, GTV_TSF_CLSID_KEY, 0, KEY_READ, &hKey);
        if (rc != ERROR_SUCCESS)
            rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, GTV_TSF_CLSID_KEY, 0, KEY_READ, &hKey);
        if (rc == ERROR_SUCCESS) {
            WCHAR wreg[MAX_PATH] = {0};
            DWORD size = sizeof(wreg);
            if (RegQueryValueExW(hKey, NULL, NULL, NULL, (LPBYTE)wreg, &size) == ERROR_SUCCESS && *wreg) {
                gchar *ureg = g_utf16_to_utf8(wreg, -1, NULL, NULL, NULL);
                if (ureg) {
                    gchar *regdir = g_path_get_dirname(ureg);
                    g_free(ureg);
                    gchar *bundled = bundled_below(regdir, name);
                    g_free(regdir);
                    RegCloseKey(hKey);
                    if (bundled) return bundled;
                }
            }
            RegCloseKey(hKey);
        }
    }
#endif
    gchar *system = g_build_filename("/usr", "share", "gotv", name, NULL);
    if(g_file_test(system, G_FILE_TEST_EXISTS)) return system;
    g_free(system);
    return g_build_filename("data", name, NULL);
}

static void read_bool(GKeyFile *file, const gchar *group, const gchar *key, gboolean *value) {
    gchar *next = g_key_file_get_string(file,group,key,NULL);
    if (!next) return;
    g_strstrip(next);
    if (!g_ascii_strcasecmp(next,"true") || !strcmp(next,"1")) *value=TRUE;
    if (!g_ascii_strcasecmp(next,"false") || !strcmp(next,"0")) *value=FALSE;
    g_free(next);
}
/* Suggestions used to live behind an [ai] section (Ollama provider).
 * The dictionary backend reads [suggest] enable; legacy [ai] enable /
 * provider values still migrate (provider=ollama implies on) so existing
 * user configs keep working. */
static void overlay_suggest(GKeyFile *file, GtvConfig *config) {
    gchar *dicts = g_key_file_get_string(file, "suggest", "dicts", NULL);
    if (!dicts) dicts = g_key_file_get_string(file, "suggest", "langs", NULL);
    if (dicts) {
        g_strstrip(dicts);
        if (*dicts) g_strlcpy(config->dicts, dicts, sizeof(config->dicts));
        g_free(dicts);
    }
    if (g_key_file_has_key(file, "suggest", "enable", NULL)) {
        read_bool(file, "suggest", "enable", &config->suggest_enabled);
        return;
    }
    if (g_key_file_has_key(file, "ai", "enable", NULL)) {
        read_bool(file, "ai", "enable", &config->suggest_enabled);
        return;
    }
    gchar *prov = g_key_file_get_string(file, "ai", "provider", NULL);
    if (prov) {
        g_strstrip(prov);
        if (!g_strcmp0(prov, "ollama")) config->suggest_enabled = TRUE;
        if (!g_strcmp0(prov, "rule")) config->suggest_enabled = FALSE;
        g_free(prov);
    }
}
static void overlay_file(GKeyFile *file, GtvConfig *config, gboolean input) {
    if (input) {
        gchar *method = g_key_file_get_string(file, "input", "method", NULL);
        config->mode = method && !g_ascii_strcasecmp(method, "vni") ? GTV_VNI : GTV_TELEX;
        g_free(method);
        gchar *sw = g_key_file_get_string(file, "input", "switch_key", NULL);
        if (sw) {
            config->switch_key = (!g_ascii_strcasecmp(sw, "alt_z") || !g_ascii_strcasecmp(sw, "altz"))
                ? GTV_SWITCH_ALT_Z : GTV_SWITCH_CTRL_SHIFT;
            g_free(sw);
        }
        read_bool(file, "input", "modern", &config->modern);
        read_bool(file, "input", "spellcheck", &config->spellcheck);
    }
    overlay_suggest(file, config);
}
static void overlay_path(GtvConfig *config, const gchar *path, gboolean input) {
    GKeyFile *file = g_key_file_new();
    if (g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL))
        overlay_file(file, config, input);
    g_key_file_unref(file);
}
void gtv_config_load(GtvConfig *config, const gchar *directory) {
    *config = (GtvConfig){.mode = GTV_TELEX, .switch_key = GTV_SWITCH_CTRL_SHIFT,
        .modern = TRUE, .spellcheck = FALSE, .suggest_enabled = TRUE};
    g_strlcpy(config->dicts, "vi,tsql,c,csharp", sizeof(config->dicts));
    /* Shipped defaults first (data/config), then user files. */
    gchar *path = gtv_data_path("config");
    overlay_path(config, path, TRUE);
    g_free(path);
    path = g_build_filename(directory, "config", NULL);
    overlay_path(config, path, TRUE);
    g_free(path);
    gtv_dict_set_langs(config->dicts);
    g_strlcpy(config->dicts, gtv_dict_get_langs(), sizeof(config->dicts));
}

gboolean gtv_config_save(const GtvConfig *config, const gchar *directory, GError **error) {
    if (g_mkdir_with_parents(directory, 0755) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot create %s: %s", directory, g_strerror(errno));
        return FALSE;
    }
    GKeyFile *file = g_key_file_new();
    gchar *path = g_build_filename(directory, "config", NULL);
    g_key_file_load_from_file(file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_string(file, "input", "method", config->mode == GTV_VNI ? "vni" : "telex");
    g_key_file_set_string(file, "input", "switch_key", config->switch_key == GTV_SWITCH_ALT_Z ? "alt_z" : "ctrl_shift");
    g_key_file_set_boolean(file, "input", "modern", config->modern);
    g_key_file_set_boolean(file, "input", "spellcheck", config->spellcheck);
    g_key_file_set_string(file, "input", "charset", "unicode");
    g_key_file_set_boolean(file, "suggest", "enable", config->suggest_enabled);
    if (config->dicts[0]) {
        gtv_dict_set_langs(config->dicts);
        g_key_file_set_string(file, "suggest", "dicts", gtv_dict_get_langs());
    }
    gboolean ok = g_key_file_save_to_file(file, path, error);
    g_free(path);
    g_key_file_unref(file);
    return ok;
}
void gtv_config_clear(GtvConfig *config) {
    *config = (GtvConfig){0};
}

void gtv_app_dict_rule_free(gpointer p) {
    GtvAppDictRule *r = p;
    if (!r) return;
    g_free(r->app_id);
    g_free(r->dicts);
    g_free(r);
}

gchar *gtv_app_dict_normalize_id(const gchar *app_id) {
    if (!app_id) return NULL;
    gchar *tmp = g_strdup(app_id);
    g_strstrip(tmp);
    if (!*tmp) {
        g_free(tmp);
        return NULL;
    }
    const gchar *slash = strrchr(tmp, '/');
    const gchar *bslash = strrchr(tmp, '\\');
    const gchar *base = tmp;
    if (slash && bslash)
        base = (slash > bslash ? slash : bslash) + 1;
    else if (slash)
        base = slash + 1;
    else if (bslash)
        base = bslash + 1;
    if (!*base) {
        g_free(tmp);
        return NULL;
    }
    gchar *lower = g_ascii_strdown(base, -1);
    g_free(tmp);
    return lower;
}

static void overlay_app_dicts_file(GHashTable *map, const gchar *path) {
    if (!map || !path) return;
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL) &&
        g_key_file_has_group(kf, "app_dicts")) {
        gsize nkeys = 0;
        gchar **keys = g_key_file_get_keys(kf, "app_dicts", &nkeys, NULL);
        for (gsize i = 0; keys && keys[i]; i++) {
            gchar *norm_id = gtv_app_dict_normalize_id(keys[i]);
            gchar *val = g_key_file_get_string(kf, "app_dicts", keys[i], NULL);
            if (norm_id && val) {
                g_strstrip(val);
                if (*val) {
                    g_hash_table_replace(map, norm_id, val);
                    norm_id = NULL;
                    val = NULL;
                }
            }
            g_free(norm_id);
            g_free(val);
        }
        g_strfreev(keys);
    }
    g_key_file_unref(kf);
}

static gint app_rule_cmp(gconstpointer a, gconstpointer b) {
    const GtvAppDictRule *ra = *(const GtvAppDictRule * const *)a;
    const GtvAppDictRule *rb = *(const GtvAppDictRule * const *)b;
    return g_strcmp0(ra->app_id, rb->app_id);
}

GPtrArray *gtv_app_dicts_load(const gchar *directory) {
    GPtrArray *rules = g_ptr_array_new_with_free_func(gtv_app_dict_rule_free);
    GHashTable *map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gchar *seed = gtv_data_path("config");
    overlay_app_dicts_file(map, seed);
    g_free(seed);

    gchar *dir = directory ? g_strdup(directory) : gtv_user_path(g_get_user_config_dir(), NULL);
    gchar *user_cfg = g_build_filename(dir, "config", NULL);
    overlay_app_dicts_file(map, user_cfg);
    g_free(user_cfg);
    g_free(dir);

    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, map);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        GtvAppDictRule *r = g_new0(GtvAppDictRule, 1);
        r->app_id = g_strdup((const gchar *)k);
        r->dicts = g_strdup((const gchar *)v);
        g_ptr_array_add(rules, r);
    }
    g_hash_table_unref(map);
    g_ptr_array_sort(rules, app_rule_cmp);
    return rules;
}

gboolean gtv_app_dicts_save(const GPtrArray *rules, const gchar *directory, GError **error) {
    gchar *dir = directory ? g_strdup(directory) : gtv_user_path(g_get_user_config_dir(), NULL);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create %s: %s", dir, g_strerror(errno));
        g_free(dir);
        return FALSE;
    }
    GKeyFile *file = g_key_file_new();
    gchar *path = g_build_filename(dir, "config", NULL);
    g_key_file_load_from_file(file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    if (g_key_file_has_group(file, "app_dicts")) {
        g_key_file_remove_group(file, "app_dicts", NULL);
    }
    if (rules) {
        for (guint i = 0; i < rules->len; i++) {
            const GtvAppDictRule *r = rules->pdata[i];
            if (!r || !r->app_id || !r->dicts) continue;
            gchar *norm_id = gtv_app_dict_normalize_id(r->app_id);
            gchar *dicts = g_strdup(r->dicts);
            g_strstrip(dicts);
            if (norm_id && *dicts) {
                g_key_file_set_string(file, "app_dicts", norm_id, dicts);
            }
            g_free(norm_id);
            g_free(dicts);
        }
    }
    gboolean ok = g_key_file_save_to_file(file, path, error);
    g_free(path);
    g_key_file_unref(file);
    g_free(dir);
    return ok;
}

G_LOCK_DEFINE_STATIC(app_dicts_cache_lock);
static GHashTable *app_dicts_cache = NULL;
static gchar *app_dicts_sig = NULL;

static gchar *app_dicts_signature(const gchar *user_cfg_path) {
    GStatBuf st;
    if (user_cfg_path && g_stat(user_cfg_path, &st) == 0) {
        return g_strdup_printf("%s:%" G_GINT64_FORMAT ":%" G_GOFFSET_FORMAT,
                               user_cfg_path, (gint64)st.st_mtime, (goffset)st.st_size);
    }
    return g_strdup_printf("%s:missing", user_cfg_path ? user_cfg_path : "");
}

const gchar *gtv_config_resolve_dicts(const GtvConfig *config, const gchar *app_id,
                                      const gchar *directory, gchar *out, gsize out_size) {
    if (!out || out_size == 0) return "";
    const gchar *fallback = (config && config->dicts[0]) ? config->dicts : "vi";
    g_strlcpy(out, fallback, out_size);

    gchar *norm_id = gtv_app_dict_normalize_id(app_id);
    if (!norm_id) return out;

    gchar *dir = directory ? g_strdup(directory) : gtv_user_path(g_get_user_config_dir(), NULL);
    gchar *user_cfg = g_build_filename(dir, "config", NULL);
    gchar *sig = app_dicts_signature(user_cfg);

    G_LOCK(app_dicts_cache_lock);
    if (!app_dicts_cache || g_strcmp0(app_dicts_sig, sig) != 0) {
        if (app_dicts_cache) g_hash_table_unref(app_dicts_cache);
        g_free(app_dicts_sig);
        app_dicts_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        app_dicts_sig = g_strdup(sig);
        gchar *seed = gtv_data_path("config");
        overlay_app_dicts_file(app_dicts_cache, seed);
        g_free(seed);
        overlay_app_dicts_file(app_dicts_cache, user_cfg);
    }
    const gchar *hit = g_hash_table_lookup(app_dicts_cache, norm_id);
    if (!hit) {
        if (g_str_has_suffix(norm_id, ".exe")) {
            gchar *no_exe = g_strndup(norm_id, strlen(norm_id) - 4);
            hit = g_hash_table_lookup(app_dicts_cache, no_exe);
            g_free(no_exe);
        } else if (g_str_has_suffix(norm_id, ".desktop")) {
            gchar *no_dt = g_strndup(norm_id, strlen(norm_id) - 8);
            hit = g_hash_table_lookup(app_dicts_cache, no_dt);
            g_free(no_dt);
        } else {
            gchar *with_exe = g_strconcat(norm_id, ".exe", NULL);
            hit = g_hash_table_lookup(app_dicts_cache, with_exe);
            g_free(with_exe);
            if (!hit) {
                gchar *with_dt = g_strconcat(norm_id, ".desktop", NULL);
                hit = g_hash_table_lookup(app_dicts_cache, with_dt);
                g_free(with_dt);
            }
        }
    }
    if (hit && *hit) {
        g_strlcpy(out, hit, out_size);
    }
    G_UNLOCK(app_dicts_cache_lock);

    g_free(sig);
    g_free(user_cfg);
    g_free(dir);
    g_free(norm_id);
    return out;
}
