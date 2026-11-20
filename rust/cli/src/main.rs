//! perchctl: Perch's to-do list and water from a terminal or a script, and `perchctl mcp` for AI tools.

mod actions;
mod mcp;

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "perchctl", version, about = "Perch from the command line, and an MCP server for AI tools")]
struct Cli {
    /// Print the result as JSON (for scripts)
    #[arg(long, global = true)]
    json: bool,
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Today's to-do list
    #[command(subcommand)]
    Task(TaskCommand),
    /// Water drunk today
    #[command(subcommand)]
    Water(WaterCommand),
    /// Events from your calendar link: today (default), tomorrow or the week
    Calendar {
        #[arg(value_enum, default_value_t = Span::Today)]
        when: Span,
    },
    /// Ask Perch (Gemini, with the key saved in Perch): perchctl ask "What's 15% of 80?"
    Ask { question: Vec<String> },
    /// Check GitHub for a newer Perch
    Update,
    /// Run as an MCP server on stdin / stdout (for Claude, Cursor and other AI tools)
    Mcp,
}

#[derive(Clone, Copy, clap::ValueEnum)]
enum Span {
    Today,
    Tomorrow,
    Week,
}

#[derive(Subcommand)]
enum TaskCommand {
    /// Add a task: perchctl task add "Call Mum" --at 18:00 --until 18:30
    Add {
        title: String,
        /// Start time today (18:30); you get a reminder then
        #[arg(long)]
        at: Option<String>,
        /// End time today
        #[arg(long)]
        until: Option<String>,
        /// Repeat every day
        #[arg(long)]
        daily: bool,
    },
    /// Show today's tasks, numbered
    List,
    /// Mark task N done (use --undo to mark it not done)
    Done {
        number: usize,
        #[arg(long)]
        undo: bool,
    },
    /// Remove task N
    Remove { number: usize },
}

#[derive(Subcommand)]
enum WaterCommand {
    /// Add water: one cup, or the amount in ml (negative takes some back)
    Add {
        #[arg(allow_negative_numbers = true)]
        ml: Option<i32>,
    },
    /// Today's water and the goal
    Status,
}

fn main() {
    let cli = Cli::parse();
    let result = match cli.command {
        Command::Mcp => {
            mcp::serve();
            return;
        }
        Command::Task(TaskCommand::Add { title, at, until, daily }) => actions::add_task(&title, at.as_deref(), until.as_deref(), daily),
        Command::Task(TaskCommand::List) => actions::list_tasks(),
        Command::Task(TaskCommand::Done { number, undo }) => actions::set_done(number, !undo),
        Command::Task(TaskCommand::Remove { number }) => actions::remove_task(number),
        Command::Water(WaterCommand::Add { ml }) => actions::add_water(ml),
        Command::Water(WaterCommand::Status) => actions::water_status(),
        Command::Calendar { when } => match when {
            Span::Today => actions::calendar(0, 1),
            Span::Tomorrow => actions::calendar(1, 1),
            Span::Week => actions::calendar(0, 7),
        },
        Command::Ask { question } => actions::ask(&question.join(" ")),
        Command::Update => actions::update_check(),
    };
    match result {
        Ok(o) => {
            if cli.json {
                println!("{}", o.json);
            } else {
                println!("{}", o.text);
            }
        }
        Err(e) => {
            eprintln!("perchctl: {e}");
            std::process::exit(1);
        }
    }
}
