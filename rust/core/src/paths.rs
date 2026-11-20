//! Where Perch keeps its files.

use std::path::PathBuf;

/// `%PERCH_DATA_DIR%` if set (tests), else the system's settings folder plus `Perch`:
/// `%APPDATA%\Perch` on Windows (the app's own folder), `~/Library/Application Support/Perch` on macOS,
/// `~/.config/Perch` on Linux.
pub fn data_dir() -> PathBuf {
    if let Some(d) = std::env::var_os("PERCH_DATA_DIR").filter(|d| !d.is_empty()) {
        return PathBuf::from(d);
    }
    dirs::config_dir().unwrap_or_else(|| PathBuf::from(".")).join("Perch")
}

pub fn settings_file() -> PathBuf {
    data_dir().join("settings.ini")
}

pub fn tasks_file() -> PathBuf {
    data_dir().join("tasks.txt")
}

pub fn water_file() -> PathBuf {
    data_dir().join("water.txt")
}
