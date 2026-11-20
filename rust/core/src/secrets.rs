//! The Gemini key for Ask Perch. On Windows it's the app's own `ask.key`, encrypted for this Windows account
//! (DPAPI), so the command line and the notch share one key. Elsewhere, until the notch exists there, it comes
//! from the PERCH_GEMINI_KEY environment variable.

use crate::paths;

/// The saved key, or None.
pub fn gemini_key() -> Option<String> {
    if let Ok(k) = std::env::var("PERCH_GEMINI_KEY") {
        if !k.trim().is_empty() {
            return Some(k.trim().to_string());
        }
    }
    platform::read(&paths::data_dir().join("ask.key"))
}

#[cfg(windows)]
mod platform {
    use std::path::Path;
    use windows_sys::Win32::Foundation::LocalFree;
    use windows_sys::Win32::Security::Cryptography::{CryptUnprotectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB};

    pub fn read(path: &Path) -> Option<String> {
        let mut enc = std::fs::read(path).ok()?;
        let input = CRYPT_INTEGER_BLOB { cbData: enc.len() as u32, pbData: enc.as_mut_ptr() };
        let mut out = CRYPT_INTEGER_BLOB { cbData: 0, pbData: std::ptr::null_mut() };
        unsafe {
            if CryptUnprotectData(&input, std::ptr::null_mut(), std::ptr::null(), std::ptr::null(), std::ptr::null(), CRYPTPROTECT_UI_FORBIDDEN, &mut out) == 0 {
                return None;
            }
            // the app stores the key as UTF-16
            let units = std::slice::from_raw_parts(out.pbData as *const u16, out.cbData as usize / 2).to_vec();
            std::ptr::write_bytes(out.pbData, 0, out.cbData as usize);
            LocalFree(out.pbData as _);
            String::from_utf16(&units).ok().filter(|k| !k.is_empty())
        }
    }
}

#[cfg(not(windows))]
mod platform {
    pub fn read(_path: &std::path::Path) -> Option<String> {
        None
    }
}
