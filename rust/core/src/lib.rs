//! Perch's core: the data and logic every platform shares, in the same files the Windows app uses
//! (`settings.ini`, `tasks.txt`, `water.txt`, `calendar.ics`, `ask.key` in Perch's data folder), so the command
//! line, the MCP server and the notch all see the same things.

pub mod app;
pub mod ask;
pub mod calendar;
pub mod clock;
pub mod lyrics;
pub mod net;
pub mod paths;
pub mod reminders;
pub mod secrets;
pub mod settings;
pub mod tasks;
pub mod update;
pub mod water;

pub use tasks::{Task, TaskList};
pub use water::WaterDay;

/// What went wrong, in words a person can act on.
#[derive(Debug)]
pub struct Error(pub String);

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for Error {}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Error(format!("couldn't read or write Perch's files: {e}"))
    }
}

pub type Result<T> = std::result::Result<T, Error>;
