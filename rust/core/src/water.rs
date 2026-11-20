//! Water, in the same `water.txt` the Windows app keeps: one line per day, `YYYYMMDD ml`.

use crate::Result;
use std::path::Path;

#[derive(Debug, Clone, PartialEq)]
pub struct WaterDay {
    pub day: i32,
    pub ml: i32,
}

/// Today's water (0 when nothing is logged yet).
pub fn today(path: &Path, today: i32) -> WaterDay {
    let ml = read(path).into_iter().filter(|(d, _)| *d == today).map(|(_, ml)| ml).last().unwrap_or(0);
    WaterDay { day: today, ml }
}

/// Adds `ml` to today (a negative amount takes some back; never below 0 or above 20 L) and saves.
pub fn add(path: &Path, today_day: i32, ml: i32) -> Result<WaterDay> {
    let mut days: Vec<(i32, i32)> = read(path).into_iter().filter(|(d, _)| *d != today_day).collect();
    let now = (today(path, today_day).ml + ml).clamp(0, 20000);
    days.push((today_day, now));
    if days.len() > 400 {
        days.drain(..days.len() - 400); // about a year, as the app keeps
    }
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let text: String = days.iter().map(|(d, ml)| format!("{d} {ml}\n")).collect();
    let tmp = path.with_extension("txt.tmp");
    std::fs::write(&tmp, text)?;
    std::fs::rename(&tmp, path)?;
    Ok(WaterDay { day: today_day, ml: now })
}

fn read(path: &Path) -> Vec<(i32, i32)> {
    let Ok(text) = std::fs::read_to_string(path) else { return Vec::new() };
    text.lines()
        .filter_map(|l| {
            let mut p = l.split_whitespace();
            Some((p.next()?.parse().ok()?, p.next()?.parse::<i32>().ok()?.max(0)))
        })
        .collect()
}

/// 1250 -> "1.25 L", 2000 -> "2 L", 300 -> "300 ml".
pub fn litres(ml: i32) -> String {
    if ml < 1000 {
        return format!("{ml} ml");
    }
    let s = format!("{:.2}", ml as f64 / 1000.0);
    format!("{} L", s.trim_end_matches('0').trim_end_matches('.'))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn adds_and_keeps_other_days() {
        let dir = std::env::temp_dir().join(format!("perch-core-water-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let f = dir.join("water.txt");
        std::fs::write(&f, "20261119 1500\n20261120 250\n").unwrap();
        assert_eq!(today(&f, 20261120).ml, 250);
        assert_eq!(add(&f, 20261120, 500).unwrap().ml, 750);
        assert_eq!(add(&f, 20261120, -2000).unwrap().ml, 0);
        assert_eq!(std::fs::read_to_string(&f).unwrap(), "20261119 1500\n20261120 0\n");
        assert_eq!(litres(1250), "1.25 L");
        assert_eq!(litres(2000), "2 L");
        assert_eq!(litres(300), "300 ml");
        std::fs::remove_dir_all(&dir).ok();
    }
}
