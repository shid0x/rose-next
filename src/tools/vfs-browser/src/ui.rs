use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, List, ListItem, ListState, Paragraph, Wrap};
use ratatui::Frame;

use crate::app::{Action, App, Mode, Zone, ZoneKind};

pub fn draw(f: &mut Frame, app: &mut App) {
    let size = f.size();
    app.zones.clear();

    let root = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3), // title
            Constraint::Length(3), // button bar
            Constraint::Min(1),    // body
            Constraint::Length(3), // status / help
        ])
        .split(size);

    draw_title(f, root[0], app);
    draw_button_bar(f, root[1], app);
    draw_body(f, root[2], app);
    draw_status(f, root[3], app);

    match app.mode {
        Mode::Filter => draw_input_popup(
            f,
            size,
            "Filter",
            &app.input,
            "Type to filter; Enter to apply, Esc to cancel",
        ),
        Mode::Message => draw_message_popup(f, size, &app.status),
        Mode::Browse => {}
    }
}

fn draw_button_bar(f: &mut Frame, area: Rect, app: &mut App) {
    let buttons: &[(Action, &str, u16)] = &[
        (Action::ExtractFile, "Extract file", 16),
        (Action::ExtractDir, "Extract folder", 18),
        (Action::ExtractAll, "Extract all", 15),
        (Action::Add, "Add", 8),
        (Action::Update, "Update", 11),
        (Action::Filter, "Filter", 11),
        (Action::Quit, "Quit", 9),
    ];

    let mut constraints: Vec<Constraint> = buttons.iter().map(|(_, _, w)| Constraint::Length(*w)).collect();
    constraints.push(Constraint::Min(0));

    let cols = Layout::default()
        .direction(Direction::Horizontal)
        .constraints(constraints)
        .split(area);

    for (i, (action, label, _)) in buttons.iter().enumerate() {
        let rect = cols[i];
        let block = Block::default()
            .borders(Borders::ALL)
            .style(Style::default().fg(Color::White).bg(Color::DarkGray));
        let p = Paragraph::new(Line::from(vec![Span::styled(
            format!(" {} ", label),
            Style::default().fg(Color::White).add_modifier(Modifier::BOLD),
        )]))
        .alignment(ratatui::layout::Alignment::Center)
        .block(block);
        f.render_widget(p, rect);
        app.zones.push(Zone {
            rect,
            kind: ZoneKind::Button(*action),
        });
    }
}

fn draw_title(f: &mut Frame, area: Rect, app: &App) {
    let total = app.entries.iter().filter(|e| !e.is_deleted).count();
    let title = format!(
        " Rose VFS Browser — {} — {} files ",
        app.vfs.idx_path.display(),
        total
    );
    let p = Paragraph::new(title).block(Block::default().borders(Borders::ALL));
    f.render_widget(p, area);
}

fn draw_body(f: &mut Frame, area: Rect, app: &mut App) {
    let cols = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Percentage(60), Constraint::Percentage(40)])
        .split(area);

    draw_tree(f, cols[0], app);
    draw_info(f, cols[1], app);
}

fn draw_tree(f: &mut Frame, area: Rect, app: &mut App) {
    app.tree_area = area;

    // Inner content area (inside the border) — this is where rows live.
    let inner_height = area.height.saturating_sub(2) as usize;
    app.ensure_visible(inner_height);

    let items: Vec<ListItem> = app
        .visible
        .iter()
        .map(|row| {
            let indent = "  ".repeat(row.depth);
            let icon = if row.is_dir {
                if row.is_expanded {
                    "▼ "
                } else {
                    "▶ "
                }
            } else {
                "  "
            };
            let style = if row.is_dir {
                Style::default()
                    .fg(Color::Cyan)
                    .add_modifier(Modifier::BOLD)
            } else {
                Style::default().fg(Color::White)
            };
            let line = Line::from(vec![
                Span::raw(indent),
                Span::raw(icon),
                Span::styled(row.name.clone(), style),
            ]);
            ListItem::new(line)
        })
        .collect();

    let title = if app.filter.is_empty() {
        " Files ".to_string()
    } else {
        format!(" Files (filter: {}) ", app.filter)
    };

    let list = List::new(items)
        .block(Block::default().borders(Borders::ALL).title(title))
        .highlight_style(
            Style::default()
                .bg(Color::Blue)
                .fg(Color::White)
                .add_modifier(Modifier::BOLD),
        )
        .highlight_symbol("> ");

    let mut state = ListState::default();
    if !app.visible.is_empty() {
        state.select(Some(app.selected));
    }
    *state.offset_mut() = app.scroll_offset;
    f.render_stateful_widget(list, area, &mut state);

    // Register clickable zones for each visible row (account for border).
    let row_x = area.x + 1;
    let row_w = area.width.saturating_sub(2);
    let row_y_base = area.y + 1;
    for screen_row in 0..inner_height {
        let visible_index = app.scroll_offset + screen_row;
        if visible_index >= app.visible.len() {
            break;
        }
        app.zones.push(Zone {
            rect: Rect {
                x: row_x,
                y: row_y_base + screen_row as u16,
                width: row_w,
                height: 1,
            },
            kind: ZoneKind::TreeRow(visible_index),
        });
    }
}

fn draw_info(f: &mut Frame, area: Rect, app: &App) {
    let block = Block::default().borders(Borders::ALL).title(" Info ");

    let lines: Vec<Line> = if let Some(row) = app.selected_row() {
        if let Some(entry) = app.selected_entry() {
            let vfs_name = app
                .vfs
                .index
                .file_systems
                .get(entry.vfs_index)
                .map(|v| v.filename.to_string_lossy().to_string())
                .unwrap_or_default();
            vec![
                info_line("Name", &row.name),
                info_line("Path", &entry.path),
                info_line("Size", &format!("{} bytes", entry.size)),
                info_line("Block", &format!("{} bytes", entry.block_size)),
                info_line("Offset", &format!("0x{:08X}", entry.offset)),
                info_line("VFS", &vfs_name),
                info_line("Version", &entry.version.to_string()),
                info_line("Compressed", &yes_no(entry.is_compressed)),
                info_line("Encrypted", &yes_no(entry.is_encrypted)),
                info_line("Deleted", &yes_no(entry.is_deleted)),
            ]
        } else if row.is_dir {
            let total = count_files_under(&app.entries, &row.full_path);
            let size_sum = sum_size_under(&app.entries, &row.full_path);
            vec![
                info_line("Folder", &row.full_path),
                info_line("Files", &total.to_string()),
                info_line("Total size", &format!("{} bytes", size_sum)),
                Line::from(""),
                Line::from(Span::styled(
                    "Press [D] to extract this folder",
                    Style::default().fg(Color::Yellow),
                )),
            ]
        } else {
            vec![Line::from("No selection")]
        }
    } else {
        vec![Line::from("Empty")]
    };

    let p = Paragraph::new(lines).block(block).wrap(Wrap { trim: false });
    f.render_widget(p, area);
}

fn info_line(label: &str, value: &str) -> Line<'static> {
    Line::from(vec![
        Span::styled(
            format!("{:<11} ", format!("{}:", label)),
            Style::default().fg(Color::Gray),
        ),
        Span::styled(value.to_string(), Style::default().fg(Color::White)),
    ])
}

fn yes_no(b: bool) -> String {
    if b {
        "yes".to_string()
    } else {
        "no".to_string()
    }
}

fn count_files_under(entries: &[crate::vfs::VfsEntry], prefix: &str) -> usize {
    let prefix_norm = crate::vfs::normalize_path(prefix);
    entries
        .iter()
        .filter(|e| !e.is_deleted)
        .filter(|e| {
            let p = crate::vfs::normalize_path(&e.path);
            p == prefix_norm || p.starts_with(&format!("{}/", prefix_norm))
        })
        .count()
}

fn sum_size_under(entries: &[crate::vfs::VfsEntry], prefix: &str) -> i64 {
    let prefix_norm = crate::vfs::normalize_path(prefix);
    entries
        .iter()
        .filter(|e| !e.is_deleted)
        .filter(|e| {
            let p = crate::vfs::normalize_path(&e.path);
            p == prefix_norm || p.starts_with(&format!("{}/", prefix_norm))
        })
        .map(|e| e.size as i64)
        .sum()
}

fn draw_status(f: &mut Frame, area: Rect, app: &App) {
    let help = match app.mode {
        Mode::Browse => " ↑↓ nav  Enter/→ expand  E file  D folder  X all  A add  U update  / filter  Q quit ",
        Mode::Filter => " Type filter  Enter apply  Esc cancel ",
        Mode::Message => " Any key to dismiss ",
    };
    let line = Line::from(vec![
        Span::styled(
            format!(" {} ", app.status),
            Style::default().fg(Color::Yellow),
        ),
        Span::raw("  "),
        Span::styled(help, Style::default().fg(Color::DarkGray)),
    ]);
    let p = Paragraph::new(line).block(Block::default().borders(Borders::ALL));
    f.render_widget(p, area);
}

fn draw_input_popup(f: &mut Frame, area: Rect, title: &str, input: &str, prompt: &str) {
    let popup = centered_rect(70, 30, area);
    f.render_widget(Clear, popup);

    let block = Block::default()
        .borders(Borders::ALL)
        .title(format!(" {} ", title))
        .style(Style::default().bg(Color::Black));

    let inner = block.inner(popup);
    f.render_widget(block, popup);

    let rows = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(2),
            Constraint::Length(3),
            Constraint::Min(0),
        ])
        .split(inner);

    let prompt_p =
        Paragraph::new(prompt).wrap(Wrap { trim: true }).style(Style::default().fg(Color::Gray));
    f.render_widget(prompt_p, rows[0]);

    let input_block = Block::default().borders(Borders::ALL);
    let input_p = Paragraph::new(input.to_string())
        .block(input_block)
        .style(Style::default().fg(Color::White));
    f.render_widget(input_p, rows[1]);
}

fn draw_message_popup(f: &mut Frame, area: Rect, msg: &str) {
    let popup = centered_rect(60, 20, area);
    f.render_widget(Clear, popup);
    let block = Block::default()
        .borders(Borders::ALL)
        .title(" Message ")
        .style(Style::default().bg(Color::Black));
    let p = Paragraph::new(msg.to_string())
        .block(block)
        .wrap(Wrap { trim: true })
        .style(Style::default().fg(Color::White));
    f.render_widget(p, popup);
}

fn centered_rect(percent_x: u16, percent_y: u16, area: Rect) -> Rect {
    let vertical = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Percentage((100 - percent_y) / 2),
            Constraint::Percentage(percent_y),
            Constraint::Percentage((100 - percent_y) / 2),
        ])
        .split(area);

    Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage((100 - percent_x) / 2),
            Constraint::Percentage(percent_x),
            Constraint::Percentage((100 - percent_x) / 2),
        ])
        .split(vertical[1])[1]
}
