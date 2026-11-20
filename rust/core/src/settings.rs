//! Reads `settings.ini`, which the Windows app writes as UTF-16 (or UTF-8 when edited by hand). Only what the
//! core needs is read; a missing file or line means the default, with the same limits as the app.

use std::collections::HashMap;
use std::path::Path;

#[derive(Debug, Clone, PartialEq)]
pub struct Settings {
    /// [Water] Goal, ml.
    pub water_goal: i32,
    /// [Water] Cup, ml: what one "+" adds.
    pub water_cup: i32,
    /// [Water] Reminders, From, Until (minutes since midnight).
    pub water_remind: bool,
    pub water_from: i32,
    pub water_until: i32,
    /// [ToDo] RemindStart / RemindEnd.
    pub todo_start: bool,
    pub todo_end: bool,
    /// [Calendar] Link (an iCal address), and Remind: minutes before an event (0 at its start, None = off).
    pub cal_link: String,
    pub cal_remind: Option<i32>,
    /// [Ask] Model.
    pub ask_model: String,
    /// [Music] Lyrics, Earlier (ms).
    pub lyrics: bool,
    pub lyrics_earlier: i32,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            water_goal: 2000,
            water_cup: 250,
            water_remind: false,
            water_from: 8 * 60,
            water_until: 17 * 60,
            todo_start: true,
            todo_end: true,
            cal_link: String::new(),
            cal_remind: Some(10),
            ask_model: "gemini-flash-latest".into(),
            lyrics: false,
            lyrics_earlier: 0,
        }
    }
}

impl Settings {
    pub fn load(path: &Path) -> Settings {
        let Ok(bytes) = std::fs::read(path) else { return Settings::default() };
        Settings::from_text(&decode(&bytes))
    }

    pub fn from_text(text: &str) -> Settings {
        let mut s = Settings::default();
        let ini = parse(text);
        let get = |sec: &str, key: &str| ini.get(&(sec.to_ascii_lowercase(), key.to_ascii_lowercase())).map(|v| v.trim().to_string());
        let num = |sec: &str, key: &str| get(sec, key).and_then(|v| v.parse::<i32>().ok());
        let flag = |sec: &str, key: &str, d: bool| match get(sec, key).as_deref() {
            Some("1") => true,
            Some("0") => false,
            _ => d,
        };
        if let Some(g) = num("Water", "Goal").filter(|g| [1500, 2000, 2500, 3000].contains(g)) {
            s.water_goal = g;
        }
        if let Some(c) = num("Water", "Cup").filter(|c| (100..=1000).contains(c)) {
            s.water_cup = c;
        }
        s.water_remind = flag("Water", "Reminders", s.water_remind);
        if let Some(t) = get("Water", "From").and_then(|v| crate::clock::parse_time(&v)) {
            s.water_from = t;
        }
        if let Some(t) = get("Water", "Until").and_then(|v| crate::clock::parse_time(&v)) {
            s.water_until = t;
        }
        if s.water_until <= s.water_from {
            s.water_until = (s.water_from + 60).min(24 * 60 - 1);
        }
        s.todo_start = flag("ToDo", "RemindStart", s.todo_start);
        s.todo_end = flag("ToDo", "RemindEnd", s.todo_end);
        if let Some(l) = get("Calendar", "Link") {
            s.cal_link = l;
        }
        match get("Calendar", "Remind").as_deref() {
            Some(v) if v.eq_ignore_ascii_case("off") => s.cal_remind = None,
            Some(v) => {
                if let Ok(m) = v.parse::<i32>() {
                    if [0, 5, 10, 15, 30].contains(&m) {
                        s.cal_remind = Some(m);
                    }
                }
            }
            None => {}
        }
        if let Some(m) = get("Ask", "Model").filter(|m| !m.is_empty() && m.len() < 100 && m.chars().all(|c| c.is_ascii_alphanumeric() || "-._".contains(c))) {
            s.ask_model = m;
        }
        s.lyrics = flag("Music", "Lyrics", s.lyrics);
        if let Some(e) = num("Music", "Earlier").filter(|e| [0, 300, 600, 1000].contains(e)) {
            s.lyrics_earlier = e;
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
        let text = "; Perch settings\r\n[Water]\r\nGoal=2500\r\nCup=300\r\nReminders=1\r\nFrom=09:30\r\n[Calendar]\r\nRemind=Off\r\n[Ask]\r\nModel=gemini-9-pro\r\n";
        let mut utf16 = vec![0xFF, 0xFE];
        for u in text.encode_utf16() {
            utf16.extend_from_slice(&u.to_le_bytes());
        }
        let dir = std::env::temp_dir().join(format!("perch-core-settings-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let f = dir.join("settings.ini");
        std::fs::write(&f, &utf16).unwrap();
        let s = Settings::load(&f);
        assert_eq!((s.water_goal, s.water_cup, s.water_remind, s.water_from, s.water_until), (2500, 300, true, 570, 1020));
        assert_eq!(s.cal_remind, None);
        assert_eq!(s.ask_model, "gemini-9-pro");
        std::fs::write(&f, "[water]\ngoal = 9999\ncup=150\n[Ask]\nModel=bad model!\n").unwrap(); // out of range / bad: defaults
        let s = Settings::load(&f);
        assert_eq!((s.water_goal, s.water_cup, s.ask_model.as_str()), (2000, 150, "gemini-flash-latest"));
        std::fs::remove_dir_all(&dir).ok();
    }
}
