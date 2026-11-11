# Shared sources + version for all platforms (Linux/macOS/Windows).
# Single writer for the file list: both Makefile and Makefile.win include this.
# Release version itself lives in ./VERSION; gtv.sh bump rewrites it.
GTV_VERSION := $(shell cat $(dir $(lastword $(MAKEFILE_LIST)))../VERSION 2>/dev/null || echo 0.8.0)

ENGINE_SRCS = engine/charset.c engine/compose.c engine/config.c \
              engine/dict.c engine/engine.c engine/i18n.c engine/learn.c engine/macro.c \
              engine/phonology.c engine/promotion.c engine/spell.c \
              engine/suggest.c engine/telex.c engine/text.c engine/update.c engine/verdir.c engine/vni.c

# Windows tray app (C) + TSF text service (C++). Keep beside ENGINE_SRCS so
# Makefile.win never drifts from the file list again.
WIN_SRCS = windows/main.c windows/app.c windows/tray.c windows/tray_ai.c \
           windows/setup.c windows/data.c windows/startup.c windows/update.c windows/tsf_install.c windows/input_setup.c windows/keyboard.c windows/keyboard_ui.c windows/win_utf.c windows/suggest_overlay.c windows/hotkey.c
# TSF engine DLL (real text service, versioned) vs stable stub (registered once).
TSF_ENGINE_SRCS = windows/tsf/tsf_service.cpp windows/tsf/tsf_key_sink.cpp \
           windows/tsf/tsf_composition.cpp windows/tsf/tsf_factory.cpp \
           windows/tsf/tsf_suggest.cpp
TSF_STUB_SRCS = windows/tsf/stub_loader.cpp windows/tsf/tsf_register.cpp
TSF_SRCS = $(TSF_ENGINE_SRCS) $(TSF_STUB_SRCS)
