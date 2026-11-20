//! One blocking web request, the way every online feature needs it (calendar, lyrics, Ask Perch, updates).

use std::io::Read;
use std::sync::Arc;
use std::time::Duration;

pub struct Response {
    /// The HTTP status; 0 when the server couldn't be reached at all.
    pub status: u16,
    pub body: Vec<u8>,
}

impl Response {
    pub fn text(&self) -> String {
        String::from_utf8_lossy(&self.body).into_owned()
    }
}

/// Sends a request and reads the whole reply (at most `limit` bytes). Never fails: a connection problem is
/// status 0, an HTTP error keeps its status and body (Gemini and GitHub explain errors there).
pub fn request(method: &str, url: &str, headers: &[(&str, &str)], body: Option<&[u8]>, limit: usize) -> Response {
    let tls = match native_tls::TlsConnector::new() {
        Ok(t) => Arc::new(t),
        Err(_) => return Response { status: 0, body: Vec::new() },
    };
    let agent = ureq::AgentBuilder::new()
        .tls_connector(tls)
        .timeout_connect(Duration::from_secs(10))
        .timeout_read(Duration::from_secs(60))
        .user_agent(concat!("Perch/", env!("CARGO_PKG_VERSION")))
        .build();
    let mut req = agent.request(method, url);
    for (k, v) in headers {
        req = req.set(k, v);
    }
    let result = match body {
        Some(b) => req.send_bytes(b),
        None => req.call(),
    };
    let resp = match result {
        Ok(r) => r,
        Err(ureq::Error::Status(_, r)) => r,
        Err(ureq::Error::Transport(_)) => return Response { status: 0, body: Vec::new() },
    };
    let status = resp.status();
    let mut body = Vec::new();
    let _ = resp.into_reader().take(limit as u64).read_to_end(&mut body);
    Response { status, body }
}

/// Percent-encodes a query value.
pub fn url_encode(s: &str) -> String {
    let mut o = String::new();
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) {
            o.push(b as char);
        } else {
            o.push_str(&format!("%{b:02X}"));
        }
    }
    o
}

/// For tests: a tiny web server on 127.0.0.1 that answers each request with `reply(request line, body)`.
#[cfg(test)]
pub mod testserver {
    use std::io::{BufRead, BufReader, Read, Write};
    use std::net::TcpListener;

    pub fn start(reply: impl Fn(&str, &str) -> (u16, String) + Send + 'static) -> String {
        let l = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = l.local_addr().unwrap().port();
        std::thread::spawn(move || {
            for s in l.incoming() {
                let Ok(mut s) = s else { break };
                let mut r = BufReader::new(s.try_clone().unwrap());
                let mut line = String::new();
                r.read_line(&mut line).unwrap();
                let mut len = 0;
                loop {
                    let mut h = String::new();
                    if r.read_line(&mut h).unwrap() == 0 || h.trim().is_empty() {
                        break;
                    }
                    if let Some(v) = h.to_ascii_lowercase().strip_prefix("content-length:") {
                        len = v.trim().parse().unwrap_or(0);
                    }
                }
                let mut body = vec![0; len];
                r.read_exact(&mut body).unwrap();
                let (code, out) = reply(line.trim(), &String::from_utf8_lossy(&body));
                let _ = write!(s, "HTTP/1.1 {code} X\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{out}", out.len());
            }
        });
        format!("http://127.0.0.1:{port}")
    }
}
