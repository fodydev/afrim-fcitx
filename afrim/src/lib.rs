/// C-compatible FFI layer.
///
/// The C++ side includes `afrim_ffi.h` and calls these functions.
/// Every function that returns a heap-allocated string transfers ownership
/// to the caller; the caller **must** free it with `afrim_string_free`.
///
/// Wire protocols
/// ──────────────
/// • `afrim_engine_process_key` / `afrim_engine_commit_candidate` return a
///   **newline-separated command list**:
///
///       pause
///       delete
///       delete
///       commit:ç
///       resume
///
///   Possible command lines:
///     `pause`         – preprocessor started a bracketed edit sequence
///     `resume`        – bracketed edit sequence ended
///     `delete`        – delete one character from the application text
///     `clean_delete`  – internal deletion (inhibit mode); no BackSpace needed
///     `commit:<text>` – commit `<text>` to the application
///
/// • `afrim_engine_get_candidates` returns one line per `Predicate`:
///
///       <code>\t<remaining_code>\t<can_commit:0|1>\t<text1>|<text2>|…
mod engine;

use engine::AfrimEngine;
use afrim_preprocessor::Command;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/// Create an `AfrimEngine` from the TOML config file at `config_path`.
///
/// Returns a non-null opaque pointer on success, `NULL` on error (bad path,
/// parse failure, …).  Ownership transfers to the caller; free with
/// `afrim_engine_destroy`.
#[no_mangle]
pub extern "C" fn afrim_engine_create(config_path: *const c_char) -> *mut AfrimEngine {
    if config_path.is_null() {
        return std::ptr::null_mut();
    }
    let path_str = match unsafe { CStr::from_ptr(config_path) }.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };
    match AfrimEngine::from_config_path(path_str) {
        Ok(engine) => Box::into_raw(Box::new(engine)),
        Err(e) => {
            eprintln!("[afrim-fcitx5] afrim_engine_create error: {e}");
            std::ptr::null_mut()
        }
    }
}

/// Destroy an `AfrimEngine` returned by `afrim_engine_create`.
#[no_mangle]
pub extern "C" fn afrim_engine_destroy(engine: *mut AfrimEngine) {
    if !engine.is_null() {
        // SAFETY: pointer was created by Box::into_raw in afrim_engine_create.
        unsafe { drop(Box::from_raw(engine)) };
    }
}

/// Clear the engine's input buffer (call on input-context deactivate/reset).
#[no_mangle]
pub extern "C" fn afrim_engine_reset(engine: *mut AfrimEngine) {
    if engine.is_null() {
        return;
    }
    unsafe { (*engine).reset() };
}

// ---------------------------------------------------------------------------
// Key / candidate processing
// ---------------------------------------------------------------------------

/// Feed one key event to the engine.
///
/// Parameters
/// ----------
/// `keysym`         – X11 keysym value (e.g. 0xFF08 = BackSpace).
/// `modifier_state` – X11 modifier mask (Shift=0x01, CapsLock=0x02,
///                    Control=0x04, Alt=0x08).
/// `is_pressed`     – non-zero for key-press, zero for key-release.
/// `key_str`        – UTF-8 string produced by the key (may be "").
///
/// Returns a newline-separated command string (see module doc).  The returned
/// pointer is never NULL (empty string on no-op).  Caller frees with
/// `afrim_string_free`.
#[no_mangle]
pub extern "C" fn afrim_engine_process_key(
    engine: *mut AfrimEngine,
    keysym: u32,
    modifier_state: u32,
    is_pressed: i32,
    key_str: *const c_char,
) -> *mut c_char {
    if engine.is_null() {
        return make_cstring("");
    }
    let key_s = if key_str.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(key_str).to_str().unwrap_or("") }
    };
    let commands = unsafe {
        (*engine).process_key(keysym, modifier_state, is_pressed != 0, key_s)
    };
    make_cstring(&encode_commands(&commands))
}

/// Commit a candidate text chosen by the user (from the candidate panel).
///
/// Returns newline-separated commands (same format as `afrim_engine_process_key`).
/// Caller frees with `afrim_string_free`.
#[no_mangle]
pub extern "C" fn afrim_engine_commit_candidate(
    engine: *mut AfrimEngine,
    text: *const c_char,
) -> *mut c_char {
    if engine.is_null() || text.is_null() {
        return make_cstring("");
    }
    let text_s = unsafe { CStr::from_ptr(text).to_str().unwrap_or("") };
    let commands = unsafe { (*engine).commit_candidate(text_s) };
    make_cstring(&encode_commands(&commands))
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

/// Return the current contents of the preprocessor's input buffer.
///
/// The returned string is the "preedit" text the user has typed so far.
/// Empty string means no active sequence.  Caller frees with
/// `afrim_string_free`.
#[no_mangle]
pub extern "C" fn afrim_engine_get_input(engine: *const AfrimEngine) -> *mut c_char {
    if engine.is_null() {
        return make_cstring("");
    }
    make_cstring(&unsafe { (*engine).get_input() })
}

/// Return the current candidate list encoded as newline-separated lines.
///
/// Each line: `<code>\t<remaining_code>\t<can_commit:0|1>\t<text1>|<text2>|…`
///
/// Empty string when there are no candidates.  Caller frees with
/// `afrim_string_free`.
#[no_mangle]
pub extern "C" fn afrim_engine_get_candidates(engine: *const AfrimEngine) -> *mut c_char {
    if engine.is_null() {
        return make_cstring("");
    }
    let candidates = unsafe { (*engine).get_candidates() };
    let encoded = candidates
        .iter()
        .map(|p| {
            format!(
                "{}\t{}\t{}\t{}",
                p.code,
                p.remaining_code,
                if p.can_commit { 1 } else { 0 },
                p.texts.join("|")
            )
        })
        .collect::<Vec<_>>()
        .join("\n");
    make_cstring(&encoded)
}

// ---------------------------------------------------------------------------
// Memory management
// ---------------------------------------------------------------------------

/// Free a string returned by any `afrim_engine_*` function.
///
/// Passing `NULL` is safe (no-op).
#[no_mangle]
pub extern "C" fn afrim_string_free(s: *mut c_char) {
    if !s.is_null() {
        // SAFETY: the pointer was created by CString::into_raw inside
        // make_cstring, which is the only source of returned strings.
        unsafe { drop(CString::from_raw(s)) };
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

/// Serialise a slice of `Command` values into the wire protocol.
fn encode_commands(commands: &[Command]) -> String {
    if commands.is_empty() {
        return String::new();
    }
    commands
        .iter()
        .map(|cmd| match cmd {
            Command::Pause => "pause".to_owned(),
            Command::Resume => "resume".to_owned(),
            Command::Delete(text) => format!("delete:{text}"),
            Command::CommitText(text) => format!("commit:{text}"),
        })
        .collect::<Vec<_>>()
        .join("\n")
}

/// Allocate a NUL-terminated C string from a Rust `&str`.
///
/// Interior NUL bytes (which would truncate the string in C) are stripped.
/// The resulting pointer must be freed with `afrim_string_free`.
fn make_cstring(s: &str) -> *mut c_char {
    // Strip any embedded NUL bytes to keep the C string well-formed.
    let safe: String = s.chars().filter(|&c| c != '\0').collect();
    CString::new(safe)
        .unwrap_or_else(|_| CString::new("").unwrap())
        .into_raw()
}
