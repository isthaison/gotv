#include "../../engine/engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    GtvConfig config;
    gchar *directory = gtv_user_path(g_get_user_config_dir(), NULL);
    gtv_config_load(&config, directory);
    g_free(directory);
    /* A deterministic transform mode is useful for scripts and regression tests. */
    if (argc == 4 && !strcmp(argv[1], "--transform")) {
        if (strcmp(argv[2], "telex") && strcmp(argv[2], "vni")) return 1;
        gchar *out = gtv_transform(argv[3], !strcmp(argv[2], "vni") ? GTV_VNI : GTV_TELEX, TRUE);
        if (!out) return 1;
        puts(out); g_free(out); gtv_config_clear(&config);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "--dicts")) {
        if (argc == 2) {
            guint n = gtv_dict_catalog_count();
            for (guint i = 0; i < n; i++) {
                const GtvDictInfo *info = gtv_dict_catalog_get(i);
                if (!info) continue;
                gchar *tag = gtv_dict_pack_tag(info->id);
                printf("%s\t%u words\t(%s)\t[%s]\n", info->id,
                    gtv_dict_pack_count(info->id), info->title,
                    (tag && *tag) ? tag : "seed");
                g_free(tag);
            }
            gtv_config_clear(&config);
            return 0;
        }
        gtv_dict_set_langs(argv[2]);
        g_strlcpy(config.dicts, gtv_dict_get_langs(), sizeof(config.dicts));
        argc -= 2;
        argv += 2;
    }
    if (argc == 3 && !strcmp(argv[1], "--suggest")) {
        GPtrArray *out = gtv_suggest_combined(&config, "", argv[2], FALSE);
        for(guint i=0;i<out->len;i++) puts(g_ptr_array_index(out,i));
        g_ptr_array_unref(out);gtv_config_clear(&config);return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--correct")) {
        GPtrArray *out = gtv_suggest_combined(&config, "", argv[2], TRUE);
        for(guint i=0;i<out->len;i++) puts(g_ptr_array_index(out,i));
        g_ptr_array_unref(out);gtv_config_clear(&config);return 0;
    }
    if (argc > 1) {
        puts("GoTV Demo (C)\nUsage: gotv-demo [--dicts [LIST]] [--transform telex|vni TEXT] [--suggest PREFIX] [--correct WORD]");
        gtv_config_clear(&config);
        return strcmp(argv[1], "--help") != 0;
    }
    GtvEngine *engine = gtv_engine_new(&config);
    puts("GoTV Demo (C) — mode telex, mode vni, quit");
    gchar *line = NULL;
    size_t size = 0;
    while (TRUE) {
        printf("[%s] Nhập chuỗi > ", engine->mode == GTV_VNI ? "VNI" : "Telex");
        fflush(stdout);
        if (getline(&line, &size, stdin) < 0) break;
        g_strchomp(line);
        if (!strcmp(line, "quit") || !strcmp(line, "exit")) break;
        if (!strcmp(line, "mode telex")) { engine->mode = GTV_TELEX; continue; }
        if (!strcmp(line, "mode vni")) { engine->mode = GTV_VNI; continue; }
        if (!g_utf8_validate(line, -1, NULL)) { g_printerr("UTF-8 không hợp lệ\n"); continue; }
        gtv_engine_reset(engine);
        GString *out = g_string_new("");
        for (const gchar *p = line; *p; p = g_utf8_next_char(p)) {
            gchar *commit = gtv_engine_process(engine, g_utf8_get_char(p), NULL);
            if (commit) { g_string_append(out, commit); g_free(commit); }
        }
        gchar *tail = gtv_engine_buffer(engine);
        g_string_append(out, tail);
        printf("Kết quả: %s\n", out->str);
        g_free(tail); g_string_free(out, TRUE);
    }
    free(line);
    gtv_engine_free(engine);
    gtv_config_clear(&config);
    return 0;
}
