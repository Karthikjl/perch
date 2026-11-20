//! When reminders are due, worked out from the lists (no timers here: the app sets one timer for the next time
//! these return).

use crate::calendar::Event;
use crate::tasks::Task;

/// The next minute (after `now`) a task reminder is due: a task's start ("Task Time!") or end ("Time's Up!"),
/// or None. `remind_start` / `remind_end` are the To-do settings.
pub fn task_next_due(tasks: &[Task], today: i32, now: i32, remind_start: bool, remind_end: bool) -> Option<i32> {
    let mut next: Option<i32> = None;
    let mut take = |t: i32| {
        if t > now && next.map_or(true, |n| t < n) {
            next = Some(t);
        }
    };
    for t in tasks.iter().filter(|t| !t.done(today) && t.start >= 0) {
        if remind_start && t.start_shown != today {
            take(t.start);
        }
        if remind_end && t.end >= 0 && t.end_shown != today {
            take(t.end);
        }
    }
    next
}

/// Water reminders split the goal into cups spread evenly over the active hours. The next minute (after `now`)
/// a reminder is due because you'd be behind, or None (reminders off, goal reached, or none left today).
pub fn water_next_due(ml: i32, goal: i32, cup: i32, from: i32, until: i32, now: i32) -> Option<i32> {
    if ml >= goal || cup <= 0 || until <= from {
        return None;
    }
    let n = ((goal + cup - 1) / cup).max(1);
    let span = until - from;
    (1..=n).map(|k| (from + span * k / n, (k * cup).min(goal))).find(|&(t, need)| t > now && ml < need).map(|(t, _)| t)
}

/// How much water you should have had by `now` (what a reminder compares against).
pub fn water_expected(goal: i32, cup: i32, from: i32, until: i32, now: i32) -> i32 {
    if cup <= 0 || until <= from {
        return 0;
    }
    let n = ((goal + cup - 1) / cup).max(1);
    (1..=n).filter(|k| from + (until - from) * k / n <= now).map(|k| (k * cup).min(goal)).max().unwrap_or(0)
}

/// The minute a calendar event's reminder is due, `before` minutes ahead (0 = at its start); None for all-day events.
pub fn event_due(e: &Event, before: i32) -> Option<i32> {
    (e.start >= 0).then(|| (e.start - before).max(0))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn water() {
        // 8 cups of 250 ml over 08:00-17:00: one every 67.5 min (09:07, 10:15, 11:22, 12:30, ...)
        assert_eq!(water_next_due(250, 2000, 250, 480, 1020, 600), Some(615));
        assert_eq!(water_next_due(500, 2000, 250, 480, 1020, 700), Some(750));
        assert_eq!(water_expected(2000, 250, 480, 1020, 700), 750);
        assert_eq!(water_next_due(2000, 2000, 250, 480, 1020, 600), None);
        assert_eq!(water_next_due(0, 2000, 250, 480, 1020, 1100), None);
    }

    #[test]
    fn tasks() {
        let mut a = Task::new("A", Some(600), Some(630), false).unwrap();
        let b = Task::new("B", None, None, false).unwrap();
        assert_eq!(task_next_due(&[a.clone(), b.clone()], 20261120, 500, true, true), Some(600));
        assert_eq!(task_next_due(&[a.clone()], 20261120, 610, true, true), Some(630));
        assert_eq!(task_next_due(&[a.clone()], 20261120, 610, true, false), None);
        a.done_day = 20261120;
        assert_eq!(task_next_due(&[a], 20261120, 500, true, true), None, "done tasks don't remind");
        let e = Event { day: 0, start: 600, end: 660, title: "x".into() };
        assert_eq!(event_due(&e, 10), Some(590));
        assert_eq!(event_due(&Event { start: -1, ..e }, 10), None);
    }
}
