//! Updates from GitHub: the newest release of Karthikjl/perch, and which of its files is this system's.

use crate::net;
use serde_json::Value;

/// This build's version.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

#[derive(Debug, Clone, PartialEq)]
pub struct Release {
    /// "0.2.0" (without the v).
    pub version: String,
    /// Address of the file named `asset`, if the release has it.
    pub url: Option<String>,
}

/// The releases API, or PERCH_UPDATE_URL (tests).
pub fn api_url() -> String {
    std::env::var("PERCH_UPDATE_URL").ok().filter(|s| !s.is_empty()).unwrap_or_else(|| "https://api.github.com/repos/Karthikjl/perch/releases/latest".into())
}

/// true when version a is newer than b ("0.10.0" > "0.9.2"; a leading v is ignored).
pub fn newer(a: &str, b: &str) -> bool {
    let parts = |v: &str| -> Vec<u32> {
        v.trim_start_matches(['v', 'V']).split('.').map_while(|p| p.chars().take_while(char::is_ascii_digit).collect::<String>().parse().ok()).collect()
    };
    let (x, y) = (parts(a), parts(b));
    if x.is_empty() {
        return false;
    }
    for i in 0..x.len().max(y.len()) {
        let (p, q) = (x.get(i).copied().unwrap_or(0), y.get(i).copied().unwrap_or(0));
        if p != q {
            return p > q;
        }
    }
    false
}

/// Asks GitHub for the newest release. Err(status): 0 when GitHub couldn't be reached.
pub fn check(asset: &str) -> Result<Release, u16> {
    let r = net::request("GET", &api_url(), &[("Accept", "application/vnd.github+json")], None, 4 << 20);
    if r.status != 200 {
        return Err(r.status);
    }
    let v: Value = serde_json::from_slice(&r.body).map_err(|_| 204u16)?;
    let tag = v.get("tag_name").and_then(Value::as_str).ok_or(204u16)?;
    let url = v.get("assets").and_then(Value::as_array).into_iter().flatten().find_map(|a| {
        let name = a.get("name").and_then(Value::as_str)?;
        let url = a.get("browser_download_url").and_then(Value::as_str)?;
        (name.eq_ignore_ascii_case(asset) && (url.starts_with("https://") || url.starts_with("http://127.0.0.1:"))).then(|| url.to_string())
    });
    Ok(Release { version: tag.trim_start_matches(['v', 'V']).to_string(), url })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions() {
        assert!(newer("0.10.0", "0.9.2"));
        assert!(newer("v0.2.0", "0.1.9"));
        assert!(!newer("0.1.0", "0.1.0"));
        assert!(!newer("0.1", "0.1.0"));
        assert!(newer("1.0.1", "1.0"));
        assert!(!newer("beta", "0.1.0"));
    }

    #[test]
    fn checks_github() {
        let url = net::testserver::start(|_, _| {
            (200, r#"{"tag_name":"v9.9.9","assets":[{"name":"notes.txt","browser_download_url":"https://x/notes.txt"},{"name":"Perch.exe","browser_download_url":"https://x/Perch.exe"}]}"#.into())
        });
        std::env::set_var("PERCH_UPDATE_URL", format!("{url}/latest"));
        let r = check("Perch.exe").unwrap();
        assert_eq!(r, Release { version: "9.9.9".into(), url: Some("https://x/Perch.exe".into()) });
        assert_eq!(check("perch-linux").unwrap().url, None);
        std::env::set_var("PERCH_UPDATE_URL", "http://127.0.0.1:1/x");
        assert_eq!(check("Perch.exe"), Err(0));
        std::env::remove_var("PERCH_UPDATE_URL");
    }
}
