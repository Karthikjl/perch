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
