/**
 * afrim_ffi.h — C interface to the afrim-fcitx5 Rust static library.
 *
 * Every function that returns a `char*` transfers ownership to the caller.
 * The caller MUST free it with `afrim_string_free`.  Passing NULL to any
 * `engine` parameter is safe (functions become no-ops or return "").
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle to the Rust engine (Preprocessor + Translator). */
typedef struct AfrimRustEngine AfrimRustEngine;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

/**
 * Create an engine from the TOML config file at `config_path`.
 *
 * Returns NULL on error (bad path, parse failure …).  The caller owns the
 * returned handle and must eventually call `afrim_engine_destroy`.
 */
AfrimRustEngine *afrim_engine_create(const char *config_path);

/** Destroy an engine returned by `afrim_engine_create`. */
void afrim_engine_destroy(AfrimRustEngine *engine);

/** Clear the internal input buffer (call on input-context deactivate/reset). */
void afrim_engine_reset(AfrimRustEngine *engine);

/* ── Key / candidate processing ────────────────────────────────────────── */

/**
 * Feed one key event to the engine.
 *
 * @param keysym         X11 keysym (e.g. 0xFF08 = BackSpace).
 * @param modifier_state X11 modifier mask (Shift=0x01, CapsLock=0x02,
 *                       Control=0x04, Alt=0x08).
 * @param is_pressed     Non-zero for key-press; zero for key-release.
 * @param key_str        UTF-8 string produced by this key ("" if none).
 *
 * @return Newline-separated command list (never NULL, may be "").
 *
 *   Command lines:
 *     "pause"         – preprocessor started a bracketed edit sequence
 *     "resume"        – bracketed edit sequence ended
 *     "delete"        – delete one character from the application text
 *     "clean_delete"  – internal deletion (inhibit mode); no BackSpace needed
 *     "commit:<text>" – commit <text> to the application
 *
 * Caller frees with `afrim_string_free`.
 */
char *afrim_engine_process_key(AfrimRustEngine *engine,
                                uint32_t keysym,
                                uint32_t modifier_state,
                                int is_pressed,
                                const char *key_str);

/**
 * Commit a candidate text selected by the user from the candidate panel.
 *
 * @return Same command format as `afrim_engine_process_key`.
 *         Caller frees with `afrim_string_free`.
 */
char *afrim_engine_commit_candidate(AfrimRustEngine *engine, const char *text);

/* ── State queries ─────────────────────────────────────────────────────── */

/**
 * Return the current preedit / input buffer text (may be "").
 * Caller frees with `afrim_string_free`.
 */
char *afrim_engine_get_input(const AfrimRustEngine *engine);

/**
 * Return the current candidate list as newline-separated lines.
 *
 * Each line:  <code>\t<remaining_code>\t<can_commit:0|1>\t<text1>|<text2>|…
 *
 * Empty string when there are no candidates.
 * Caller frees with `afrim_string_free`.
 */
char *afrim_engine_get_candidates(const AfrimRustEngine *engine);

/* ── Memory management ─────────────────────────────────────────────────── */

/** Free a string returned by any `afrim_engine_*` function. NULL-safe. */
void afrim_string_free(char *s);

#ifdef __cplusplus
}
#endif
