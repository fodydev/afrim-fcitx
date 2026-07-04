use afrim_config::Config;
use afrim_preprocessor::{utils, Command, Preprocessor};
use afrim_preprocessor::{Key, KeyState, KeyboardEvent, NamedKey};
use afrim_translator::{Predicate, Translator};
use std::{path::Path, rc::Rc};


/// Owns a `Preprocessor` and a `Translator` initialised from a TOML config
/// file. This struct is intentionally **not** `Send` / `Sync` (inherited from
/// `Preprocessor` which uses `Rc<Node>`).
/// fcitx5 dispatches all IM events on its single UI thread, so that is perfectly fine.
pub struct AfrimEngine {
    preprocessor: Preprocessor,
    translator: Translator,
}

impl AfrimEngine {
    /// Load config from `config_path` and build the engine.
    pub fn from_config_path(config_path: &str) -> Result<Self, String> {
        let config = Config::from_file(Path::new(config_path)).map_err(|e| e.to_string())?;

        let auto_commit = config
            .core
            .as_ref()
            .and_then(|c| c.auto_commit)
            .unwrap_or(false);
        let buffer_size = config.core.as_ref().and_then(|c| c.buffer_size).unwrap_or(64);

        // Preprocessor
        let data_map = config.extract_data();
        let pairs: Vec<Vec<&str>> = data_map
            .iter()
            .map(|(k, v)| vec![k.as_str(), v.as_str()])
            .collect();
        let node = utils::build_map(pairs);
        drop(data_map);

        let preprocessor = Preprocessor::new(Rc::new(node), buffer_size);

        // Translator
        let translation = config.extract_translation();
        let mut translator = Translator::new(translation, auto_commit);

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

    /// Feed one key event to the preprocessor.
    ///
    /// Returns the commands generated after the event. An empty return
    /// means the key did not trigger any afrim action.
    pub fn process_key(
        &mut self,
        keysym: u32,
        key_str: &str,
    ) -> Vec<Command> {
        // Map the X11 keysym to a keyboard_types Key.
            // Unrecognised key (function keys, media keys, ...),  don't disturb
            // the preprocessor state.
        let key = keysym_to_key(keysym, key_str);

        let event = KeyboardEvent {
            key,
            state: KeyState::Down,
            ..Default::default()
        };

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

    /// Current content of the preprocessor's input buffer (may be empty).
    pub fn get_input(&self) -> String {
        self.preprocessor.get_input()
    }

    /// Translate the current input and return candidate predicates.
    pub fn get_candidates(&self) -> Vec<Predicate> {
        let input = self.preprocessor.get_input();

        self.translator.translate(&input)
    }

    /// Clear internal state.
    /// Should be called when the input context loses focus.
    pub fn reset(&mut self) {
        self.preprocessor.process(Default::default());
        self.preprocessor.clear_queue();
    }

    fn drain_queue(&mut self) -> Vec<Command> {
        let mut cmds = Vec::new();
        while let Some(cmd) = self.preprocessor.pop_queue() {
            cmds.push(cmd);
        }
        cmds
    }
}

/// Map an X11 keysym (plus an optional UTF-8 key label from fcitx5) to a
/// `keyboard_types::Key`. Returns `None` for keys that afrim should not
/// see (function keys, media keys, modifier-only events, etc.).
fn keysym_to_key(keysym: u32, key_str: &str) -> afrim_preprocessor::Key {
    let key = match keysym {
        // Named keys that afrim recognises
        0xFF08 => Some(Key::Named(NamedKey::Backspace)),
        // If fcitx5 provided a printable key string, use it.
        _ if !key_str.is_empty() => {
            Some(Key::Character(key_str.into()))
        }
        // Printable ASCII (basic Latin)
        0x0020..=0x007E => {
            char::from_u32(keysym).map(|c| Key::Character(c.to_string()))
        }

        // Non-ASCII printable (Latin extended, CJK, ...)
        // X11 Unicode keysyms sit at 0x01000000 + UCS-4 codepoint.
        0x01000080..=0x0110FFFF => {
            let ucs4 = keysym - 0x01000000;
            char::from_u32(ucs4).map(|c| Key::Character(c.to_string()))
        }

        _ => None
    };

    key.unwrap_or_default()
}
