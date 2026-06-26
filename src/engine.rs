use afrim_config::Config;
use afrim_preprocessor::{utils, Command, Preprocessor};
use afrim_translator::{Predicate, Translator};
use std::{path::Path, rc::Rc};
use keyboard_types::Modifiers;
use afrim_preprocessor::{Key, KeyboardEvent, KeyState, NamedKey};

// ---------------------------------------------------------------------------
// Public engine type
// ---------------------------------------------------------------------------

/// Owns a `Preprocessor` and a `Translator` initialised from a TOML config
/// file.  This struct is intentionally **not** `Send` / `Sync` (inherited from
/// `Preprocessor` which uses `Rc<Node>`) — fcitx5 dispatches all IM events on
/// its single UI thread, so that is perfectly fine.
pub struct AfrimEngine {
    preprocessor: Preprocessor,
    translator: Translator,
}

impl AfrimEngine {
    /// Load config from `config_path` and build the engine.
    pub fn from_config_path(config_path: &str) -> Result<Self, String> {
        let config = Config::from_file(Path::new(config_path))
            .map_err(|e| e.to_string())?;

        let auto_commit = config
            .core
            .as_ref()
            .and_then(|c| c.auto_commit)
            .unwrap_or(false);

        // ── Preprocessor ──────────────────────────────────────────────────
        // extract_data() already handles auto_capitalize internally (it adds
        // uppercase variants when the option is set), so we feed the result
        // directly into build_map without any extra work on our side.
        let data_map = config.extract_data();
        // Build the Vec<Vec<&str>> that build_map expects.  The &str slices
        // borrow from data_map; build_map copies everything into the Node so
        // data_map can be dropped afterwards.
        let pairs: Vec<Vec<&str>> = data_map
            .iter()
            .map(|(k, v)| vec![k.as_str(), v.as_str()])
            .collect();
        let node = utils::build_map(pairs);
        // data_map is still alive here — drop explicitly to make that clear.
        drop(data_map);

        let preprocessor = Preprocessor::new(Rc::new(node), 64);

        // ── Translator ────────────────────────────────────────────────────
        let translation = config.extract_translation();
        let mut translator = Translator::new(translation, auto_commit);

        // Register any Rhai scripted translators declared in [translators].
        #[cfg(feature = "rhai")]
        {
            match config.extract_translators() {
                Ok(translators) => {
                    for (name, ast) in translators {
                        translator.register(name, ast);
                    }
                }
                Err(e) => {
                    // Non-fatal: log and continue without scripts.
                    eprintln!("[afrim-fcitx5] Failed to load translators: {e}");
                }
            }
        }

        Ok(Self {
            preprocessor,
            translator,
        })
    }

    // ── Key processing ────────────────────────────────────────────────────

    /// Feed one key event to the preprocessor.
    ///
    /// Returns the command queue drained after the event.  An empty return
    /// means the key did not trigger any afrim action.
    pub fn process_key(
        &mut self,
        keysym: u32,
        modifier_state: u32,
        is_pressed: bool,
        key_str: &str,
    ) -> Vec<Command> {
        // Map the X11 keysym to a keyboard_types Key.
        let key = match keysym_to_key(keysym, key_str) {
            Some(k) => k,
            // Unrecognised key (function keys, media keys, …) — don't disturb
            // the preprocessor state.
            None => return Vec::new(),
        };

        let state = if is_pressed {
            KeyState::Down
        } else {
            KeyState::Up
        };

        let event = KeyboardEvent {
            key,
            state,
            modifiers: raw_to_modifiers(modifier_state),
            ..Default::default()
        };

        dbg!(&event);

        self.preprocessor.process(event);
        self.drain_queue()
    }

    /// Commit a candidate that the user selected from the candidate panel.
    ///
    /// The preprocessor will discard its current input buffer and emit
    /// commands to replace it with `text`.
    pub fn commit_candidate(&mut self, text: &str) -> Vec<Command> {
        self.preprocessor.commit(text.to_owned());
        self.drain_queue()
    }

    // ── State queries ─────────────────────────────────────────────────────

    /// Current content of the preprocessor's input buffer (may be empty).
    pub fn get_input(&self) -> String {
        self.preprocessor.get_input()
    }

    /// Translate the current input and return candidate predicates.
    pub fn get_candidates(&self) -> Vec<Predicate> {
        let input = self.preprocessor.get_input();
        if input.is_empty() {
            return Vec::new();
        }
        self.translator.translate(&input)
    }

    /// Clear internal state — called when the input context loses focus.
    pub fn reset(&mut self) {
        self.preprocessor.clear_queue();
    }

    // ── Private helpers ───────────────────────────────────────────────────

    fn drain_queue(&mut self) -> Vec<Command> {
        let mut cmds = Vec::new();
        while let Some(cmd) = self.preprocessor.pop_queue() {
            cmds.push(cmd);
        }
        cmds
    }
}

// ---------------------------------------------------------------------------
// Key mapping helpers
// ---------------------------------------------------------------------------

/// Map an X11 keysym (plus an optional UTF-8 key label from fcitx5) to a
/// `keyboard_types::Key`.  Returns `None` for keys that afrim should not
/// see (function keys, media keys, modifier-only events, etc.).
fn keysym_to_key(
    keysym: u32,
    key_str: &str,
) -> Option<afrim_preprocessor::Key> {
    let key = match keysym {
        // ── Named keys that afrim/keyboard_types recognises ──────────────
        0xFF08 => Key::Named(NamedKey::Backspace),
        0xFF09 => Key::Named(NamedKey::Tab),
        0xFF0A | 0xFF0D => Key::Named(NamedKey::Enter),
        0xFF1B => Key::Named(NamedKey::Escape),
        0xFF50 => Key::Named(NamedKey::Home),
        0xFF51 => Key::Named(NamedKey::ArrowLeft),
        0xFF52 => Key::Named(NamedKey::ArrowUp),
        0xFF53 => Key::Named(NamedKey::ArrowRight),
        0xFF54 => Key::Named(NamedKey::ArrowDown),
        0xFF55 => Key::Named(NamedKey::PageUp),
        0xFF56 => Key::Named(NamedKey::PageDown),
        0xFF57 => Key::Named(NamedKey::End),
        0xFF63 => Key::Named(NamedKey::Insert),
        0xFFFF => Key::Named(NamedKey::Delete),

        // ── Modifier-only keysyms — do not disturb the preprocessor ──────
        0xFFE1..=0xFFEE => return None,

        // ── Function / special keys — let pass through ────────────────────
        0xFFBE..=0xFFCF => return None, // F1-F16
        0xFF61..=0xFF6B => return None, // keypad / misc
        0xFF7E | 0xFF7F => return None, // compose, delete

        // ── Printable ASCII (basic Latin) ─────────────────────────────────
        0x0020..=0x007E => {
            if !key_str.is_empty() {
                Key::Character(key_str.into())
            } else if let Some(c) = char::from_u32(keysym) {
                Key::Character(c.to_string().into())
            } else {
                return None;
            }
        }

        // ── Non-ASCII printable (Latin extended, CJK, …) ─────────────────
        // X11 Unicode keysyms sit at 0x01000000 + UCS-4 codepoint.
        0x01000080..=0x0110FFFF => {
            let ucs4 = keysym - 0x01000000;
            if !key_str.is_empty() {
                Key::Character(key_str.into())
            } else if let Some(c) = char::from_u32(ucs4) {
                Key::Character(c.to_string().into())
            } else {
                return None;
            }
        }

        _ => {
            // If fcitx5 provided a printable key string, use it.
            if !key_str.is_empty() {
                Key::Character(key_str.into())
            } else {
                return None;
            }
        }
    };

    Some(key)
}

/// Map X11 / fcitx5 modifier mask bits to `keyboard_types::Modifiers`.
///
/// The lower four bits of the X11 modifier mask happen to coincide with the
/// keyboard_types bit assignments for Shift, CapsLock, Control, and Alt.
fn raw_to_modifiers(raw: u32) -> Modifiers {
    let mut m = Modifiers::empty();
    if raw & 0x01 != 0 {
        m |= Modifiers::SHIFT;
    }
    if raw & 0x02 != 0 {
        m |= Modifiers::CAPS_LOCK;
    }
    if raw & 0x04 != 0 {
        m |= Modifiers::CONTROL;
    }
    if raw & 0x08 != 0 {
        m |= Modifiers::ALT;
    }
    m
}
