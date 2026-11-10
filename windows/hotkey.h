#ifndef GTV_HOTKEY_H
#define GTV_HOTKEY_H

#include <windows.h>
#include "engine.h"

#define WM_GTV_TOGGLE_INPUT (WM_USER + 101)

#ifdef __cplusplus
extern "C" {
#endif

void gtv_hotkey_init(HWND hwnd, GtvSwitchKey key);
void gtv_hotkey_set_key(GtvSwitchKey key);
void gtv_hotkey_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* GTV_HOTKEY_H */
