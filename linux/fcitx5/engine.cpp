#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputpanel.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include <ctime>
#include <memory>
#include <vector>

extern "C" {
#include "engine.h"
}

#include <glib/gstdio.h>

namespace fcitx {

class GoTVState final : public InputContextProperty {
public:
    explicit GoTVState(InputContext *inputContext);
    ~GoTVState() override;

    void reloadConfig();
    void publishPreedit();
    void clear();
    void commit();
    bool hasPreedit() const;

    GtvEngine *composer() const { return composer_.get(); }

private:
    InputContext *inputContext_;
    GtvConfig config_{};
    std::unique_ptr<GtvEngine, decltype(&gtv_engine_free)> composer_{
        nullptr, &gtv_engine_free};
    std::time_t configMtime_ = 0;
    bool configLoaded_ = false;
};

class GoTVEngine final : public InputMethodEngine {
public:
    explicit GoTVEngine(Instance *instance)
        : instance_(instance),
          factory_([](InputContext &ic) {
              return new GoTVState(&ic);
          }) {
        instance_->inputContextManager().registerProperty("gotv", &factory_);
        gtv_init();
    }

    std::vector<InputMethodEntry> listInputMethods() override {
        std::vector<InputMethodEntry> methods;
        methods.emplace_back("gotv", "GoTV", "vi", "gotv");
        methods.back().setIcon("gotv").setLabel("TV");
        return methods;
    }

    void activate(const InputMethodEntry &, InputContextEvent &event) override {
        if (auto *ic = event.inputContext()) {
            state(ic)->reloadConfig();
        }
    }

    void reset(const InputMethodEntry &, InputContextEvent &event) override {
        if (auto *ic = event.inputContext()) {
            state(ic)->clear();
        }
    }

    void keyEvent(const InputMethodEntry &, KeyEvent &event) override {
        if (event.isRelease()) {
            return;
        }

        InputContext *ic = event.inputContext();
        if (!ic) {
            return;
        }

        GoTVState *current = state(ic);
        current->reloadConfig();
        const auto flags = ic->capabilityFlags();
        if (flags.test(CapabilityFlag::Password) ||
            flags.test(CapabilityFlag::Sensitive) ||
            flags.test(CapabilityFlag::Disable)) {
            current->clear();
            return;
        }

        const Key key = event.key();
        const KeySym sym = key.sym();

        const auto states = key.states();
        if (states.test(KeyState::Ctrl) || states.test(KeyState::Alt) ||
            states.test(KeyState::Super) || states.test(KeyState::Super2) ||
            states.test(KeyState::Hyper) || states.test(KeyState::Hyper2) ||
            states.test(KeyState::Meta)) {
            current->commit();
            return;
        }

        if (sym == FcitxKey_Escape) {
            if (current->hasPreedit()) {
                current->clear();
                event.filterAndAccept();
            }
            return;
        }

        if (sym == FcitxKey_BackSpace) {
            if (current->hasPreedit()) {
                gtv_engine_process(current->composer(), '\b', nullptr);
                current->publishPreedit();
                event.filterAndAccept();
            }
            return;
        }

        if (sym == FcitxKey_Tab || sym == FcitxKey_ISO_Left_Tab) {
            gchar *expanded = gtv_engine_process(current->composer(), '\t', nullptr);
            if (expanded) {
                ic->commitString(expanded);
                g_free(expanded);
                current->publishPreedit();
                event.filterAndAccept();
            } else {
                current->commit();
            }
            return;
        }

        if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
            if (current->hasPreedit()) {
                current->commit();
                event.filterAndAccept();
            }
            return;
        }

        const uint32_t unicode = Key::keySymToUnicode(sym);
        if (unicode >= 0x20 && g_unichar_isprint(unicode)) {
            gchar *committed =
                gtv_engine_process(current->composer(), unicode, nullptr);
            if (committed) {
                ic->commitString(committed);
                g_free(committed);
            }
            current->publishPreedit();
            event.filterAndAccept();
            return;
        }

        current->commit();
    }

private:
    GoTVState *state(InputContext *ic) {
        return ic->propertyFor(&factory_);
    }

    Instance *instance_;
    FactoryFor<GoTVState> factory_;
};

GoTVState::GoTVState(InputContext *inputContext)
    : inputContext_(inputContext) {
    reloadConfig();
    composer_.reset(gtv_engine_new(&config_));
}

GoTVState::~GoTVState() {
    gtv_config_clear(&config_);
}

void GoTVState::reloadConfig() {
    gchar *path = gtv_user_path(g_get_user_config_dir(), "config");
    GStatBuf statBuffer{};
    const std::time_t mtime =
        g_stat(path, &statBuffer) == 0 ? statBuffer.st_mtime : 0;
    g_free(path);
    if (configLoaded_ && mtime == configMtime_) {
        return;
    }

    configMtime_ = mtime;
    configLoaded_ = true;
    gchar *directory = gtv_user_path(g_get_user_config_dir(), nullptr);
    gtv_config_load(&config_, directory);
    g_free(directory);
    if (composer_) {
        composer_->mode = config_.mode;
        composer_->modern = config_.modern;
        composer_->spellcheck = config_.spellcheck;
    }
}

bool GoTVState::hasPreedit() const {
    gchar *text = gtv_engine_buffer(composer_.get());
    const bool present = text && *text;
    g_free(text);
    return present;
}

void GoTVState::publishPreedit() {
    gchar *text = gtv_engine_buffer(composer_.get());
    Text preedit(text);
    preedit.setCursor(preedit.textLength());
    inputContext_->inputPanel().setClientPreedit(preedit);
    inputContext_->updatePreedit();
    g_free(text);
}

void GoTVState::clear() {
    gtv_engine_reset(composer_.get());
    publishPreedit();
}

void GoTVState::commit() {
    gchar *text = gtv_engine_buffer(composer_.get());
    if (text && *text) {
        inputContext_->commitString(text);
    }
    g_free(text);
    clear();
}

class GoTVAddonFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new GoTVEngine(manager->instance());
    }
};

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(gotv, fcitx::GoTVAddonFactory);
