#ifndef GTV_I18N_H
#define GTV_I18N_H

#include "engine.h"

G_BEGIN_DECLS

void gtv_i18n_set_lang(GtvUiLang lang);
GtvUiLang gtv_i18n_get_lang(void);
const gchar *gtv_i18n_s(GtvUiLang lang, const gchar *vi, const gchar *en);
const gchar *gtv_i18n(const gchar *vi, const gchar *en);

G_END_DECLS

#endif
