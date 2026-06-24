mod app;
mod ui;
mod vfs;

use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::{anyhow, Context, Result};
use clap::{Parser, Subcommand, ValueEnum};
use crossterm::event::{
    self, DisableMouseCapture, EnableMouseCapture, Event, KeyCode, KeyEventKind, KeyModifiers,
    MouseButton, MouseEvent, MouseEventKind,
};
use crossterm::execute;
use crossterm::terminal::{
    disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen,
};
use ratatui::backend::CrosstermBackend;
use ratatui::Terminal;

use crate::app::{Action, App, Mode, ZoneKind};
use crate::vfs::{Vfs, VfsFormat, VfsOpenOptions};

#[derive(Parser, Debug)]
#[command(
    name = "rose-vfs",
    about = "Browse and extract ROSE Online VFS (.idx / .vfs) archives"
)]
struct Cli {
    /// Archive format to read. Auto uses TitanVFS when a sibling data.trf exists.
    #[arg(long, value_enum, default_value_t = CliVfsFormat::Auto, global = true)]
    format: CliVfsFormat,

    /// Optional newline-separated VFS path list for resolving Titan hash entries.
    #[arg(long, global = true)]
    path_list: Option<PathBuf>,

    /// Show unresolved Titan hash entries as @HASH/XXXXXXXX.bin.
    #[arg(long, global = true)]
    show_hashes: bool,

    /// Path to data.idx (defaults to ./data.idx)
    #[arg(default_value = "data.idx")]
    idx: PathBuf,

    #[command(subcommand)]
    command: Option<Command>,
}

#[derive(Clone, Copy, Debug, ValueEnum)]
enum CliVfsFormat {
    Auto,
    Rose,
    Titan,
}

impl From<CliVfsFormat> for VfsFormat {
    fn from(value: CliVfsFormat) -> Self {
        match value {
            CliVfsFormat::Auto => VfsFormat::Auto,
            CliVfsFormat::Rose => VfsFormat::Rose,
            CliVfsFormat::Titan => VfsFormat::Titan,
        }
    }
}

#[derive(Subcommand, Debug)]
enum Command {
    /// List all files in the VFS
    List {
        /// Filter substring (case-insensitive)
        #[arg(short, long)]
        filter: Option<String>,
    },
    /// Extract a single file
    Extract {
        /// VFS path of the file
        vfs_path: String,
        /// Output file path
        output: PathBuf,
    },
    /// Extract all files under a folder prefix
    ExtractDir {
        /// VFS folder prefix
        prefix: String,
        /// Output directory
        output: PathBuf,
    },
    /// Extract all files in the VFS
    ExtractAll {
        /// Output directory
        output: PathBuf,
    },
    /// Add or replace a file in the VFS
    Add {
        /// Source file on disk
        source: PathBuf,
        /// VFS path to store it under
        vfs_path: String,
    },
    /// Update an existing VFS file in place (errors if it doesn't exist)
    Update {
        /// VFS path of the file to replace
        vfs_path: String,
        /// New source file on disk
        source: PathBuf,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();

    if !cli.idx.exists() {
        return Err(anyhow!("idx file not found: {}", cli.idx.display()));
    }

    let options = VfsOpenOptions {
        format: cli.format.into(),
        path_list: cli.path_list,
        show_hashes: cli.show_hashes,
    };

    match cli.command {
        None => run_tui(&cli.idx, options),
        Some(Command::List { filter }) => cmd_list(&cli.idx, options, filter.as_deref()),
        Some(Command::Extract { vfs_path, output }) => {
            cmd_extract(&cli.idx, options, &vfs_path, &output)
        }
        Some(Command::ExtractDir { prefix, output }) => {
            cmd_extract_dir(&cli.idx, options, &prefix, &output)
        }
        Some(Command::ExtractAll { output }) => cmd_extract_dir(&cli.idx, options, "", &output),
        Some(Command::Add { source, vfs_path }) => cmd_add(&cli.idx, options, &source, &vfs_path),
        Some(Command::Update { vfs_path, source }) => {
            cmd_update(&cli.idx, options, &vfs_path, &source)
        }
    }
}

// ---------- CLI subcommands ----------

fn cmd_list(idx: &Path, options: VfsOpenOptions, filter: Option<&str>) -> Result<()> {
    let vfs = Vfs::open_with_options(idx, options)?;
    let needle = filter.map(|s| s.to_ascii_uppercase());
    let mut count = 0usize;
    let stdout = io::stdout();
    let mut stdout = stdout.lock();
    for entry in vfs.entries() {
        if entry.is_deleted {
            continue;
        }
        if let Some(n) = &needle {
            if !entry.path.to_ascii_uppercase().contains(n) {
                continue;
            }
        }
        if let Err(e) = writeln!(stdout, "{:>10}  {}", entry.size, entry.path) {
            if e.kind() == io::ErrorKind::BrokenPipe {
                return Ok(());
            }
            return Err(e.into());
        }
        count += 1;
    }
    eprintln!("{} file(s)", count);
    Ok(())
}

fn cmd_extract(idx: &Path, options: VfsOpenOptions, vfs_path: &str, output: &Path) -> Result<()> {
    let vfs = Vfs::open_with_options(idx, options)?;
    let entry = vfs
        .find(vfs_path)
        .ok_or_else(|| anyhow!("file not found in vfs: {}", vfs_path))?;
    vfs.extract_entry(&entry, output)?;
    println!(
        "Extracted {} ({} bytes) -> {}",
        entry.path,
        entry.size,
        output.display()
    );
    Ok(())
}

fn cmd_extract_dir(idx: &Path, options: VfsOpenOptions, prefix: &str, output: &Path) -> Result<()> {
    let vfs = Vfs::open_with_options(idx, options)?;
    let count = vfs.extract_tree(prefix, output, |path, i, total| {
        if i % 50 == 0 || i == total {
            eprintln!("[{}/{}] {}", i, total, path);
        }
    })?;
    println!("Extracted {} file(s) to {}", count, output.display());
    Ok(())
}

fn cmd_add(idx: &Path, options: VfsOpenOptions, source: &Path, vfs_path: &str) -> Result<()> {
    if !source.exists() {
        return Err(anyhow!("source not found: {}", source.display()));
    }
    let mut vfs = Vfs::open_with_options(idx, options)?;
    vfs.add_file(source, vfs_path)?;
    println!("Added '{}' -> '{}'", source.display(), vfs_path);
    Ok(())
}

fn cmd_update(idx: &Path, options: VfsOpenOptions, vfs_path: &str, source: &Path) -> Result<()> {
    if !source.exists() {
        return Err(anyhow!("source not found: {}", source.display()));
    }
    let mut vfs = Vfs::open_with_options(idx, options)?;
    if vfs.find(vfs_path).is_none() {
        return Err(anyhow!("'{}' not found in vfs", vfs_path));
    }
    vfs.add_file(source, vfs_path)?;
    println!("Updated '{}' from '{}'", vfs_path, source.display());
    Ok(())
}

// ---------- TUI ----------

fn run_tui(idx: &Path, options: VfsOpenOptions) -> Result<()> {
    let vfs = Vfs::open_with_options(idx, options)?;
    let mut app = App::new(vfs);

    enable_raw_mode().context("enable raw mode")?;
    let mut stdout = io::stdout();
    execute!(stdout, EnterAlternateScreen, EnableMouseCapture)?;
    let backend = CrosstermBackend::new(stdout);
    let mut terminal = Terminal::new(backend)?;

    let result = event_loop(&mut terminal, &mut app);

    disable_raw_mode().ok();
    execute!(
        terminal.backend_mut(),
        LeaveAlternateScreen,
        DisableMouseCapture
    )
    .ok();
    terminal.show_cursor().ok();

    result
}

fn event_loop<B: ratatui::backend::Backend>(
    terminal: &mut Terminal<B>,
    app: &mut App,
) -> Result<()> {
    loop {
        terminal.draw(|f| ui::draw(f, app))?;

        if !event::poll(Duration::from_millis(250))? {
            continue;
        }

        match event::read()? {
            Event::Key(key) => {
                if key.kind != KeyEventKind::Press {
                    continue;
                }
                match app.mode {
                    Mode::Browse => handle_browse_key(app, key.code, key.modifiers),
                    Mode::Filter => handle_text_input(app, key.code, |app| {
                        app.filter = app.input.clone();
                        app.input.clear();
                        app.rebuild_visible();
                        app.mode = Mode::Browse;
                        app.set_status(format!("Filter: '{}'", app.filter));
                    }),
                    Mode::Message => {
                        app.mode = Mode::Browse;
                    }
                }
            }
            Event::Mouse(me) => handle_mouse(app, me),
            _ => {}
        }

        if app.should_quit {
            break;
        }
    }
    Ok(())
}

fn handle_mouse(app: &mut App, me: MouseEvent) {
    // Only react to mouse in Browse mode — popups are keyboard-driven.
    if app.mode != Mode::Browse {
        return;
    }

    match me.kind {
        MouseEventKind::Down(MouseButton::Left) => {
            let hit = app.zone_at(me.column, me.row).map(|z| z.kind.clone());
            if let Some(kind) = hit {
                match kind {
                    ZoneKind::TreeRow(idx) => {
                        let was_selected = app.selected == idx;
                        app.selected = idx;
                        if was_selected {
                            if let Some(row) = app.visible.get(idx) {
                                if row.is_dir {
                                    app.toggle_expand();
                                }
                            }
                        }
                    }
                    ZoneKind::Button(action) => dispatch_action(app, action),
                }
            }
        }
        MouseEventKind::ScrollUp => {
            if let Some(zone) = app.zone_at(me.column, me.row) {
                if matches!(zone.kind, ZoneKind::TreeRow(_)) {
                    for _ in 0..3 {
                        app.move_up();
                    }
                    return;
                }
            }
            for _ in 0..3 {
                app.move_up();
            }
        }
        MouseEventKind::ScrollDown => {
            if let Some(zone) = app.zone_at(me.column, me.row) {
                if matches!(zone.kind, ZoneKind::TreeRow(_)) {
                    for _ in 0..3 {
                        app.move_down();
                    }
                    return;
                }
            }
            for _ in 0..3 {
                app.move_down();
            }
        }
        _ => {}
    }
}

fn dispatch_action(app: &mut App, action: Action) {
    match action {
        Action::ExtractFile => begin_extract_file(app),
        Action::ExtractDir => begin_extract_dir(app),
        Action::ExtractAll => begin_extract_all(app),
        Action::Add => begin_add(app),
        Action::Update => begin_update(app),
        Action::Filter => {
            app.input = app.filter.clone();
            app.mode = Mode::Filter;
        }
        Action::Quit => app.should_quit = true,
    }
}

fn handle_browse_key(app: &mut App, code: KeyCode, mods: KeyModifiers) {
    match code {
        KeyCode::Char('q') | KeyCode::Char('Q') | KeyCode::Esc => app.should_quit = true,
        KeyCode::Char('c') if mods.contains(KeyModifiers::CONTROL) => app.should_quit = true,
        KeyCode::Up | KeyCode::Char('k') => app.move_up(),
        KeyCode::Down | KeyCode::Char('j') => app.move_down(),
        KeyCode::PageUp => app.page_up(10),
        KeyCode::PageDown => app.page_down(10),
        KeyCode::Home => app.go_home(),
        KeyCode::End => app.go_end(),
        KeyCode::Enter | KeyCode::Right | KeyCode::Char('l') => app.toggle_expand(),
        KeyCode::Left | KeyCode::Char('h') => app.collapse_or_parent(),
        KeyCode::Char('/') => {
            app.input = app.filter.clone();
            app.mode = Mode::Filter;
        }
        KeyCode::Char('e') | KeyCode::Char('E') => begin_extract_file(app),
        KeyCode::Char('d') | KeyCode::Char('D') => begin_extract_dir(app),
        KeyCode::Char('x') | KeyCode::Char('X') => begin_extract_all(app),
        KeyCode::Char('a') | KeyCode::Char('A') => begin_add(app),
        KeyCode::Char('u') | KeyCode::Char('U') => begin_update(app),
        KeyCode::Char('?') => {
            app.set_status(
                "Keys: ↑↓ nav · Enter expand · E file · D folder · X all · A add · / filter · Q quit",
            );
        }
        _ => {}
    }
}

fn handle_text_input(app: &mut App, code: KeyCode, on_enter: impl FnOnce(&mut App)) {
    match code {
        KeyCode::Esc => {
            app.input.clear();
            app.mode = Mode::Browse;
        }
        KeyCode::Enter => on_enter(app),
        KeyCode::Backspace => {
            app.input.pop();
        }
        KeyCode::Char(c) => {
            app.input.push(c);
        }
        _ => {}
    }
}

fn begin_extract_file(app: &mut App) {
    let entry = match app.selected_entry().cloned() {
        Some(e) => e,
        None => {
            app.set_status("Select a file first");
            return;
        }
    };
    let default_name = Path::new(&entry.path)
        .file_name()
        .map(|f| f.to_string_lossy().to_string())
        .unwrap_or_else(|| entry.path.clone());

    let dest = rfd::FileDialog::new()
        .set_title(&format!("Extract '{}' as...", entry.path))
        .set_file_name(&default_name)
        .save_file();

    let Some(out) = dest else {
        app.set_status("Extract cancelled");
        return;
    };

    match app.vfs.extract_entry(&entry, &out) {
        Ok(_) => app.set_status(format!(
            "Extracted {} ({} bytes) -> {}",
            entry.path,
            entry.size,
            out.display()
        )),
        Err(e) => {
            app.set_status(format!("Error: {}", e));
            app.mode = Mode::Message;
        }
    }
}

fn begin_extract_dir(app: &mut App) {
    let row = match app.selected_row().cloned() {
        Some(r) if r.is_dir => r,
        _ => {
            app.set_status("Select a folder first");
            return;
        }
    };

    let dest = rfd::FileDialog::new()
        .set_title(&format!("Extract folder '{}' to...", row.full_path))
        .pick_folder();

    let Some(out_dir) = dest else {
        app.set_status("Extract cancelled");
        return;
    };

    match app.vfs.extract_tree(&row.full_path, &out_dir, |_, _, _| {}) {
        Ok(n) => app.set_status(format!(
            "Extracted {} file(s) from {} -> {}",
            n,
            row.full_path,
            out_dir.display()
        )),
        Err(e) => {
            app.set_status(format!("Error: {}", e));
            app.mode = Mode::Message;
        }
    }
}

fn begin_extract_all(app: &mut App) {
    let dest = rfd::FileDialog::new()
        .set_title("Extract entire VFS to folder...")
        .pick_folder();

    let Some(out_dir) = dest else {
        app.set_status("Extract cancelled");
        return;
    };

    match app.vfs.extract_tree("", &out_dir, |_, _, _| {}) {
        Ok(n) => app.set_status(format!("Extracted {} file(s) -> {}", n, out_dir.display())),
        Err(e) => {
            app.set_status(format!("Error: {}", e));
            app.mode = Mode::Message;
        }
    }
}

fn begin_add(app: &mut App) {
    if !app.vfs.is_writable() {
        app.set_status("TitanVFS support is read-only");
        app.mode = Mode::Message;
        return;
    }

    let row = match app.selected_row().cloned() {
        Some(r) => r,
        None => {
            app.set_status("Select a target location first");
            return;
        }
    };

    let source = match rfd::FileDialog::new()
        .set_title("Pick file to add to VFS")
        .pick_file()
    {
        Some(p) => p,
        None => {
            app.set_status("Add cancelled");
            return;
        }
    };

    let filename = source
        .file_name()
        .map(|f| f.to_string_lossy().to_uppercase())
        .unwrap_or_default();

    let target = if row.is_dir {
        format!("{}/{}", row.full_path, filename)
    } else {
        match Path::new(&row.full_path).parent() {
            Some(p) if !p.as_os_str().is_empty() => {
                format!("{}/{}", p.to_string_lossy().replace('\\', "/"), filename)
            }
            _ => filename,
        }
    };

    apply_add_or_update(app, &source, &target, false);
}

fn begin_update(app: &mut App) {
    if !app.vfs.is_writable() {
        app.set_status("TitanVFS support is read-only");
        app.mode = Mode::Message;
        return;
    }

    let entry = match app.selected_entry().cloned() {
        Some(e) => e,
        None => {
            app.set_status("Select an existing file to update");
            return;
        }
    };

    let source = match rfd::FileDialog::new()
        .set_title(&format!("Pick new file to replace '{}'", entry.path))
        .pick_file()
    {
        Some(p) => p,
        None => {
            app.set_status("Update cancelled");
            return;
        }
    };

    apply_add_or_update(app, &source, &entry.path, true);
}

fn apply_add_or_update(app: &mut App, source: &Path, vfs_path: &str, is_update: bool) {
    if !source.exists() {
        app.set_status(format!("Error: source not found: {}", source.display()));
        app.mode = Mode::Message;
        return;
    }
    if is_update && app.vfs.find(vfs_path).is_none() {
        app.set_status(format!("Error: '{}' not found in vfs", vfs_path));
        app.mode = Mode::Message;
        return;
    }
    match app.vfs.add_file(source, vfs_path) {
        Ok(_) => {
            app.entries = app.vfs.entries();
            app.tree = crate::vfs::TreeNode::build(&app.entries);
            app.rebuild_visible();
            let verb = if is_update { "Updated" } else { "Added" };
            app.set_status(format!("{} {} -> {}", verb, source.display(), vfs_path));
        }
        Err(e) => {
            app.set_status(format!("Error: {}", e));
            app.mode = Mode::Message;
        }
    }
}
