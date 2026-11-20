//! What perchctl can do, shared by the command line and the MCP server, so both behave the same.
//! Every change is written to Perch's files, then a running Perch is told to read them again.

use perch_core::app::{self, Changed};
use perch_core::settings::Settings;
use perch_core::{clock, paths, tasks::Task, tasks::TaskList, water, Error, Result};
use serde_json::{json, Value};

fn list() -> Result<TaskList> {
    TaskList::load(&paths::tasks_file(), clock::today())
}

fn task_json(n: usize, t: &Task) -> Value {
    let today = clock::today();
    json!({
        "number": n,
        "title": t.title,
        "start": (t.start >= 0).then(|| clock::format_time(t.start)),
        "end": (t.end >= 0).then(|| clock::format_time(t.end)),
        "daily": t.daily,
        "done": t.done(today),
    })
}

fn task_line(n: usize, t: &Task) -> String {
    let today = clock::today();
    let time = t.time_text();
    format!(
        "{n}. [{}] {}{}{}",
        if t.done(today) { "x" } else { " " },
        t.title,
        if time.is_empty() { String::new() } else { format!("  {time}") },
        if t.daily { "  (daily)" } else { "" }
    )
}

/// Result of an action: what to print for a person, and the same as JSON for scripts and AI tools.
pub struct Outcome {
    pub text: String,
    pub json: Value,
}

pub fn add_task(title: &str, at: Option<&str>, until: Option<&str>, daily: bool) -> Result<Outcome> {
    let time = |s: Option<&str>, what: &str| -> Result<Option<i32>> {
        s.map(|v| clock::parse_time(v).ok_or_else(|| Error(format!("{what} \"{v}\" isn't a time like 18:30"))))
            .transpose()
    };
    let task = Task::new(title, time(at, "start time")?, time(until, "end time")?, daily)?;
    let mut l = list()?;
    l.add(task)?;
    let n = l.tasks.len();
    let t = &l.tasks[n - 1];
    let shown = app::reload(Changed::Tasks);
    let time = t.time_text();
    Ok(Outcome {
        text: format!(
            "Added task {n}: {}{}{}",
            t.title,
            if time.is_empty() { String::new() } else { format!(" ({time})") },
            if t.daily { ", every day" } else { "" }
        ),
        json: json!({ "added": task_json(n, t), "perch_running": shown }),
    })
}

pub fn list_tasks() -> Result<Outcome> {
    let l = list()?;
    let today = clock::today();
    let done = l.tasks.iter().filter(|t| t.done(today)).count();
    let mut text = if l.tasks.is_empty() { "No tasks for today.".to_string() } else { format!("{done} of {} done today:", l.tasks.len()) };
    for (i, t) in l.tasks.iter().enumerate() {
        text.push('\n');
        text.push_str(&task_line(i + 1, t));
    }
    let items: Vec<Value> = l.tasks.iter().enumerate().map(|(i, t)| task_json(i + 1, t)).collect();
    Ok(Outcome { text, json: json!({ "tasks": items, "done": done }) })
}

pub fn set_done(n: usize, done: bool) -> Result<Outcome> {
    let mut l = list()?;
    let t = l.set_done(n, done, clock::today())?.clone();
    app::reload(Changed::Tasks);
    Ok(Outcome {
        text: format!("{} task {n}: {}", if done { "Done" } else { "Not done" }, t.title),
        json: json!({ "task": task_json(n, &t) }),
    })
}

pub fn remove_task(n: usize) -> Result<Outcome> {
    let mut l = list()?;
    let t = l.remove(n)?;
    app::reload(Changed::Tasks);
    Ok(Outcome { text: format!("Removed task {n}: {}", t.title), json: json!({ "removed": t.title }) })
}

fn water_status_of(ml: i32) -> (String, Value) {
    let s = Settings::load(&paths::settings_file());
    let text = format!("{} of {} today", water::litres(ml), water::litres(s.water_goal));
    (text, json!({ "ml": ml, "goal_ml": s.water_goal, "cup_ml": s.water_cup, "goal_reached": ml >= s.water_goal }))
}

/// Adds `ml` (default: one cup from the settings) to today's water.
pub fn add_water(ml: Option<i32>) -> Result<Outcome> {
    let cup = Settings::load(&paths::settings_file()).water_cup;
    let ml = ml.unwrap_or(cup);
    if ml == 0 || ml.abs() > 5000 {
        return Err(Error("give an amount between 1 and 5000 ml (or a negative one to take some back)".into()));
    }
    let day = water::add(&paths::water_file(), clock::today(), ml)?;
    app::reload(Changed::Water);
    let (status, json) = water_status_of(day.ml);
    let verb = if ml > 0 { format!("Added {}", water::litres(ml)) } else { format!("Took back {}", water::litres(-ml)) };
    Ok(Outcome { text: format!("{verb}: {status}"), json })
}

pub fn water_status() -> Result<Outcome> {
    let day = water::today(&paths::water_file(), clock::today());
    let (text, json) = water_status_of(day.ml);
    Ok(Outcome { text: format!("Water: {text}"), json })
}

// ---- calendar ----

/// Events from the user's calendar link (downloaded now, or the copy the app kept when offline): `days` days,
/// starting `skip` days from today.
pub fn calendar(skip: i32, days: i32) -> Result<Outcome> {
    use perch_core::calendar;
    let s = Settings::load(&paths::settings_file());
    if s.cal_link.is_empty() {
        return Err(Error("no calendar link yet: right-click the notch > Paste calendar link, or set [Calendar] Link in settings.ini".into()));
    }
    let cache = paths::data_dir().join("calendar.ics");
    let (ics, offline) = match calendar::download(&s.cal_link) {
        Ok(t) => {
            let _ = std::fs::write(&cache, &t);
            (t, false)
        }
        Err(()) => (std::fs::read_to_string(&cache).map_err(|_| Error("couldn't reach your calendar, and there's no saved copy".into()))?, true),
    };
    let today = calendar::today();
    let first = today + skip;
    let events = calendar::parse(&ics, first, first + days - 1).ok_or_else(|| Error("that link isn't an iCal calendar".into()))?;
    let mut text = String::new();
    if offline {
        text.push_str("(Couldn't reach your calendar: this is the last copy.)\n");
    }
    let mut items = Vec::new();
    for d in first..first + days {
        let (y, m, dd) = calendar::date(d);
        let on: Vec<_> = events.iter().filter(|e| e.day == d).collect();
        let name = match d - today { 0 => "Today".to_string(), 1 => "Tomorrow".to_string(), _ => format!("{y}-{m:02}-{dd:02}") };
        text.push_str(&format!("{name}:{}\n", if on.is_empty() { " nothing" } else { "" }));
        for e in on {
            let time = if e.start < 0 { "all day".to_string() } else if e.end >= 0 { format!("{}-{}", clock::format_time(e.start), clock::format_time(e.end)) } else { clock::format_time(e.start) };
            text.push_str(&format!("  {time}  {}\n", e.title));
            items.push(json!({ "date": format!("{y}-{m:02}-{dd:02}"), "start": (e.start >= 0).then(|| clock::format_time(e.start)), "end": (e.end >= 0 && e.start >= 0).then(|| clock::format_time(e.end)), "all_day": e.start < 0, "title": e.title }));
        }
    }
    Ok(Outcome { text: text.trim_end().to_string(), json: json!({ "events": items, "offline_copy": offline }) })
}

// ---- Ask Perch ----

/// One question to Gemini with the saved key and model. A task the model asks for is added for real.
pub fn ask(question: &str) -> Result<Outcome> {
    use perch_core::ask;
    if question.trim().is_empty() {
        return Err(Error("ask a question: perchctl ask \"What's 15% of 80?\"".into()));
    }
    let key = perch_core::secrets::gemini_key().ok_or_else(|| Error("no Gemini key: add one in Perch (Settings > Ask Perch > Paste key), or set PERCH_GEMINI_KEY".into()))?;
    let model = Settings::load(&paths::settings_file()).ask_model;
    let a = ask::generate(&key, &model, &[ask::Turn { user: true, text: question.to_string() }], clock::now_minute()).map_err(Error)?;
    let mut text = a.text.clone();
    let mut added = Value::Null;
    if let Some(t) = &a.task {
        let s = |m: Option<i32>| m.map(clock::format_time);
        let o = add_task(&t.title, s(t.start).as_deref(), s(t.end).as_deref(), t.daily)?;
        if !text.is_empty() {
            text.push_str("\n\n");
        }
        text.push_str(&o.text);
        added = o.json;
    }
    Ok(Outcome { text, json: json!({ "answer": a.text, "model": model, "task": added }) })
}

// ---- updates ----

pub fn update_check() -> Result<Outcome> {
    use perch_core::update;
    let r = update::check(if cfg!(windows) { "Perch.exe" } else { "perch" }).map_err(|s| Error(if s == 0 { "couldn't reach GitHub".into() } else { format!("GitHub answered {s}") }))?;
    let newer = update::newer(&r.version, update::VERSION);
    let text = if newer { format!("Perch {} is available (you have {}). Install it from the notch: right-click > Install update.", r.version, update::VERSION) } else { format!("Perch is up to date ({}).", update::VERSION) };
    Ok(Outcome { text, json: json!({ "current": update::VERSION, "latest": r.version, "newer": newer, "download": r.url }) })
}
