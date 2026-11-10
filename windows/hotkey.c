#include "hotkey.h"

#define ID_HOTKEY_ALT_Z 1002

static HHOOK s_hHook = NULL;
static HWND s_target_hwnd = NULL;
static GtvSwitchKey s_current_key = GTV_SWITCH_CTRL_SHIFT;

static BOOL s_ctrl_down = FALSE;
static BOOL s_shift_down = FALSE;
static BOOL s_other_key = FALSE;

static void trigger_toggle(void) {
    if (s_target_hwnd) {
        PostMessage(s_target_hwnd, WM_GTV_TOGGLE_INPUT, 0, 0);
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_current_key == GTV_SWITCH_CTRL_SHIFT) {
        KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *)lParam;
        DWORD vk = p->vkCode;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) {
                s_ctrl_down = TRUE;
            } else if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT) {
                s_shift_down = TRUE;
            } else {
                s_other_key = TRUE;
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) {
                if (s_ctrl_down && s_shift_down && !s_other_key) {
                    trigger_toggle();
                }
                s_ctrl_down = FALSE;
                if (!s_shift_down) s_other_key = FALSE;
            } else if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT) {
                if (s_ctrl_down && s_shift_down && !s_other_key) {
                    trigger_toggle();
                }
                s_shift_down = FALSE;
                if (!s_ctrl_down) s_other_key = FALSE;
            } else {
                s_other_key = TRUE;
            }
        }
    }
    return CallNextHookEx(s_hHook, nCode, wParam, lParam);
}

void gtv_hotkey_set_key(GtvSwitchKey key) {
    s_current_key = key;
    s_ctrl_down = FALSE;
    s_shift_down = FALSE;
    s_other_key = FALSE;

    if (s_current_key == GTV_SWITCH_ALT_Z) {
        if (s_hHook) {
            UnhookWindowsHookEx(s_hHook);
            s_hHook = NULL;
        }
        if (s_target_hwnd) {
            RegisterHotKey(s_target_hwnd, ID_HOTKEY_ALT_Z, MOD_ALT, 'Z');
        }
    } else {
        if (s_target_hwnd) {
            UnregisterHotKey(s_target_hwnd, ID_HOTKEY_ALT_Z);
        }
        if (!s_hHook) {
            s_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                        GetModuleHandleW(NULL), 0);
        }
    }
}

void gtv_hotkey_init(HWND hwnd, GtvSwitchKey key) {
    s_target_hwnd = hwnd;
    gtv_hotkey_set_key(key);
}

void gtv_hotkey_cleanup(void) {
    if (s_hHook) {
        UnhookWindowsHookEx(s_hHook);
        s_hHook = NULL;
    }
    if (s_target_hwnd) {
        UnregisterHotKey(s_target_hwnd, ID_HOTKEY_ALT_Z);
        s_target_hwnd = NULL;
    }
}
