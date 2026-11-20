//! Today's to-do list, in the same `tasks.txt` the Windows app keeps: one task per line, tab-separated,
//! `doneDay  daily  startShown  start  end  endShown  title` (days as YYYYMMDD, 0 = not; times as minutes
//! since midnight, -1 = none), UTF-8 with a BOM. A daily task counts as done only on the day it was ticked;
//! a one-off task done on an earlier day is dropped when the list is read.

use crate::clock;
use crate::{Error, Result};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq)]
pub struct Task {
    pub title: String,
    /// Minutes since midnight, or -1.
    pub start: i32,
    pub end: i32,
    pub daily: bool,
    pub done_day: i32,
    pub start_shown: i32,
    pub end_shown: i32,
}

impl Task {
    pub fn new(title: &str, start: Option<i32>, end: Option<i32>, daily: bool) -> Result<Task> {
        let title: String = title.chars().filter(|c| !matches!(c, '\t' | '\r' | '\n')).collect();
        let title = title.trim();
        if title.is_empty() {
            return Err(Error("a task needs a title".into()));
        }
        if title.chars().count() > 60 {
            return Err(Error("a task's title can be at most 60 characters".into()));
        }
        let start = start.unwrap_or(-1);
        let end = end.unwrap_or(-1);
        if end >= 0 && start < 0 {
            return Err(Error("an end time needs a start time".into()));
        }
        if end >= 0 && end <= start {
            return Err(Error("the end time must be after the start time".into()));
        }
        Ok(Task { title: title.to_string(), start, end, daily, done_day: 0, start_shown: 0, end_shown: 0 })
    }

    pub fn done(&self, today: i32) -> bool {
        if self.daily { self.done_day == today } else { self.done_day != 0 }
    }

    /// "18:00-18:30", "18:00" or "".
    pub fn time_text(&self) -> String {
        match (self.start, self.end) {
            (s, e) if s >= 0 && e >= 0 => format!("{}-{}", clock::format_time(s), clock::format_time(e)),
            (s, _) if s >= 0 => clock::format_time(s),
            _ => String::new(),
        }
    }
}

#[derive(Debug)]
pub struct TaskList {
    pub tasks: Vec<Task>,
    path: PathBuf,
}

impl TaskList {
    /// Reads the list (an empty one when there's no file yet) and drops one-off tasks done before today.
    pub fn load(path: &Path, today: i32) -> Result<TaskList> {
        let mut tasks = Vec::new();
        if let Ok(bytes) = std::fs::read(path) {
            let text = crate::settings::decode(&bytes);
            for line in text.lines() {
                if let Some(t) = parse_line(line) {
                    tasks.push(t);
                }
            }
        }
        let mut list = TaskList { tasks, path: path.to_path_buf() };
        let before = list.tasks.len();
        list.tasks.retain(|t| t.daily || t.done_day == 0 || t.done_day >= today);
        if list.tasks.len() != before {
            list.save()?;
        }
        Ok(list)
    }

    pub fn save(&self) -> Result<()> {
        if let Some(dir) = self.path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let mut text = String::from("\u{FEFF}");
        for t in &self.tasks {
            text.push_str(&format!("{}\t{}\t{}\t{}\t{}\t{}\t{}\n", t.done_day, t.daily as i32, t.start_shown, t.start, t.end, t.end_shown, t.title));
        }
        // Write a new file, then swap it in, so a reader never sees half a list.
        let tmp = self.path.with_extension("txt.tmp");
        std::fs::write(&tmp, text)?;
        std::fs::rename(&tmp, &self.path)?;
        Ok(())
    }

    pub fn add(&mut self, task: Task) -> Result<&Task> {
        self.tasks.push(task);
        self.save()?;
        Ok(self.tasks.last().unwrap())
    }

    /// Ticks task `n` (1-based, as `list` shows them) done for today, or un-ticks it.
    pub fn set_done(&mut self, n: usize, done: bool, today: i32) -> Result<&Task> {
        let t = self.get_mut(n)?;
        t.done_day = if done { today } else { 0 };
        self.save()?;
        Ok(&self.tasks[n - 1])
    }

    pub fn remove(&mut self, n: usize) -> Result<Task> {
        self.get_mut(n)?;
        let t = self.tasks.remove(n - 1);
        self.save()?;
        Ok(t)
    }

    fn get_mut(&mut self, n: usize) -> Result<&mut Task> {
        let count = self.tasks.len();
        if n == 0 || n > count {
            return Err(Error(if count == 0 { "there are no tasks".into() } else { format!("there is no task {n}; there are {count}") }));
        }
        Ok(&mut self.tasks[n - 1])
    }
}

fn parse_line(line: &str) -> Option<Task> {
    let mut parts = line.trim_end_matches(['\r', '\n']).splitn(7, '\t');
    let mut num = || parts.next()?.trim().parse::<i32>().ok();
    let (done_day, daily, start_shown, start, end, end_shown) = (num()?, num()?, num()?, num()?, num()?, num()?);
    let title = parts.next()?.to_string();
    (!title.is_empty()).then(|| Task { title, start, end, daily: daily != 0, done_day, start_shown, end_shown })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("perch-core-{name}-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        dir.join("tasks.txt")
    }

    #[test]
    fn same_format_as_the_app() {
        let f = temp("format");
        // as the Windows app writes it: BOM, CRLF
        std::fs::write(&f, "\u{FEFF}0\t1\t0\t-1\t-1\t0\tWater plants\r\n20260101\t0\t0\t600\t630\t0\tOld errand\r\n0\t0\t0\t600\t630\t0\tCall mum\r\n").unwrap();
        let mut l = TaskList::load(&f, 20261120).unwrap();
        assert_eq!(l.tasks.len(), 2, "the one-off task done on an earlier day is dropped");
        assert_eq!(l.tasks[1].time_text(), "10:00-10:30");
        l.add(Task::new("Buy milk", Some(1080), None, false).unwrap()).unwrap();
        l.set_done(1, true, 20261120).unwrap();
        let text = std::fs::read_to_string(&f).unwrap();
        assert!(text.starts_with('\u{FEFF}'));
        assert!(text.contains("20261120\t1\t0\t-1\t-1\t0\tWater plants\n"));
        assert!(text.contains("0\t0\t0\t1080\t-1\t0\tBuy milk\n"));
        let l = TaskList::load(&f, 20261121).unwrap();
        assert!(!l.tasks[0].done(20261121), "a daily task is undone the next day");
        std::fs::remove_dir_all(f.parent().unwrap()).ok();
    }

    #[test]
    fn checks_input() {
        assert!(Task::new("  ", None, None, false).is_err());
        assert!(Task::new("x", Some(600), Some(600), false).is_err());
        assert!(Task::new("x", None, Some(600), false).is_err());
        assert_eq!(Task::new("a\tb", None, None, false).unwrap().title, "ab");
        let f = temp("bounds");
        let mut l = TaskList::load(&f, 20261120).unwrap();
        assert!(l.remove(1).is_err());
        std::fs::remove_dir_all(f.parent().unwrap()).ok();
    }
}
