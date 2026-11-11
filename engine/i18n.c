#include "i18n.h"

static GtvUiLang s_current_lang = GTV_LANG_VI;

void gtv_i18n_set_lang(GtvUiLang lang) {
    s_current_lang = lang;
}

GtvUiLang gtv_i18n_get_lang(void) {
    return s_current_lang;
}

const gchar *gtv_i18n_s(GtvUiLang lang, const gchar *vi, const gchar *en) {
    return (lang == GTV_LANG_EN) ? en : vi;
}

const gchar *gtv_i18n(const gchar *vi, const gchar *en) {
    return (s_current_lang == GTV_LANG_EN) ? en : vi;
}
