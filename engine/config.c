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
    *config = (GtvConfig){.mode = GTV_TELEX, .modern = TRUE, .spellcheck = TRUE,
        .suggest_enabled = TRUE};
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
