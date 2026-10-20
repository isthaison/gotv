#ifndef GTV_ENGINE_H
#define GTV_ENGINE_H
#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* Returned strings and arrays belong to the caller (g_free/g_ptr_array_unref). */
typedef enum { GTV_TELEX, GTV_VNI } GtvMode;
typedef struct {
    GtvMode mode;
    gboolean modern, spellcheck, suggest_enabled;
    gchar dicts[128];
} GtvConfig;
typedef struct {
    GtvMode mode;
    gboolean modern, spellcheck;
    GArray *buffer;
} GtvEngine;

void gtv_init(void);
gchar *gtv_transform(const gchar *input, GtvMode mode, gboolean modern);
GtvEngine *gtv_engine_new(const GtvConfig *config);
void gtv_engine_free(GtvEngine *engine);
void gtv_engine_reset(GtvEngine *engine);
gchar *gtv_engine_buffer(const GtvEngine *engine);
/* NULL means composing; otherwise returns committed text and clears the buffer. */
gchar *gtv_engine_process(GtvEngine *engine, gunichar key, guint *backspaces);
gchar *expand_macro(const char *word);
gchar *expand_emoji(const char *word);
void gtv_config_load(GtvConfig *config, const gchar *directory);
gboolean gtv_config_save(const GtvConfig *config, const gchar *directory, GError **error);
void gtv_config_clear(GtvConfig *config);
/* GoTV user path; copies missing legacy files without removing the originals. */
gchar *gtv_user_path(const gchar *base, const gchar *name);
/* Offline dictionary suggestions (bundled dict-*.txt seeds, refreshed
 * from GitHub releases, plus the user's learned vocabulary). Multiple
 * language/keyword packs (vi, tsql, c, csharp, en) can be selected. */
typedef struct {
    const gchar *id;
    const gchar *filename;
    const gchar *title;
} GtvDictInfo;

gboolean gtv_dict_available(const GtvConfig *config);
guint gtv_dict_count(void);
const gchar *gtv_dict_tag(void);
gboolean gtv_dict_update_check(const gchar *repo);
guint gtv_dict_catalog_count(void);
const GtvDictInfo *gtv_dict_catalog_get(guint index);
const GtvDictInfo *gtv_dict_catalog_find(const gchar *id);
void gtv_dict_set_langs(const gchar *langs);
const gchar *gtv_dict_get_langs(void);
gboolean gtv_dict_lang_enabled(const gchar *langs, const gchar *id);
gchar *gtv_dict_lang_toggle(const gchar *langs, const gchar *id, gboolean enable);
guint gtv_dict_pack_count(const gchar *id);
gchar *gtv_dict_pack_tag(const gchar *id);
gboolean gtv_dict_update_pack(const gchar *repo, const gchar *id);
GPtrArray *gtv_suggest_combined(const GtvConfig *config, const gchar *context, const gchar *preedit, gboolean bad);
void gtv_suggest_combined_async(const GtvConfig *config, const gchar *context, const gchar *preedit, gboolean bad,
                               GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
GPtrArray *gtv_suggest_combined_finish(GAsyncResult *res, GError **error);
/* Self-update against GitHub releases. Check/download need curl and network;
 * parse/compare/throttle are offline. Out strings belong to the caller. */
typedef enum { GTV_UPDATE_AVAILABLE, GTV_UPDATE_CURRENT, GTV_UPDATE_ERROR } GtvUpdateStatus;
#define GTV_GITHUB_REPO "isthaison/gotv"
gint gtv_version_compare(const gchar *a, const gchar *b);
gchar *gtv_update_asset_name(const gchar *version);
/* Platform asset matcher: asset_name from the release payload, tag_version
 * stripped of any leading v. Returns TRUE for the downloadable package. */
typedef gboolean (*GtvAssetMatch)(const gchar *asset_name, const gchar *tag_version, gpointer user_data);
GtvUpdateStatus gtv_update_parse_release_full(const gchar *json, const gchar *current_version,
                                              GtvAssetMatch match, gpointer match_data,
                                              gchar **out_tag, gchar **out_asset_url);
GtvUpdateStatus gtv_update_check_full(const gchar *repo, const gchar *current_version,
                                      GtvAssetMatch match, gpointer match_data,
                                      gchar **out_tag, gchar **out_asset_url);
GtvUpdateStatus gtv_update_parse_release(const gchar *json, const gchar *current_version,
                                         gchar **out_tag, gchar **out_asset_url);
GtvUpdateStatus gtv_update_check(const gchar *repo, const gchar *current_version,
                                 gchar **out_tag, gchar **out_asset_url);
gboolean gtv_update_download(const gchar *url, const gchar *dest_path);
/* Borrowed static string describing the last check failure (curl exit code,
 * timeout, empty body, payload stage). Never NULL. For update.log only. */
const gchar *gtv_update_last_error(void);
gboolean gtv_update_should_autocheck(void);
void gtv_update_mark_checked(void);

G_END_DECLS

#endif
