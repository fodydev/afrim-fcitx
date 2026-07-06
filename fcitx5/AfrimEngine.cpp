#include "AfrimEngine.h"

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

FCITX_DEFINE_LOG_CATEGORY(AFRIM_LOG_CATEGORY, "afrim");

// Anonymous namespace
namespace {

/**
 * AfrimCandidateWord
 *
 * A candidate word that calls back into AfrimInputMethodEngine when selected.
 */
class AfrimCandidateWord final : public fcitx::CandidateWord {
public:
    AfrimCandidateWord(AfrimInputMethodEngine *engine, std::string text,
                       std::string remCode)
        : fcitx::CandidateWord(makeText(text, remCode)), engine_(engine),
          text_(std::move(text)) {}

    void select(fcitx::InputContext *ic) const override {
        // Commit the candidate to the user.
        // Note that, we commit directly.
        ic->commitString(text_);
        afrim_engine_reset(engine_->rustEngine());

        engine_->clearUI(ic);
    }

private:
    AfrimInputMethodEngine *engine_;
    std::string text_;

    static fcitx::Text makeText(const std::string &text,
                                const std::string &remCode) {
        fcitx::Text ftext;
        ftext.append(text, fcitx::TextFormatFlag::Bold);
        ftext.append(" ~ ", fcitx::TextFormatFlag::NoFlag);
        ftext.append(remCode, fcitx::TextFormatFlag::Italic);

        return ftext;
    }
};

/** Determine the default afrim config path. */
std::string defaultConfigPath() {
    // $AFRIM_CONFIG overrides the default location.
    if (const char *env = std::getenv("AFRIM_CONFIG"); env && *env) {
        return env;
    }
    return fcitx::StandardPaths::global().userDirectory(
               fcitx::StandardPathsType::PkgConfig) /
           "afrim/config.toml";
}

} // anonymous namespace

// AfrimInputMethodEngine
AfrimInputMethodEngine::AfrimInputMethodEngine(fcitx::Instance *instance)
    : instance_(instance) {
    const std::string path = defaultConfigPath();
    AFRIM_INFO() << "[afrim] Loading configuration from: " << path;
    engine_ = afrim_engine_create(path.c_str());
    if (!engine_) {
        AFRIM_ERROR() << "[afrim] Engine creation failed. "
                         "Ensure the config exists at: "
                      << path << "  (override with $AFRIM_CONFIG)";
    } else {
        AFRIM_INFO() << "[afrim] Engine loaded from: " << path;
    }
}

AfrimInputMethodEngine::~AfrimInputMethodEngine() {
    if (engine_) {
        afrim_engine_destroy(engine_);
        engine_ = nullptr;
    }
}

// InputMethodEngineV2
std::vector<fcitx::InputMethodEntry>
AfrimInputMethodEngine::listInputMethods() {
    // A single generic "Afrim" entry. Users wanting multiple language
    // profiles should configure separate afrim instances or extend this list.
    std::vector<fcitx::InputMethodEntry> entries;
    entries.push_back(
        std::move(fcitx::InputMethodEntry("afrim", "Afrim", "en_CM", "afrim")
                      .setLabel("AF")
                      .setIcon("input-keyboard")));
    return entries;
}

// InputMethodEngine
void AfrimInputMethodEngine::activate(const fcitx::InputMethodEntry &,
                                      fcitx::InputContextEvent &) {
    // Nothing to do on activate.
    // The engine state carries over until the
    // next deactivate / reset.
}

void AfrimInputMethodEngine::deactivate(const fcitx::InputMethodEntry &im_entry,
                                        fcitx::InputContextEvent &event) {
    reset(im_entry, event);
}

void AfrimInputMethodEngine::reset(const fcitx::InputMethodEntry &,
                                   fcitx::InputContextEvent &event) {
    if (engine_) {
        afrim_engine_reset(engine_);
    }
    clearUI(event.inputContext());
}

void AfrimInputMethodEngine::keyEvent(const fcitx::InputMethodEntry &im_entry,
                                      fcitx::KeyEvent &event) {
    if (!engine_)
        return;

    // We only intercept key-press events. Releases pass through unchanged.
    if (event.isRelease())
        return;

    auto *ic = event.inputContext();
    const auto &key = event.key();
    const auto sym = key.sym();

    // If we already have active input, we absorb the event regardless of
    // whether the new key advances the sequence. This ensures that Escape,
    // BackSpace, and arrow keys during a sequence are handled by us.
    char *preBuf = afrim_engine_get_input(engine_);
    bool hadInput = preBuf && *preBuf;
    afrim_string_free(preBuf);

    // Special keys while a sequence is active
    if (hadInput) {
        if (sym == FcitxKey_Escape) {
            // Abort the current sequence without committing anything.
            reset(im_entry, event);
            event.filterAndAccept();
            return;
        }
        // Commit the raw input text as-is (no translation).
        if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
            if (auto *candidateList = ic->inputPanel().candidateList().get()) {
                int idx = candidateList->cursorIndex();

                if (idx >= 0) {
                    candidateList->candidate(idx).select(ic);
                    event.filterAndAccept();
                    return;
                }
            }

            char *raw = afrim_engine_get_input(engine_);
            if (raw && *raw) {
                ic->commitString(raw);
            }
            afrim_string_free(raw);
            reset(im_entry, event);
            event.filterAndAccept();
            return;
        }
        // Commit the preedit text as-is.
        if (sym == FcitxKey_space || sym == FcitxKey_KP_Space) {
            ic->commitString(preedit);
            reset(im_entry, event);
            return;
        }
        // Navigate between candidates.
        if (sym == FcitxKey_Left || sym == FcitxKey_KP_Left ||
            sym == FcitxKey_Right || sym == FcitxKey_KP_Right) {
            auto *candidateList = ic->inputPanel().candidateList().get();
            if (candidateList) {
                auto *movable =
                    dynamic_cast<fcitx::CursorMovableCandidateList *>(
                        candidateList);
                if (movable) {
                    if (sym == FcitxKey_Left || sym == FcitxKey_KP_Left) {
                        movable->prevCandidate();
                    } else {
                        movable->nextCandidate();
                    }

                    ic->updateUserInterface(
                        fcitx::UserInterfaceComponent::InputPanel);
                }
            }

            event.filterAndAccept();
            return;
        }
    }
    // Let pass the backspace event when the input is not active.
    else if (sym == FcitxKey_BackSpace) {
        return;
    } else {
        // We clear the preedit since there is no sequence.
        preedit.clear();
    }
    // Feed the key to the Rust engine.
    const std::string keyStr = fcitx::Key::keySymToUTF8(sym);

    AFRIM_DEBUG() << "[afrim] process: " << sym << " (" << keyStr << ")";
    char *cmds = afrim_engine_process_key(engine_, sym, keyStr.c_str());

    // Check if the input changed after processing
    char *postBuf = afrim_engine_get_input(engine_);
    AFRIM_DEBUG() << "[afrim] cursor: " << postBuf;
    bool hasInput = postBuf && *postBuf;
    afrim_string_free(postBuf);

    // Clear the preedit if input is empty.
    if (hasInput) {
        // Prevent an unwanted backspace char in the preedit.
        // Except it, the other characters are safe since afrim will reset
        // in case of detected non printable characters.
        if (sym != FcitxKey_BackSpace) {
            preedit.append(keyStr);
        }
    } else {
        preedit.clear();
    }

    applyCommands(ic, cmds);

    // Absorb the key event if the engine was or is now active.
    if (hadInput || hasInput) {
        event.filterAndAccept();
    }

    afrim_string_free(cmds);
    updateUI(ic);
}

void AfrimInputMethodEngine::applyCommands(fcitx::InputContext *ic,
                                           const char *cmds) {
    if (!cmds || !*cmds)
        return;

    std::istringstream stream(cmds);
    std::string line;
    while (std::getline(stream, line)) {
        AFRIM_DEBUG() << "[afrim] command: " << line;
        AFRIM_DEBUG() << "[afrim] preedit before: " << preedit;
        if (line.compare(0, 7, "commit:") == 0) {
            preedit.append(line.substr(7));
        } else if (line.compare(0, 7, "delete:") == 0) {
            int step = line.substr(6).size() - 1;
            AFRIM_DEBUG() << "[afrim] delele step: " << step;
            preedit = preedit.substr(0, preedit.size() - (step ? step : 1));
        }
        AFRIM_DEBUG() << "[afrim] preedit after: " << preedit;
    }
}

void AfrimInputMethodEngine::updateUI(fcitx::InputContext *ic) {
    if (!engine_)
        return;

    // Input
    char *inputBuf = afrim_engine_get_input(engine_);
    std::string input = inputBuf ? inputBuf : "";
    afrim_string_free(inputBuf);
    ic->inputPanel().setAuxUp(
        fcitx::Text(input, fcitx::TextFormatFlag::Underline));

    // Preedit
    fcitx::Text cPreedit(preedit, fcitx::TextFormatFlag::Underline);
    cPreedit.setCursor(static_cast<int>(preedit.size()));
    ic->inputPanel().setClientPreedit(cPreedit);
    ic->updatePreedit();

    // Candidate list
    char *candBuf = afrim_engine_get_candidates(engine_);
    std::string candStr = candBuf ? candBuf : "";
    afrim_string_free(candBuf);

    auto candidateList = std::make_unique<fcitx::CommonCandidateList>();
    candidateList->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
    candidateList->setPageSize(9);

    if (!candStr.empty()) {
        std::istringstream cs(candStr);
        std::string line;
        while (std::getline(cs, line)) {
            // Format: code\tremaining_code\tcan_commit\ttext1|text2|...
            std::vector<std::string> parts;
            {
                std::istringstream ls(line);
                std::string part;
                while (std::getline(ls, part, '\t')) {
                    parts.push_back(std::move(part));
                }
            }
            if (parts.size() < 4)
                continue;

            // Split the texts field on '|' and add one entry per translation.
            const std::string &remCode = parts[1];
            const std::string &textsField = parts[3];
            std::istringstream ts(textsField);
            std::string text;
            while (std::getline(ts, text, '|')) {
                if (text.empty())
                    continue;

                candidateList->append<AfrimCandidateWord>(this, text, remCode);
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

// Addon entry point
FCITX_ADDON_FACTORY(AfrimEngineFactory)
