#ifndef GTV_SETUP_DATA_H
#define GTV_SETUP_DATA_H
#include <gtk/gtk.h>
/* Macro/emoji manager dialog. Saves the user file and reloads this
 * process's tables; active input-method engines are restarted on close. */
void data_show(GtkWindow *parent);
void data_restart_fcitx5_if_active(void);
#endif
