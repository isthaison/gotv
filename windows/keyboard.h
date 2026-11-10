#ifndef GTV_KEYBOARD_H
#define GTV_KEYBOARD_H

#include <windows.h>
#include <glib.h>

/* One entry of the Win+Space switcher: a keyboard (TIP string) that belongs
 * to a language (BCP-47 tag). Flat, ordered exactly like the switcher shows
 * it: all of one language's keyboards in a row, then the next language. */
typedef struct {
    gchar *lang; /* "en-US", "vi", ... */
    gchar *tip;  /* "0409:00000409" or "042a:{CLSID}{profile}" */
} GtvKbd;

/* Ordered list of keyboards = the user's switcher order. */
typedef struct {
    GArray *items; /* GtvKbd */
} GtvKbdList;

/* A keyboard that could be added: what Windows calls it, and the TIP string
 * that selects it. `name` is the display name (already localized). */
typedef struct {
    gchar *tip;
    gchar *name;
} GtvKbdChoice;

GtvKbdList *gtv_kbd_list_new(void);
void gtv_kbd_list_free(GtvKbdList *list);
void gtv_kbd_list_clear(GtvKbdList *list);
void gtv_kbd_list_add(GtvKbdList *list, const gchar *lang, const gchar *tip);
/* Move the entry at `from` to `to` (this is the Win+Space order). */
void gtv_kbd_list_move(GtvKbdList *list, guint from, guint to);
void gtv_kbd_list_copy(GtvKbdList *dst, const GtvKbdList *src);
gboolean gtv_kbd_list_has(const GtvKbdList *list, const gchar *tip);
gboolean gtv_kbd_list_equals(const GtvKbdList *a, const GtvKbdList *b);
const GtvKbd *gtv_kbd_list_get(const GtvKbdList *list, guint index);

/* Display name for a TIP string, or NULL when it cannot be resolved. */
gchar *gtv_kbd_tip_name(const gchar *tip);

/* Every keyboard Windows offers for `langid`, sorted by name. `out` is a
 * GPtrArray of GtvKbdChoice*, already owning its elements. */
GPtrArray *gtv_kbd_choices_for_langid(LANGID langid);
void gtv_kbd_choices_free(GPtrArray *choices);

/* Language ids of the keyboards currently in the user's language list. */
GArray *gtv_kbd_current_langids(void);
/* Appends the distinct langids of `list` to `out` (no powershell, no I/O). */
void gtv_kbd_langids_of(const GtvKbdList *list, GArray *out);

/* The BCP-47 tag Windows uses for a langid ("042a" -> "vi-VN"): needed
 * because New-WinUserLanguageList takes a tag, never a raw langid. */
gchar *gtv_kbd_tag_for_langid(LANGID langid);
/* The tag `list` already uses for `langid`, so a new keyboard joins the
 * existing language instead of creating a second entry for the same one. */
gchar *gtv_kbd_existing_tag(const GtvKbdList *list, LANGID langid);

/* The setup GoTV wants on a fresh install: English keeps the plain US
 * keyboard, Vietnamese gets GoTV as its only keyboard. */
void gtv_kbd_list_set_default(GtvKbdList *list);

/* Read the user's real keyboard list (spawns powershell). Returns FALSE and
 * sets *error when the list could not be read. */
gboolean gtv_kbd_list_load(GtvKbdList *list, GError **error);

/* Write `list` back as the user's keyboard list. Returns FALSE and sets
 * *error when Windows rejected the new list or the read-back differs. */
gboolean gtv_kbd_list_apply(const GtvKbdList *list, GError **error);

/* TRUE once the user has saved their own list: the automatic first-run
 * setup then stays out of the way. */
gboolean gtv_kbd_is_custom(void);
void gtv_kbd_mark_custom(void);

/* Keyboard list manager dialog (opened from the tray menu / setup dialog). */
void gtv_keyboard_show(HWND parent);

/* Query whether the currently active foreground input layout is Vietnamese. */
BOOL gtv_kbd_is_vietnamese_active(void);

/* Toggle active keyboard layout / input language between GoTV and English. */
void gtv_kbd_toggle_active(void);

#endif /* GTV_KEYBOARD_H */