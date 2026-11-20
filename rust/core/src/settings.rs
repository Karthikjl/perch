//! Reads `settings.ini`, which the Windows app writes as UTF-16 (or UTF-8 when edited by hand).
//! Only what the core needs is read; a missing file or line means the default, as in the app.

use std::collections::HashMap;
use std::path::Path;

#[derive(Debug, Clone, PartialEq)]
pub struct Settings {
    /// [Water] Goal, ml.
    pub water_goal: i32,
    /// [Water] Cup, ml: what one "+" adds.
    pub water_cup: i32,
}

impl Default for Settings {
    fn default() -> Self {
        Settings { water_goal: 2000, water_cup: 250 }
    }
}

impl Settings {
    pub fn load(path: &Path) -> Settings {
        let mut s = Settings::default();
        let Ok(bytes) = std::fs::read(path) else { return s };
        let ini = parse(&decode(&bytes));
        let num = |sec: &str, key: &str| ini.get(&(sec.to_ascii_lowercase(), key.to_ascii_lowercase())).and_then(|v| v.trim().parse::<i32>().ok());
        if let Some(g) = num("Water", "Goal").filter(|g| [1500, 2000, 2500, 3000].contains(g)) {
            s.water_goal = g;
        }
        if let Some(c) = num("Water", "Cup").filter(|c| (100..=1000).contains(c)) {
            s.water_cup = c;
        }
        s
    }
}

/// UTF-16 LE (with its BOM) or UTF-8 text.
pub fn decode(bytes: &[u8]) -> String {
    if bytes.starts_with(&[0xFF, 0xFE]) {
        let units: Vec<u16> = bytes[2..].chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect();
        return String::from_utf16_lossy(&units);
    }
    let b = bytes.strip_prefix(&[0xEF, 0xBB, 0xBF]).unwrap_or(bytes);
    String::from_utf8_lossy(b).into_owned()
}

/// (section, key) in lower case -> value. Comments start with ';'.
pub fn parse(text: &str) -> HashMap<(String, String), String> {
    let mut out = HashMap::new();
    let mut section = String::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with(';') {
            continue;
        }
        if let Some(name) = line.strip_prefix('[').and_then(|l| l.strip_suffix(']')) {
            section = name.trim().to_ascii_lowercase();
        } else if let Some((k, v)) = line.split_once('=') {
            out.insert((section.clone(), k.trim().to_ascii_lowercase()), v.trim().to_string());
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_utf16_and_utf8() {
        let text = "; Perch settings\r\n[Water]\r\nGoal=2500\r\nCup=300\r\n";
        let mut utf16 = vec![0xFF, 0xFE];
        for u in text.encode_utf16() {
            utf16.extend_from_slice(&u.to_le_bytes());
        }
        let dir = std::env::temp_dir().join(format!("perch-core-settings-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let f = dir.join("settings.ini");
        std::fs::write(&f, &utf16).unwrap();
        assert_eq!(Settings::load(&f), Settings { water_goal: 2500, water_cup: 300 });
        std::fs::write(&f, "[water]\ngoal = 9999\ncup=150\n").unwrap(); // an out-of-range goal keeps the default
        assert_eq!(Settings::load(&f), Settings { water_goal: 2000, water_cup: 150 });
        std::fs::remove_dir_all(&dir).ok();
    }
}
