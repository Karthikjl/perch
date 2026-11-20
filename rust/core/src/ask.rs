//! Ask Perch: questions to Google's Gemini with the user's own key. One tool is offered, add_task; the model is
//! told it's the only action it can take, so it never claims to have done anything else.

use crate::net;
use serde_json::{json, Value};

#[derive(Debug, Clone, PartialEq)]
pub struct Turn {
    pub user: bool,
    pub text: String,
}

/// A task the model asked Perch to add. Times are minutes since midnight.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct TaskRequest {
    pub title: String,
    pub start: Option<i32>,
    pub end: Option<i32>,
    pub daily: bool,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct Answer {
    /// The reply as plain text (Markdown made plain); may be empty when the model only asked for a task.
    pub text: String,
    pub task: Option<TaskRequest>,
}

/// https://generativelanguage.googleapis.com/v1beta, or PERCH_GEMINI_URL (tests).
pub fn base_url() -> String {
    std::env::var("PERCH_GEMINI_URL").ok().filter(|s| !s.is_empty()).unwrap_or_else(|| "https://generativelanguage.googleapis.com/v1beta".into())
}

fn error_text(status: u16, body: &[u8]) -> String {
    let msg = serde_json::from_slice::<Value>(body).ok().and_then(|v| v.pointer("/error/message").and_then(Value::as_str).map(str::to_string));
    match status {
        0 => "Couldn't reach Google. Check your connection.".into(),
        400 if msg.as_deref().is_some_and(|m| m.contains("API key")) => "Google didn't accept the key. Check it in Settings > Ask Perch.".into(),
        401 | 403 => "Google didn't accept the key. Check it in Settings > Ask Perch.".into(),
        404 => "That model isn't available to your key. Pick another in Settings > Ask Perch.".into(),
        429 => "Too many questions for now. Try again in a minute.".into(),
        _ => msg.unwrap_or_else(|| "Google couldn't answer right now.".into()),
    }
}

/// Markdown to plain text: no ** or #, "* " and "- " bullets become "•".
pub fn plain(t: &str) -> String {
    let mut out = Vec::new();
    for line in t.replace("**", "").replace('`', "").lines() {
        let l = line.trim_start_matches('#');
        let l = if l.len() < line.len() { l.trim_start() } else { line };
        let l = match l.strip_prefix("* ").or_else(|| l.strip_prefix("- ")) {
            Some(rest) => format!("\u{2022} {rest}"),
            None => l.to_string(),
        };
        out.push(l);
    }
    out.join("\n").trim_end().to_string()
}

fn minutes(s: &str) -> Option<i32> {
    crate::clock::parse_time(s)
}

/// Sends the conversation (oldest first; the last turn is the question). Err: a message for the user.
pub fn generate(key: &str, model: &str, turns: &[Turn], now_minute: i32) -> Result<Answer, String> {
    let instruction = format!(
        "You are Perch, a helpful assistant in a small panel at the top of the screen. It is {} now. \
         Answer briefly and plainly in a few short paragraphs or a short list. No tables. \
         You can add a task to the user's Perch to-do list for today with the add_task tool (with an optional start and end \
         time, and daily if it should repeat every day); a task with a start time gets a reminder then. That is the only \
         action you can take. You cannot set alarms, calendar events or other reminders, send messages, open apps or change \
         anything else, so never say you did; say what the user can do instead.",
        crate::clock::format_time(now_minute)
    );
    let contents: Vec<Value> = turns.iter().map(|t| json!({ "role": if t.user { "user" } else { "model" }, "parts": [{ "text": t.text }] })).collect();
    let body = json!({
        "systemInstruction": { "parts": [{ "text": instruction }] },
        "tools": [{ "functionDeclarations": [{
            "name": "add_task",
            "description": "Adds a task to the user's Perch to-do list for today.",
            "parameters": { "type": "OBJECT", "properties": {
                "title": { "type": "STRING", "description": "Short task title, at most 60 characters" },
                "start": { "type": "STRING", "description": "Start time today, HH:MM in 24 hours (optional)" },
                "end": { "type": "STRING", "description": "End time today, HH:MM in 24 hours (optional)" },
                "daily": { "type": "BOOLEAN", "description": "Repeat every day (optional)" }
            }, "required": ["title"] }
        }]}],
        "contents": contents
    });
    let url = format!("{}/models/{model}:generateContent", base_url());
    let r = net::request("POST", &url, &[("Content-Type", "application/json"), ("x-goog-api-key", key)], Some(body.to_string().as_bytes()), 4 << 20);
    if r.status != 200 {
        return Err(error_text(r.status, &r.body));
    }
    let v: Value = serde_json::from_slice(&r.body).map_err(|_| "Google sent an answer Perch couldn't read.".to_string())?;
    let parts = v.pointer("/candidates/0/content/parts").and_then(Value::as_array).cloned().unwrap_or_default();
    let text: String = parts.iter().filter_map(|p| p.get("text").and_then(Value::as_str)).collect();
    let task = parts.iter().filter_map(|p| p.get("functionCall")).find(|c| c.get("name").and_then(Value::as_str) == Some("add_task")).and_then(|c| {
        let a = c.get("args")?;
        let title: String = a.get("title")?.as_str()?.trim().chars().take(60).collect();
        if title.is_empty() {
            return None;
        }
        let start = a.get("start").and_then(Value::as_str).and_then(minutes);
        let end = a.get("end").and_then(Value::as_str).and_then(minutes).filter(|e| start.is_some_and(|s| *e > s));
        Some(TaskRequest { title, start, end, daily: a.get("daily").and_then(Value::as_bool).unwrap_or(false) })
    });
    let answer = Answer { text: plain(&text), task };
    if answer.text.is_empty() && answer.task.is_none() {
        return Err("Google sent an empty answer. Try asking another way.".into());
    }
    Ok(answer)
}

/// The text models this key may use ("gemini-..."), newest-looking first.
pub fn list_models(key: &str) -> Result<Vec<String>, String> {
    let r = net::request("GET", &format!("{}/models?pageSize=200", base_url()), &[("x-goog-api-key", key)], None, 4 << 20);
    if r.status != 200 {
        return Err(error_text(r.status, &r.body));
    }
    let v: Value = serde_json::from_slice(&r.body).map_err(|_| "Google sent a list Perch couldn't read.".to_string())?;
    let mut out: Vec<String> = v.get("models").and_then(Value::as_array).into_iter().flatten().filter_map(|m| {
        let name = m.get("name")?.as_str()?.strip_prefix("models/")?;
        let methods = m.get("supportedGenerationMethods")?.as_array()?;
        let text_model = name.starts_with("gemini-") && methods.iter().any(|x| x == "generateContent")
            && !["embedding", "image", "tts", "audio"].iter().any(|w| name.contains(w));
        text_model.then(|| name.to_string())
    }).collect();
    out.sort_by(|a, b| b.cmp(a));
    out.dedup();
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plain_text() {
        assert_eq!(plain("**Paris** is the capital.\n* It sits on the Seine\n## Notes\n- `one`"), "Paris is the capital.\n\u{2022} It sits on the Seine\nNotes\n\u{2022} one");
    }

    #[test]
    fn asks_and_adds_tasks() {
        let url = net::testserver::start(|line, body| {
            let v: Value = serde_json::from_str(body).unwrap_or(Value::Null);
            let q = v.pointer("/contents").and_then(Value::as_array).and_then(|c| c.last()).and_then(|t| t.pointer("/parts/0/text")).and_then(Value::as_str).unwrap_or("").to_string();
            if line.starts_with("GET") {
                (200, r#"{"models":[{"name":"models/gemini-flash-latest","supportedGenerationMethods":["generateContent"]},{"name":"models/text-embedding-004","supportedGenerationMethods":["embedContent"]},{"name":"models/gemini-9-pro","supportedGenerationMethods":["generateContent"]}]}"#.into())
            } else if line.contains("/models/gone:") {
                (404, r#"{"error":{"message":"not found"}}"#.into())
            } else if q.contains("Remind me") {
                assert!(body.contains("\"add_task\"") && body.contains("only action you can take"));
                (200, r#"{"candidates":[{"content":{"parts":[{"functionCall":{"name":"add_task","args":{"title":"Call Mum","start":"18:00","end":"17:00"}}}]}}]}"#.into())
            } else {
                (200, r#"{"candidates":[{"content":{"parts":[{"text":"**Paris** is the capital."}]}}]}"#.into())
            }
        });
        std::env::set_var("PERCH_GEMINI_URL", format!("{url}/v1beta"));
        let t = |s: &str| vec![Turn { user: true, text: s.into() }];
        assert_eq!(generate("k", "gemini-flash-latest", &t("Capital of France?"), 600).unwrap().text, "Paris is the capital.");
        let a = generate("k", "gemini-flash-latest", &t("Remind me to call Mum at 18:00"), 600).unwrap();
        assert_eq!(a.task, Some(TaskRequest { title: "Call Mum".into(), start: Some(1080), end: None, daily: false }), "an end before the start is dropped");
        assert!(generate("k", "gone", &t("x"), 600).unwrap_err().contains("isn't available"));
        assert_eq!(list_models("k").unwrap(), ["gemini-flash-latest", "gemini-9-pro"]);
        std::env::remove_var("PERCH_GEMINI_URL");
    }
}
