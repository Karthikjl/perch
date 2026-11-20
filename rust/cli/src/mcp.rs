//! `perchctl mcp`: an MCP (Model Context Protocol) server over stdin / stdout, one JSON-RPC message per line.
//! AI tools (Claude Desktop, Claude Code, Cursor...) start it and can then manage Perch's to-do list and
//! water through the same actions as the command line.

use crate::actions;
use serde_json::{json, Value};
use std::io::{BufRead, Write};

const PROTOCOLS: [&str; 3] = ["2025-06-18", "2025-03-26", "2024-11-05"];

fn tools() -> Value {
    json!([
        {
            "name": "add_task",
            "description": "Add a task to today's to-do list in Perch. A task with a start time gets a reminder in the notch then.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "title": { "type": "string", "description": "Short title, at most 60 characters" },
                    "start": { "type": "string", "description": "Start time today, HH:MM (24 hours)" },
                    "end": { "type": "string", "description": "End time today, HH:MM; needs a start time" },
                    "daily": { "type": "boolean", "description": "Repeat every day" }
                },
                "required": ["title"]
            }
        },
        {
            "name": "list_tasks",
            "description": "Today's to-do list in Perch, numbered, with times and whether each is done.",
            "inputSchema": { "type": "object", "properties": {} }
        },
        {
            "name": "complete_task",
            "description": "Mark a task done for today (or not done) by its number from list_tasks.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "number": { "type": "integer", "minimum": 1 },
                    "done": { "type": "boolean", "description": "false marks it not done; default true" }
                },
                "required": ["number"]
            }
        },
        {
            "name": "remove_task",
            "description": "Remove a task by its number from list_tasks.",
            "inputSchema": { "type": "object", "properties": { "number": { "type": "integer", "minimum": 1 } }, "required": ["number"] }
        },
        {
            "name": "add_water",
            "description": "Log water drunk today. Without an amount, adds one cup (the user's cup size).",
            "inputSchema": { "type": "object", "properties": { "ml": { "type": "integer", "description": "Millilitres; negative takes some back" } } }
        },
        {
            "name": "water_status",
            "description": "How much water the user has had today, and their daily goal.",
            "inputSchema": { "type": "object", "properties": {} }
        }
    ])
}

fn call(name: &str, args: &Value) -> Result<actions::Outcome, String> {
    let s = |k: &str| args.get(k).and_then(Value::as_str);
    let n = |k: &str| args.get(k).and_then(Value::as_i64);
    let r = match name {
        "add_task" => actions::add_task(s("title").unwrap_or(""), s("start"), s("end"), args.get("daily").and_then(Value::as_bool).unwrap_or(false)),
        "list_tasks" => actions::list_tasks(),
        "complete_task" => actions::set_done(n("number").unwrap_or(0).max(0) as usize, args.get("done").and_then(Value::as_bool).unwrap_or(true)),
        "remove_task" => actions::remove_task(n("number").unwrap_or(0).max(0) as usize),
        "add_water" => actions::add_water(n("ml").map(|v| v as i32)),
        "water_status" => actions::water_status(),
        _ => return Err(format!("unknown tool {name}")),
    };
    r.map_err(|e| e.to_string())
}

fn handle(msg: &Value) -> Option<Value> {
    let id = msg.get("id").cloned();
    let method = msg.get("method").and_then(Value::as_str).unwrap_or("");
    let params = msg.get("params").cloned().unwrap_or(Value::Null);
    id.as_ref()?; // notifications (no id) get no reply
    let result = match method {
        "initialize" => {
            let asked = params.get("protocolVersion").and_then(Value::as_str).unwrap_or("");
            let version = if PROTOCOLS.contains(&asked) { asked } else { PROTOCOLS[0] };
            json!({
                "protocolVersion": version,
                "capabilities": { "tools": { "listChanged": false } },
                "serverInfo": { "name": "perch", "title": "Perch", "version": env!("CARGO_PKG_VERSION") },
                "instructions": "Perch is a notch app. These tools manage the user's to-do list for today and the water they drink."
            })
        }
        "ping" => json!({}),
        "tools/list" => json!({ "tools": tools() }),
        "tools/call" => {
            let name = params.get("name").and_then(Value::as_str).unwrap_or("");
            let args = params.get("arguments").cloned().unwrap_or(json!({}));
            match call(name, &args) {
                Ok(o) => json!({ "content": [{ "type": "text", "text": o.text }], "structuredContent": o.json, "isError": false }),
                Err(e) => json!({ "content": [{ "type": "text", "text": e }], "isError": true }),
            }
        }
        _ => return Some(json!({ "jsonrpc": "2.0", "id": id, "error": { "code": -32601, "message": format!("method not found: {method}") } })),
    };
    Some(json!({ "jsonrpc": "2.0", "id": id, "result": result }))
}

pub fn serve() {
    let stdin = std::io::stdin();
    let mut out = std::io::stdout().lock();
    for line in stdin.lock().lines() {
        let Ok(line) = line else { break };
        if line.trim().is_empty() {
            continue;
        }
        let reply = match serde_json::from_str::<Value>(&line) {
            Ok(msg) => handle(&msg),
            Err(e) => Some(json!({ "jsonrpc": "2.0", "id": null, "error": { "code": -32700, "message": format!("parse error: {e}") } })),
        };
        if let Some(r) = reply {
            if writeln!(out, "{r}").and_then(|_| out.flush()).is_err() {
                break;
            }
        }
    }
}
