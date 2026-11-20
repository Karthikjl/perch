//! The running Perch app, if there is one: after the CLI or the MCP server changes a file, the app reads it
//! again so the notch shows the change at once. On Windows that's a message to its window; elsewhere there is
//! no app yet, and the files are enough.

/// Which file changed.
#[derive(Debug, Clone, Copy)]
pub enum Changed {
    Tasks = 1,
    Water = 2,
}

/// Tells a running Perch to reload. Returns true when one was running and got the message.
#[cfg(windows)]
pub fn reload(what: Changed) -> bool {
    use windows_sys::Win32::UI::WindowsAndMessaging::{FindWindowW, SendMessageTimeoutW, SMTO_ABORTIFHUNG, WM_APP};
    const WM_APP_RELOAD: u32 = WM_APP + 41; // notch.cpp: wp 1 = tasks.txt, 2 = water.txt
    let class: Vec<u16> = "PerchWindow\0".encode_utf16().collect();
    unsafe {
        let w = FindWindowW(class.as_ptr(), std::ptr::null());
        if w.is_null() {
            return false;
        }
        let mut result = 0usize;
        SendMessageTimeoutW(w, WM_APP_RELOAD, what as usize, 0, SMTO_ABORTIFHUNG, 2000, &mut result) != 0
    }
}

#[cfg(not(windows))]
pub fn reload(_what: Changed) -> bool {
    false
}
