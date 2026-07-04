use afrim_preprocessor::Command;
use std::ffi::{c_char, CString};

/// Serialise a slice of `Command` values.
pub fn encode_commands(commands: &[Command]) -> String {
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
pub fn make_cstring(s: &str) -> *mut c_char {
    // Strip any embedded NUL bytes to keep the C string well-formed.
    let safe: String = s.chars().filter(|&c| c != '\0').collect();
    CString::new(safe)
        .unwrap_or_else(|_| CString::new("").unwrap())
        .into_raw()
}
