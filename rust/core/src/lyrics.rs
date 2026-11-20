//! Synced lyrics from LRCLIB (lrclib.net), a free community lyrics database. Only used when the user turns lyrics
//! on: it sends the song's title, artist and length.

use crate::net;
use serde_json::Value;

#[derive(Debug, Clone, PartialEq)]
pub struct Line {
    /// When the line starts, in ms from the start of the song.
    pub ms: i32,
    pub text: String,
}

pub enum Found {
    Lines(Vec<Line>),
    /// The song has no synced lyrics there.
    None,
    /// The service couldn't be reached.
    Offline,
}

/// https://lrclib.net/api, or PERCH_LRCLIB_URL (tests).
pub fn base_url() -> String {
    std::env::var("PERCH_LRCLIB_URL").ok().filter(|s| !s.is_empty()).unwrap_or_else(|| "https://lrclib.net/api".into())
}

/// "[01:02.50] text" lines (LRC) -> lines sorted by time; tags like [ar:...] and lines with no time are skipped,
/// and a line with several times counts once for each.
pub fn parse(lrc: &str) -> Vec<Line> {
    let mut out = Vec::new();
    for line in lrc.lines() {
        let mut rest = line;
        let mut times = Vec::new();
        while let Some(r) = rest.strip_prefix('[') {
            let Some(close) = r.find(']') else { break };
            if let Some((m, s)) = r[..close].split_once(':') {
                if let (Ok(m), Ok(s)) = (m.trim().parse::<i32>(), s.trim().parse::<f64>()) {
                    if m >= 0 && s >= 0.0 {
                        times.push((m as f64 * 60000.0 + s * 1000.0).round() as i32);
                    }
                }
            }
            rest = &r[close + 1..];
        }
        let text = rest.trim().to_string();
        out.extend(times.into_iter().map(|ms| Line { ms, text: text.clone() }));
    }
    out.sort_by_key(|l| l.ms);
    out
}

/// The line being sung at `ms` (None: before the first line). `earlier` shows lines that many ms early.
pub fn line_at(lines: &[Line], ms: i32, earlier: i32) -> Option<usize> {
    let ms = ms + earlier;
    lines.iter().rposition(|l| l.ms <= ms)
}

/// Looks the song up: the exact match by title, artist and length first, then the first search result that has
/// synced lyrics.
pub fn fetch(title: &str, artist: &str, seconds: i32) -> Found {
    let q = format!("track_name={}&artist_name={}", net::url_encode(title), net::url_encode(artist));
    let base = base_url();
    let synced = |body: &[u8]| -> Vec<String> {
        let v: Value = serde_json::from_slice(body).unwrap_or(Value::Null);
        let items = if v.is_array() { v.as_array().cloned().unwrap_or_default() } else { vec![v] };
        items.iter().filter_map(|i| i.get("syncedLyrics").and_then(Value::as_str).map(str::to_string)).collect()
    };
    let exact = format!("{base}/get?{q}{}", if seconds > 0 { format!("&duration={seconds}") } else { String::new() });
    let r = net::request("GET", &exact, &[], None, 4 << 20);
    if r.status == 0 {
        return Found::Offline;
    }
    let mut found = if r.status == 200 { synced(&r.body) } else { vec![] };
    if found.is_empty() {
        let r = net::request("GET", &format!("{base}/search?{q}"), &[], None, 4 << 20);
        if r.status == 0 {
            return Found::Offline;
        }
        if r.status == 200 {
            found = synced(&r.body);
        }
    }
    for s in found {
        let lines = parse(&s);
        if !lines.is_empty() {
            return Found::Lines(lines);
        }
    }
    Found::None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_lrc() {
        let l = parse("[ar:The Band]\n[00:01.00]First line\n[00:05.50] Second line \n[00:09.00][00:12.00]Chorus\nno time\n");
        assert_eq!(l.iter().map(|l| l.ms).collect::<Vec<_>>(), [1000, 5500, 9000, 12000]);
        assert_eq!(l[1].text, "Second line");
        assert_eq!(line_at(&l, 999, 0), None);
        assert_eq!(line_at(&l, 5499, 0), Some(0));
        assert_eq!(line_at(&l, 4900, 600), Some(1));
        assert_eq!(line_at(&l, 60000, 0), Some(3));
    }

    #[test]
    fn fetches_with_a_fallback() {
        let url = net::testserver::start(|line, _| {
            if line.contains("/get?track_name=Song%20One&artist_name=The%20Band&duration=200") {
                (200, r#"{"syncedLyrics":"[00:01.00]First"}"#.into())
            } else if line.contains("/search?track_name=Other") {
                (200, r#"[{"syncedLyrics":null},{"syncedLyrics":"[00:02.00]Found by search"}]"#.into())
            } else if line.contains("/search") {
                (200, "[]".into())
            } else {
                (404, r#"{"code":404}"#.into())
            }
        });
        std::env::set_var("PERCH_LRCLIB_URL", format!("{url}/api"));
        assert!(matches!(fetch("Song One", "The Band", 200), Found::Lines(l) if l[0].text == "First"));
        assert!(matches!(fetch("Other", "The Band", 0), Found::Lines(l) if l[0].text == "Found by search"));
        assert!(matches!(fetch("Unknown", "Nobody", 100), Found::None));
        std::env::set_var("PERCH_LRCLIB_URL", "http://127.0.0.1:1/api");
        assert!(matches!(fetch("x", "y", 1), Found::Offline));
        std::env::remove_var("PERCH_LRCLIB_URL");
    }
}
