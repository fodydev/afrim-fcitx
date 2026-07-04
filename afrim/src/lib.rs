/// C-compatible FFI layer.
///
/// The C++ side includes `afrim_ffi.h` and calls these functions.
/// Every function that returns a heap-allocated string transfers ownership
/// to the caller; the caller **must** free it with `afrim_string_free`.
///
/// ### Wire protocols
/// 
/// `afrim_engine_process_key` / `afrim_engine_commit_candidate` return a
///   **newline-separated command list**:
///
///       pause
///       delete
///       delete
///       commit:ç
///       resume
///
///   Possible command lines:
///     `pause`         - start the process of commands
///     `resume`        - end the process of commands
///     `delete:<text>` - delete characters from the application text
///     `commit:<text>` - commit `<text>` to the application
///
/// `afrim_engine_get_candidates` returns one line per `Predicate`:
///
///       <code>\t<remaining_code>\t<can_commit:0|1>\t<text1>|<text2>|...
///
mod engine;
mod helpers;

use engine::AfrimEngine;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use helpers::{encode_commands, make_cstring};


/// Create an `AfrimEngine` from the TOML config file at `config_path`.
///
/// Returns a non-null pointer on success, `NULL` on error (bad path,
/// parse failure, ...). Ownership transfers to the caller; free with
/// `afrim_engine_destroy`.
///
/// # Safety
///
/// Consult the engine doc.
///
#[no_mangle]
pub unsafe extern "C" fn afrim_engine_create(config_path: *const c_char) -> *mut AfrimEngine {
    if config_path.is_null() {
        return std::ptr::null_mut();
    }
    let path_str = match CStr::from_ptr(config_path).to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };
    match AfrimEngine::from_config_path(path_str) {
        Ok(engine) => Box::into_raw(Box::new(engine)),
        Err(e) => {
            eprintln!("[afrim] afrim_engine_create error: {e}");
            std::ptr::null_mut()
        }
    }
}

/// Destroy an `AfrimEngine` returned by `afrim_engine_create`.
///
/// # Safety
///
/// Consult the engine doc.
///
#[no_mangle]
pub unsafe extern "C" fn afrim_engine_destroy(engine: *mut AfrimEngine) {
    if !engine.is_null() {
        // SAFETY: pointer was created by Box::into_raw in afrim_engine_create.
        drop(Box::from_raw(engine));
    }
}

/// Clear the engine's input buffer (call on input-context deactivate/reset).
///
/// # Safety
///
/// Consult the engine doc.
///
#[no_mangle]
pub unsafe extern "C" fn afrim_engine_reset(engine: *mut AfrimEngine) {
    if engine.is_null() {
        return;
    }
    (*engine).reset();
}

/// Feed one key event to the engine.
///
/// Parameters
/// ----------
/// `keysym`         - X11 keysym value (e.g. 0xFF08 = BackSpace).
/// `key_str`        - UTF-8 string produced by the key (may be "").
///
/// Returns a newline-separated command string (see module doc). The returned
/// pointer is never NULL (empty string on no-op). Caller frees with
/// `afrim_string_free`.
///
/// # Safety
///
/// Consult the engine doc.
///
#[no_mangle]
pub unsafe extern "C" fn afrim_engine_process_key(
    engine: *mut AfrimEngine,
    keysym: u32,
    key_str: *const c_char,
) -> *mut c_char {
    if engine.is_null() {
        return make_cstring("");
    }
    let key_s = if key_str.is_null() {
        ""
    } else {
        CStr::from_ptr(key_str).to_str().unwrap_or("")
    };
    let commands = (*engine).process_key(keysym, key_s);
    make_cstring(&encode_commands(&commands))
}

/// Commit a candidate text chosen by the user (from the candidate panel).
///
/// Returns newline-separated commands (same format as `afrim_engine_process_key`).
/// Caller frees with `afrim_string_free`.
///
/// # Safety
///
/// Consult the engine doc.
///
#[no_mangle]
pub unsafe extern "C" fn afrim_engine_commit_candidate(
    engine: *mut AfrimEngine,
    text: *const c_char,
) -> *mut c_char {
    if engine.is_null() || text.is_null() {
        return make_cstring("");
    }
    let text_s = CStr::from_ptr(text).to_str().unwrap_or("");
    let commands = (*engine).commit_candidate(text_s);
    make_cstring(&encode_commands(&commands))
}

/// Return the current contents of the preprocessor's input buffer.
///
/// The returned string is the "preedit" text the user has typed so far.
/// Empty string means no active sequence. Caller frees with
/// `afrim_string_free`.
///
/// # Safety
///
/// Consult the engine doc.
///
#[no_mangle]
pub unsafe extern "C" fn afrim_engine_get_input(engine: *const AfrimEngine) -> *mut c_char {
    if engine.is_null() {
        return make_cstring("");
    }
    make_cstring(&((*engine).get_input()))
}

/// Return the current candidate list encoded as newline-separated lines.
///
/// Each line: `<code>\t<remaining_code>\t<can_commit:0|1>\t<text1>|<text2>|...`
///
/// Empty string when there are no candidates. Caller frees with
/// `afrim_string_free`.
///
/// # Safety
///
/// Consult the engine doc.
///
#[no_mangle]
pub unsafe extern "C" fn afrim_engine_get_candidates(engine: *const AfrimEngine) -> *mut c_char {
    if engine.is_null() {
        return make_cstring("");
    }
    let candidates = (*engine).get_candidates();
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

/// Free a string returned by any `afrim_engine_*` function.
///
/// Passing `NULL` is safe (no-op).
///
/// # Safety
///
/// Consult the engine doc.
///
#[no_mangle]
pub unsafe extern "C" fn afrim_string_free(s: *mut c_char) {
    if !s.is_null() {
        // SAFETY: the pointer was created by CString::into_raw inside
        // make_cstring, which is the only source of returned strings.
        drop(CString::from_raw(s));
    }
}
