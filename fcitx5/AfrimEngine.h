#pragma once

#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include <string>
#include <vector>

#include "afrim_ffi.h"

// ── Forward declaration
// ────────────────────────────────────────────────────── class
// AfrimCandidateWord;

// ── Main engine class
// ────────────────────────────────────────────────────────

/**
 * AfrimInputMethodEngine
 *
 * Implements fcitx5's InputMethodEngineV2 interface backed by the Rust
 * afrim-fcitx5 static library via the C FFI layer in afrim_ffi.h.
 *
 * Design summary
 * ──────────────
 * • One `AfrimEngine*` is shared for the lifetime of the addon.  Its
 *   state is reset whenever the active InputContext changes (deactivate /
 *   reset events).
 *
 * • A preedit-buffer approach is used: raw keystrokes are NEVER forwarded to
 *   the application.  Instead they appear in the inline preedit string
 *   (underlined).  When a sequence matches, the preedit is cleared and the
 *   translated text is committed to the application via commitString().
 *
 * • Candidate words appear in the standard fcitx5 candidate panel.  Selecting
 *   one calls commit_candidate() on the Rust engine.
 */
class AfrimInputMethodEngine final : public fcitx::InputMethodEngineV2 {
public:
    explicit AfrimInputMethodEngine(fcitx::Instance *instance);
    ~AfrimInputMethodEngine() override;

    // ── InputMethodEngineV2 ──────────────────────────────────────────────
    std::vector<fcitx::InputMethodEntry> listInputMethods() override;

    // ── InputMethodEngine ────────────────────────────────────────────────
    void activate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
    void deactivate(const fcitx::InputMethodEntry &entry,
                    fcitx::InputContextEvent &event) override;
    void keyEvent(const fcitx::InputMethodEntry &entry,
                  fcitx::KeyEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry,
               fcitx::InputContextEvent &event) override;
    void save() override {}

    // ── Internal helpers (used by AfrimCandidateWord) ────────────────────

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

// ── Addon factory
// ────────────────────────────────────────────────────────────

class AfrimEngineFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new AfrimInputMethodEngine(manager->instance());
    }
};
