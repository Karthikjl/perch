//! Calendar: reads an iCalendar (.ics) file, such as Google Calendar's "secret address in iCal format", into one
//! entry per day, repeating events included (DAILY / WEEKLY / MONTHLY / YEARLY with INTERVAL, COUNT, UNTIL,
//! BYDAY, BYMONTHDAY), skipping EXDATEs and cancelled events, and letting a moved repeat (RECURRENCE-ID)
//! replace its day. Times with a Z are UTC and become local; others are taken as local time.

use chrono::{Datelike, Local, NaiveDate, TimeZone, Timelike, Utc};
use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Event {
    /// Day number (days since 1970-01-01).
    pub day: i32,
    /// Minutes since midnight, local time; -1 for an all-day event.
    pub start: i32,
    pub end: i32,
    pub title: String,
}

// ---- days ----
pub fn day_number(y: i32, m: i32, d: i32) -> i32 {
    NaiveDate::from_ymd_opt(y, m as u32, d as u32)
        .map(|n| (n - NaiveDate::from_ymd_opt(1970, 1, 1).unwrap()).num_days() as i32)
        .unwrap_or(0)
}
pub fn date(day: i32) -> (i32, i32, i32) {
    let n = NaiveDate::from_ymd_opt(1970, 1, 1).unwrap() + chrono::Duration::days(day as i64);
    (n.year(), n.month() as i32, n.day() as i32)
}
/// 0 = Sunday.
pub fn weekday(day: i32) -> i32 {
    ((day % 7) + 11) % 7
}
pub fn today() -> i32 {
    let n = Local::now();
    day_number(n.year(), n.month() as i32, n.day() as i32)
}
fn days_in_month(y: i32, m: i32) -> i32 {
    let (ny, nm) = if m == 12 { (y + 1, 1) } else { (y, m + 1) };
    day_number(ny, nm, 1) - day_number(y, m, 1)
}

#[derive(Clone, Copy, Default)]
struct When {
    day: i32,
    minute: i32, // -1: a date (all day)
    ok: bool,
}

/// "20261119", "20261119T093000" (local), "20261119T093000Z" (UTC -> local).
fn parse_when(v: &str) -> When {
    let b = v.as_bytes();
    if b.len() < 8 || !b[..8].iter().all(u8::is_ascii_digit) {
        return When::default();
    }
    let num = |a: usize, n: usize| v.get(a..a + n).and_then(|s| s.parse::<i32>().ok()).unwrap_or(-1);
    let (y, mo, d) = (num(0, 4), num(4, 2), num(6, 2));
    if !(1..=12).contains(&mo) || !(1..=31).contains(&d) {
        return When::default();
    }
    let mut w = When { day: day_number(y, mo, d), minute: -1, ok: true };
    if b.len() >= 13 && b[8] == b'T' {
        let (mut h, mut mi) = (num(9, 2).max(0), num(11, 2).max(0));
        if b.len() >= 16 && b[15] == b'Z' {
            if let Some(u) = Utc.with_ymd_and_hms(y, mo as u32, d as u32, h as u32, mi as u32, 0).single() {
                let l = u.with_timezone(&Local);
                w.day = day_number(l.year(), l.month() as i32, l.day() as i32);
                h = l.hour() as i32;
                mi = l.minute() as i32;
            }
        }
        w.minute = h * 60 + mi;
    }
    w
}

/// "PT1H30M", "P1D" -> minutes.
fn parse_duration(v: &str) -> i32 {
    let (mut total, mut num) = (0, 0);
    for c in v.chars() {
        if let Some(d) = c.to_digit(10) {
            num = num * 10 + d as i32;
        } else {
            total += num * match c { 'W' => 7 * 1440, 'D' => 1440, 'H' => 60, 'M' => 1, _ => 0 };
            num = 0;
        }
    }
    if v.starts_with('-') { -total } else { total }
}

/// iCal text: \n, \, \; \\ (a line break becomes a space: titles are one line).
fn unescape(v: &str) -> String {
    let mut o = String::new();
    let mut it = v.chars();
    while let Some(c) = it.next() {
        if c == '\\' {
            match it.next() {
                Some('n') | Some('N') => o.push(' '),
                Some(n) => o.push(n),
                None => o.push('\\'),
            }
        } else {
            o.push(c);
        }
    }
    o
}

#[derive(Clone, Copy)]
struct ByDay {
    n: i32, // 0 every, 2 second, -1 last
    wd: i32,
}

#[derive(Clone)]
struct Rule {
    freq: i32, // 0 none, 1 daily, 2 weekly, 3 monthly, 4 yearly, -1 unknown
    interval: i32,
    count: i32,
    until_day: i32,
    by_day: Vec<ByDay>,
    by_month_day: Vec<i32>,
}

impl Default for Rule {
    fn default() -> Self {
        Rule { freq: 0, interval: 1, count: 0, until_day: i32::MAX, by_day: vec![], by_month_day: vec![] }
    }
}

fn parse_rule(v: &str) -> Rule {
    let mut r = Rule::default();
    for part in v.split(';') {
        let Some((k, val)) = part.split_once('=') else { continue };
        match k {
            "FREQ" => r.freq = match val { "DAILY" => 1, "WEEKLY" => 2, "MONTHLY" => 3, "YEARLY" => 4, _ => -1 },
            "INTERVAL" => r.interval = val.parse::<i32>().unwrap_or(1).max(1),
            "COUNT" => r.count = val.parse().unwrap_or(0),
            "UNTIL" => {
                let u = parse_when(val);
                if u.ok {
                    r.until_day = u.day;
                }
            }
            "BYMONTHDAY" => r.by_month_day.extend(val.split(',').filter_map(|t| t.parse::<i32>().ok())),
            "BYDAY" => {
                for t in val.split(',').filter(|t| t.len() >= 2) {
                    let (n, code) = t.split_at(t.len() - 2);
                    if let Some(wd) = ["SU", "MO", "TU", "WE", "TH", "FR", "SA"].iter().position(|c| *c == code) {
                        r.by_day.push(ByDay { n: n.parse().unwrap_or(0), wd: wd as i32 });
                    }
                }
            }
            _ => {}
        }
    }
    r
}

#[derive(Clone, Default)]
struct VEvent {
    uid: String,
    summary: String,
    start: When,
    end: When,
    recurrence_id: When,
    duration: Option<i32>,
    cancelled: bool,
    rule: Option<Rule>,
    ex_days: Vec<i32>,
}

/// Days of month (y, m) a monthly rule picks, in order.
fn month_days(y: i32, m: i32, r: &Rule, start_dom: i32) -> Vec<i32> {
    let (first, n) = (day_number(y, m, 1), days_in_month(y, m));
    let mut days = Vec::new();
    if !r.by_month_day.is_empty() {
        for &md in &r.by_month_day {
            let d = if md > 0 { md } else { n + md + 1 };
            if (1..=n).contains(&d) {
                days.push(first + d - 1);
            }
        }
    } else if !r.by_day.is_empty() {
        for b in &r.by_day {
            let all: Vec<i32> = (0..n).map(|d| first + d).filter(|&d| weekday(d) == b.wd).collect();
            if b.n == 0 {
                days.extend(&all);
            } else if b.n > 0 && b.n as usize <= all.len() {
                days.push(all[b.n as usize - 1]);
            } else if b.n < 0 && (-b.n) as usize <= all.len() {
                days.push(all[(all.len() as i32 + b.n) as usize]);
            }
        }
    } else if start_dom <= n {
        days.push(first + start_dom - 1);
    }
    days.sort();
    days.dedup();
    days
}

/// Every day the event happens on, from its start up to `to_day` (and the rule's end).
fn occurrences(e: &VEvent, to_day: i32) -> Vec<i32> {
    let s = e.start.day;
    let Some(r) = e.rule.as_ref().filter(|r| r.freq > 0) else { return vec![s] };
    let last = to_day.min(r.until_day);
    let mut out = Vec::new();
    let mut made = 0;
    // false: stop
    let mut add = |d: i32, out: &mut Vec<i32>| -> bool {
        if d < s {
            return true;
        }
        if d > last || (r.count > 0 && made >= r.count) {
            return false;
        }
        made += 1;
        out.push(d);
        true
    };
    let (y, m, dom) = date(s);
    for k in 0..5000 {
        match r.freq {
            1 => {
                if !add(s + k * r.interval, &mut out) {
                    break;
                }
            }
            2 => {
                let week_start = s - (weekday(s) + 6) % 7 + k * 7 * r.interval; // weeks start on Monday (iCal default)
                if week_start > last {
                    break;
                }
                let mut cand: Vec<i32> = if r.by_day.is_empty() { vec![week_start + (weekday(s) + 6) % 7] } else { vec![] };
                cand.extend(r.by_day.iter().map(|b| week_start + (b.wd + 6) % 7));
                cand.sort();
                if !cand.into_iter().all(|d| add(d, &mut out)) {
                    break;
                }
            }
            3 => {
                let mm = m - 1 + k * r.interval;
                let (yy, mm) = (y + mm / 12, mm % 12 + 1);
                if day_number(yy, mm, 1) > last {
                    break;
                }
                if !month_days(yy, mm, r, dom).into_iter().all(|d| add(d, &mut out)) {
                    break;
                }
            }
            4 => {
                let yy = y + k * r.interval;
                if day_number(yy, 1, 1) > last {
                    break;
                }
                if dom <= days_in_month(yy, m) && !add(day_number(yy, m, dom), &mut out) {
                    break;
                }
            }
            _ => break,
        }
    }
    out
}

/// Reads iCalendar text. None when it isn't one (no BEGIN:VCALENDAR). Events on days `from..=to`, sorted by
/// day, time and title.
pub fn parse(ics: &str, from: i32, to: i32) -> Option<Vec<Event>> {
    if !ics.contains("BEGIN:VCALENDAR") {
        return None;
    }
    // Unfold: a line starting with a space or tab continues the one before.
    let mut lines: Vec<String> = Vec::new();
    for l in ics.split('\n') {
        let l = l.strip_suffix('\r').unwrap_or(l);
        if (l.starts_with(' ') || l.starts_with('\t')) && !lines.is_empty() {
            lines.last_mut().unwrap().push_str(&l[1..]);
        } else {
            lines.push(l.to_string());
        }
    }

    let mut events = Vec::new();
    let mut cur = VEvent::default();
    let (mut inside, mut depth) = (false, 0);
    for l in &lines {
        if l == "BEGIN:VEVENT" {
            cur = VEvent::default();
            inside = true;
            depth = 0;
            continue;
        }
        if !inside {
            continue;
        }
        if l == "END:VEVENT" {
            if cur.start.ok {
                events.push(cur.clone());
            }
            inside = false;
            continue;
        }
        if l.starts_with("BEGIN:") {
            depth += 1;
            continue;
        }
        if l.starts_with("END:") {
            depth -= 1;
            continue;
        }
        if depth > 0 {
            continue; // VALARM and such
        }
        let mut quoted = false;
        let Some(colon) = l.char_indices().find(|&(_, c)| {
            if c == '"' {
                quoted = !quoted;
            }
            c == ':' && !quoted
        }).map(|(i, _)| i) else { continue };
        let (head, value) = (&l[..colon], &l[colon + 1..]);
        match head.split(';').next().unwrap_or("") {
            "DTSTART" => cur.start = parse_when(value),
            "DTEND" => cur.end = parse_when(value),
            "DURATION" => cur.duration = Some(parse_duration(value)),
            "SUMMARY" => cur.summary = unescape(value),
            "UID" => cur.uid = value.to_string(),
            "RRULE" => cur.rule = Some(parse_rule(value)),
            "RECURRENCE-ID" => cur.recurrence_id = parse_when(value),
            "STATUS" => cur.cancelled = value == "CANCELLED",
            "EXDATE" => cur.ex_days.extend(value.split(',').map(parse_when).filter(|w| w.ok).map(|w| w.day)),
            _ => {}
        }
    }

    // A changed or cancelled repeat comes as its own event with RECURRENCE-ID: it replaces that day of the series.
    let mut moved: HashMap<String, Vec<i32>> = HashMap::new();
    for e in events.iter().filter(|e| e.recurrence_id.ok) {
        moved.entry(e.uid.clone()).or_default().push(e.recurrence_id.day);
    }

    let mut out = Vec::new();
    for e in events.iter().filter(|e| !e.cancelled) {
        let all_day = e.start.minute < 0;
        let start = e.start.minute;
        let (mut end, mut span) = (-1, 1);
        if all_day {
            if e.end.ok && e.end.day > e.start.day {
                span = e.end.day - e.start.day;
            } else if let Some(d) = e.duration.filter(|d| *d > 0) {
                span = (d / 1440).max(1);
            }
        } else if e.end.ok {
            end = if e.end.day == e.start.day { e.end.minute } else if e.end.day > e.start.day { 24 * 60 } else { -1 };
        } else if let Some(d) = e.duration.filter(|d| *d >= 0) {
            end = (start + d).min(24 * 60);
        }
        if end == 24 * 60 && !all_day {
            end = 0; // ends at midnight: shown as "-00:00"
        }
        let mv = if e.recurrence_id.ok { None } else { moved.get(&e.uid) };
        for d in occurrences(e, to) {
            if e.ex_days.contains(&d) || mv.is_some_and(|m| m.contains(&d)) {
                continue;
            }
            for k in 0..span {
                let day = d + k;
                if day >= from && day <= to {
                    let title = if e.summary.is_empty() { "(No title)".to_string() } else { e.summary.clone() };
                    out.push(Event { day, start: if all_day { -1 } else { start }, end: if all_day { -1 } else { end }, title });
                }
            }
        }
    }
    out.sort_by(|a, b| (a.day, a.start, &a.title).cmp(&(b.day, b.start, &b.title)));
    Some(out)
}

/// Downloads a calendar (https, http or webcal). Err: couldn't reach it (network, HTTP error).
pub fn download(url: &str) -> Result<String, ()> {
    let url = match url.get(..9) {
        Some(p) if p.eq_ignore_ascii_case("webcal://") => format!("https://{}", &url[9..]),
        _ => url.to_string(),
    };
    let r = crate::net::request("GET", &url, &[], None, 32 << 20);
    if r.status == 200 { Ok(r.text()) } else { Err(()) }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn d(y: i32, m: i32, dd: i32) -> i32 {
        day_number(y, m, dd)
    }

    #[test]
    fn days() {
        assert_eq!(day_number(1970, 1, 1), 0);
        assert_eq!(date(d(2026, 11, 20)), (2026, 11, 20));
        assert_eq!(weekday(d(2026, 11, 20)), 5); // a Friday
        assert_eq!(days_in_month(2028, 2), 29);
    }

    #[test]
    fn events_and_repeats() {
        let ics = "BEGIN:VCALENDAR\r\n\
BEGIN:VEVENT\r\nUID:a\r\nDTSTART:20261120T100000\r\nDTEND:20261120T110000\r\nSUMMARY:Standup\r\nEND:VEVENT\r\n\
BEGIN:VEVENT\r\nUID:h\r\nDTSTART;VALUE=DATE:20261121\r\nDTEND;VALUE=DATE:20261123\r\nSUMMARY:Holiday\r\nEND:VEVENT\r\n\
BEGIN:VEVENT\r\nUID:w\r\nDTSTART:20261106T090000\r\nDURATION:PT30M\r\nRRULE:FREQ=WEEKLY;COUNT=5\r\nEXDATE:20261127T090000\r\nSUMMARY:Weekly review\r\nEND:VEVENT\r\n\
BEGIN:VEVENT\r\nUID:g\r\nDTSTART:20261119T180000\r\nRRULE:FREQ=DAILY;COUNT=4\r\nSUMMARY:Gym\r\nEND:VEVENT\r\n\
BEGIN:VEVENT\r\nUID:g\r\nRECURRENCE-ID:20261121T180000\r\nDTSTART:20261123T180000\r\nSUMMARY:Gym (moved)\r\nEND:VEVENT\r\n\
BEGIN:VEVENT\r\nUID:m\r\nDTSTART:20261127T160000\r\nRRULE:FREQ=MONTHLY;BYDAY=-1FR\r\nSUMMARY:Month end\r\nEND:VEVENT\r\n\
BEGIN:VEVENT\r\nUID:c\r\nDTSTART:20261120T150000\r\nSTATUS:CANCELLED\r\nSUMMARY:Cancelled\r\nEND:VEVENT\r\n\
BEGIN:VEVENT\r\nUID:l\r\nDTSTART:20261124T130000\r\nSUMMARY:Lunch\\, with the team and a title long enough to be fol\r\n ded\r\nBEGIN:VALARM\r\nSUMMARY:not this\r\nEND:VALARM\r\nEND:VEVENT\r\n\
END:VCALENDAR\r\n";
        let ev = parse(ics, d(2026, 11, 1), d(2027, 1, 31)).unwrap();
        let on = |day: i32| ev.iter().filter(|e| e.day == day).map(|e| e.title.as_str()).collect::<Vec<_>>();
        assert_eq!(on(d(2026, 11, 20)), ["Weekly review", "Standup", "Gym"]);
        assert_eq!(ev.iter().find(|e| e.title == "Weekly review").unwrap().end, 9 * 60 + 30, "DURATION gives the end");
        assert_eq!(on(d(2026, 11, 21)), ["Holiday"], "the moved repeat leaves its old day");
        assert_eq!(on(d(2026, 11, 22)), ["Holiday", "Gym"]);
        assert_eq!(on(d(2026, 11, 23)), ["Gym (moved)"]);
        assert_eq!(on(d(2026, 11, 27)), ["Month end"], "the EXDATE is skipped");
        assert_eq!(on(d(2026, 12, 25)), ["Month end"], "last Friday of December");
        assert_eq!(on(d(2027, 1, 29)), ["Month end"]);
        assert_eq!(on(d(2026, 11, 24)), ["Lunch, with the team and a title long enough to be folded"]);
        assert_eq!(ev.iter().filter(|e| e.title == "Weekly review").count(), 4, "COUNT=5 with one skipped");
        assert!(parse("<html>", 0, 1).is_none());
    }

    #[test]
    fn downloads() {
        let url = crate::net::testserver::start(|line, _| {
            if line.starts_with("GET /cal.ics") { (200, "BEGIN:VCALENDAR\nEND:VCALENDAR".into()) } else { (404, "no".into()) }
        });
        assert!(download(&format!("{url}/cal.ics")).unwrap().contains("VCALENDAR"));
        assert!(download(&format!("{url}/gone.ics")).is_err());
        assert!(download("http://127.0.0.1:1/x.ics").is_err());
    }
}
