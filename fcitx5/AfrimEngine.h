#pragma once

#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include <string>
#include <vector>

#include "afrim_ffi.h"

// Forward declaration

/**
 * AfrimInputMethodEngine
 *
 * Implements fcitx5's InputMethodEngineV2 interface.
 */
class AfrimInputMethodEngine final : public fcitx::InputMethodEngineV2 {
public:
    explicit AfrimInputMethodEngine(fcitx::Instance *instance);
    ~AfrimInputMethodEngine() override;

    // InputMethodEngineV2
    std::vector<fcitx::InputMethodEntry> listInputMethods() override;

    // InputMethodEngine
    void activate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
    void deactivate(const fcitx::InputMethodEntry &entry,
                    fcitx::InputContextEvent &event) override;
    void keyEvent(const fcitx::InputMethodEntry &entry,
                  fcitx::KeyEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry,
               fcitx::InputContextEvent &event) override;
    void save() override {}

    /** Apply a newline-separated Rust command string to `ic`. */
    void applyCommands(fcitx::InputContext *ic, const char *cmds);

    /** Refresh the preedit text and candidate panel for `ic`. */
    void updateUI(fcitx::InputContext *ic);

    /** Raw pointer to the Rust engine (needed by AfrimCandidateWord). */
    AfrimEngine *rustEngine() noexcept { return engine_; }

private:
    fcitx::Instance *instance_;
    AfrimEngine *engine_ = nullptr;
    std::string output;

    /** Clear preedit + candidates and notify fcitx5. */
    void clearUI(fcitx::InputContext *ic);
};

// Addon factory
class AfrimEngineFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new AfrimInputMethodEngine(manager->instance());
    }
};
