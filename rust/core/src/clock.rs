//! Days and times as Perch's files write them.

use chrono::{Datelike, Local, Timelike};

/// Today as YYYYMMDD (local time), the way `tasks.txt` and `water.txt` name days.
pub fn today() -> i32 {
    let n = Local::now();
    n.year() * 10000 + n.month() as i32 * 100 + n.day() as i32
}

/// Minutes since midnight, now.
pub fn now_minute() -> i32 {
    let n = Local::now();
    (n.hour() * 60 + n.minute()) as i32
}

/// "18:30", "1830", "9:05" or "905" -> minutes since midnight.
pub fn parse_time(s: &str) -> Option<i32> {
    let s = s.trim();
    let (h, m) = if let Some((h, m)) = s.split_once(':') {
        (h.parse::<i32>().ok()?, m.parse::<i32>().ok()?)
    } else if s.len() == 3 || s.len() == 4 && s.chars().all(|c| c.is_ascii_digit()) {
        let n: i32 = s.parse().ok()?;
        (n / 100, n % 100)
    } else {
        return None;
    };
    ((0..24).contains(&h) && (0..60).contains(&m)).then_some(h * 60 + m)
}

/// Minutes since midnight -> "18:30".
pub fn format_time(minutes: i32) -> String {
    format!("{:02}:{:02}", minutes / 60, minutes % 60)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn times() {
        assert_eq!(parse_time("18:30"), Some(1110));
        assert_eq!(parse_time("1830"), Some(1110));
        assert_eq!(parse_time("9:05"), Some(545));
        assert_eq!(parse_time("905"), Some(545));
        assert_eq!(parse_time("24:00"), None);
        assert_eq!(parse_time("12:60"), None);
        assert_eq!(parse_time("noon"), None);
        assert_eq!(format_time(545), "09:05");
    }
}
