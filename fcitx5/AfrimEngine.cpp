#include "AfrimEngine.h"

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ── Anonymous namespace ──────────────────────────────────────────────────────
namespace {

// ── AfrimCandidateWord ───────────────────────────────────────────────────────
/**
 * A candidate word that calls back into AfrimInputMethodEngine when selected.
 */
class AfrimCandidateWord final : public fcitx::CandidateWord {
public:
    AfrimCandidateWord(AfrimInputMethodEngine *engine, std::string text)
        : fcitx::CandidateWord(fcitx::Text(text)),
          engine_(engine),
          text_(std::move(text)) {}

    void select(fcitx::InputContext *ic) const override {
        char *cmds =
            afrim_engine_commit_candidate(engine_->rustEngine(), text_.c_str());
        if (cmds) {
            engine_->applyCommands(ic, cmds);
            afrim_string_free(cmds);
        }
        engine_->updateUI(ic);
    }

private:
    AfrimInputMethodEngine *engine_;
    std::string             text_;
};

// ── Helpers ──────────────────────────────────────────────────────────────────

/**
 * Determine the UTF-8 string produced by `key` from fcitx5's key event.
 *
 * For printable ASCII and Unicode (keysym >= 0x01000000) we derive the UTF-8
 * sequence directly from the keysym value rather than relying on
 * XLookupString, which would not be available in a Wayland session.
 */
std::string keyToUtf8(const fcitx::Key &key) {
    const uint32_t sym = static_cast<uint32_t>(key.sym());

    // Unicode keysyms live at 0x01000000 + UCS-4 codepoint.
    uint32_t ucs4 = 0;
    if (sym >= 0x01000000 && sym <= 0x0110FFFF) {
        ucs4 = sym - 0x01000000;
    } else if (sym >= 0x0020 && sym <= 0x007E) {
        ucs4 = sym;           // plain ASCII
    } else {
        // For legacy keysyms that represent printable characters (Latin-1
        // supplement 0x00A0-0x00FF), keyboard_types can still accept the
        // keysym directly in the Rust mapping, so return "" here and let Rust
        // fall back to the keysym path.
        return {};
    }

    if (ucs4 == 0) return {};

    // Encode UCS-4 to UTF-8.
    std::string out;
    if (ucs4 < 0x80) {
        out += static_cast<char>(ucs4);
    } else if (ucs4 < 0x800) {
        out += static_cast<char>(0xC0 | (ucs4 >> 6));
        out += static_cast<char>(0x80 | (ucs4 & 0x3F));
    } else if (ucs4 < 0x10000) {
        out += static_cast<char>(0xE0 | (ucs4 >> 12));
        out += static_cast<char>(0x80 | ((ucs4 >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (ucs4 & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (ucs4 >> 18));
        out += static_cast<char>(0x80 | ((ucs4 >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((ucs4 >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (ucs4 & 0x3F));
    }
    return out;
}

/** Determine the default afrim config path. */
std::string defaultConfigPath() {
    // $AFRIM_CONFIG overrides the default location.
    if (const char *env = std::getenv("AFRIM_CONFIG"); env && *env) {
        return env;
    }
    const char *home = std::getenv("HOME");
    if (!home || !*home) home = "/root";
    return std::string(home) + "/.config/fcitx5/afrim/config.toml";
}

} // anonymous namespace

// ── AfrimInputMethodEngine ───────────────────────────────────────────────────

AfrimInputMethodEngine::AfrimInputMethodEngine(fcitx::Instance *instance)
    : instance_(instance) {
    const std::string path = defaultConfigPath();
    FCITX_INFO() << "[afrim] Loading configuration from: " << path;
    engine_ = afrim_engine_create(path.c_str());
    if (!engine_) {
        FCITX_WARN() << "[afrim] Engine creation failed. "
                        "Ensure the config exists at: " << path
                     << "  (override with $AFRIM_CONFIG)";
    } else {
        FCITX_INFO() << "[afrim] Engine loaded from: " << path;
    }
}

AfrimInputMethodEngine::~AfrimInputMethodEngine() {
    if (engine_) {
        afrim_engine_destroy(engine_);
        engine_ = nullptr;
    }
}

// ── InputMethodEngineV2 ──────────────────────────────────────────────────────

std::vector<fcitx::InputMethodEntry>
AfrimInputMethodEngine::listInputMethods() {
    // A single generic "Afrim" entry.  Users wanting multiple language
    // profiles should configure separate afrim instances or extend this list.
    std::vector<fcitx::InputMethodEntry> entries;
    entries.push_back(std::move(fcitx::InputMethodEntry("afrim", "Afrim", "af", "afrim")
            .setLabel("AF")
            .setIcon("input-keyboard")));
    return entries;
}

// ── InputMethodEngine ────────────────────────────────────────────────────────

void AfrimInputMethodEngine::activate(const fcitx::InputMethodEntry &,
                                      fcitx::InputContextEvent &) {
    // Nothing to do on activate — the engine state carries over until the
    // next deactivate / reset.
}

void AfrimInputMethodEngine::deactivate(const fcitx::InputMethodEntry &,
                                        fcitx::InputContextEvent &event) {
    if (engine_) {
        afrim_engine_reset(engine_);
    }
    clearUI(event.inputContext());
}

void AfrimInputMethodEngine::reset(const fcitx::InputMethodEntry &,
                                   fcitx::InputContextEvent &event) {
    if (engine_) {
        afrim_engine_reset(engine_);
    }
    clearUI(event.inputContext());
}

void AfrimInputMethodEngine::keyEvent(const fcitx::InputMethodEntry &,
                                      fcitx::KeyEvent &event) {
    if (!engine_) return;

    // We only intercept key-press events.  Releases pass through unchanged.
    if (event.isRelease()) return;

    auto       *ic    = event.inputContext();
    const auto &key   = event.key();
    const uint32_t sym = static_cast<uint32_t>(key.sym());

    // ── Snapshot preedit before processing ──────────────────────────────
    // If we already have active input, we absorb the event regardless of
    // whether the new key advances the sequence.  This ensures that Escape,
    // BackSpace, and arrow keys during a sequence are handled by us.
    char *preBuf   = afrim_engine_get_input(engine_);
    bool  hadInput = preBuf && *preBuf;
    afrim_string_free(preBuf);

    // ── Special keys while a sequence is active ──────────────────────────
    if (hadInput) {
        if (sym == FcitxKey_Escape) {
            // Abort the current sequence without committing anything.
            afrim_engine_reset(engine_);
            clearUI(ic);
            event.filterAndAccept();
            return;
        }
        if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
            // Commit the raw input text as-is (no translation).
            char *raw = afrim_engine_get_input(engine_);
            if (raw && *raw) {
                ic->commitString(raw);
            }
            afrim_string_free(raw);
            afrim_engine_reset(engine_);
            clearUI(ic);
            event.filterAndAccept();
            return;
        }
    }

    // ── Feed the key to the Rust engine ─────────────────────────────────
    const std::string keyStr   = keyToUtf8(key);
    const uint32_t    modState = static_cast<uint32_t>(key.states());

    char *cmds = afrim_engine_process_key(
        engine_, sym, modState, /*is_pressed=*/1, keyStr.c_str());

    const bool hasCommands = cmds && *cmds;

    // ── Check preedit after processing ───────────────────────────────────
    char *postBuf  = afrim_engine_get_input(engine_);
    bool  hasInput = postBuf && *postBuf;
    afrim_string_free(postBuf);

    // Absorb the key event if the engine was or is now active, or if it
    // generated explicit commands (sequence match).
    if (hadInput || hasInput || hasCommands) {
        applyCommands(ic, cmds);
        event.filterAndAccept();
    }

    afrim_string_free(cmds);
    updateUI(ic);
}

// ── Internal helpers ─────────────────────────────────────────────────────────

void AfrimInputMethodEngine::applyCommands(fcitx::InputContext *ic,
                                           const char          *cmds) {
    if (!cmds || !*cmds) return;

    // In the preedit model raw characters never reach the application, so
    // `delete` / `clean_delete` need not send BackSpace; the preedit string is
    // refreshed automatically in updateUI().  Only `commit:<text>` requires an
    // explicit action.
    std::istringstream stream(cmds);
    std::string        line;
    while (std::getline(stream, line)) {
        if (line.compare(0, 7, "commit:") == 0) {
            ic->commitString(line.substr(7));
        }
        // pause, resume, delete, clean_delete → handled implicitly by preedit.
    }
}

void AfrimInputMethodEngine::updateUI(fcitx::InputContext *ic) {
    if (!engine_) return;

    // ── Preedit ──────────────────────────────────────────────────────────
    char       *inputBuf = afrim_engine_get_input(engine_);
    std::string input    = inputBuf ? inputBuf : "";
    afrim_string_free(inputBuf);

    fcitx::Text preedit;
    if (!input.empty()) {
        preedit.append(input, fcitx::TextFormatFlag::Underline);
        preedit.setCursor(static_cast<int>(input.size()));
    }
    ic->inputPanel().setClientPreedit(preedit);
    ic->updatePreedit();

    // ── Candidate list ───────────────────────────────────────────────────
    char       *candBuf = afrim_engine_get_candidates(engine_);
    std::string candStr = candBuf ? candBuf : "";
    afrim_string_free(candBuf);

    auto candidateList = std::make_unique<fcitx::CommonCandidateList>();
    candidateList->setPageSize(9);

    if (!candStr.empty()) {
        std::istringstream cs(candStr);
        std::string        line;
        while (std::getline(cs, line)) {
            // Format: code\tremaining_code\tcan_commit\ttext1|text2|…
            std::vector<std::string> parts;
            {
                std::istringstream ls(line);
                std::string        part;
                while (std::getline(ls, part, '\t')) {
                    parts.push_back(std::move(part));
                }
            }
            if (parts.size() < 4) continue;

            // Split the texts field on '|' and add one entry per translation.
            const std::string &textsField = parts[3];
            std::istringstream  ts(textsField);
            std::string         text;
            while (std::getline(ts, text, '|')) {
                if (!text.empty()) {
                    candidateList->append<AfrimCandidateWord>(this, text);
                }
            }
        }
    }

    ic->inputPanel().setCandidateList(std::move(candidateList));
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void AfrimInputMethodEngine::clearUI(fcitx::InputContext *ic) {
    ic->inputPanel().reset();
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

// ── Addon entry point ────────────────────────────────────────────────────────
FCITX_ADDON_FACTORY(AfrimEngineFactory)
