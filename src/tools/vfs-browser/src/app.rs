use std::collections::HashSet;

use ratatui::layout::Rect;

use crate::vfs::{TreeNode, Vfs, VfsEntry};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    ExtractFile,
    ExtractDir,
    ExtractAll,
    Add,
    Update,
    Filter,
    Quit,
}

#[derive(Debug, Clone)]
pub enum ZoneKind {
    TreeRow(usize),
    Button(Action),
}

#[derive(Debug, Clone)]
pub struct Zone {
    pub rect: Rect,
    pub kind: ZoneKind,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Mode {
    Browse,
    Filter,
    Message,
}

/// A single visible row in the tree view.
#[derive(Debug, Clone)]
pub struct VisibleRow {
    pub depth: usize,
    pub name: String,
    pub full_path: String,
    pub is_dir: bool,
    pub is_expanded: bool,
    pub entry_index: Option<usize>,
}

pub struct App {
    pub vfs: Vfs,
    pub entries: Vec<VfsEntry>,
    pub tree: TreeNode,
    pub expanded: HashSet<String>,
    pub visible: Vec<VisibleRow>,
    pub selected: usize,
    pub scroll_offset: usize,
    pub tree_area: Rect,
    pub zones: Vec<Zone>,
    pub mode: Mode,
    pub input: String,
    pub status: String,
    pub filter: String,
    pub should_quit: bool,
}

impl App {
    pub fn new(vfs: Vfs) -> Self {
        let entries = vfs.entries();
        let tree = TreeNode::build(&entries);
        let mut app = Self {
            vfs,
            entries,
            tree,
            expanded: HashSet::new(),
            visible: Vec::new(),
            selected: 0,
            scroll_offset: 0,
            tree_area: Rect::default(),
            zones: Vec::new(),
            mode: Mode::Browse,
            input: String::new(),
            status: String::from("Ready. [?] for help"),
            filter: String::new(),
            should_quit: false,
        };
        app.rebuild_visible();
        app
    }

    pub fn rebuild_visible(&mut self) {
        self.visible.clear();
        let filter = self.filter.to_ascii_uppercase();
        let tree = std::mem::take(&mut self.tree);
        let expanded_snapshot = self.expanded.clone();
        Self::walk(
            &tree,
            0,
            &expanded_snapshot,
            &filter,
            &mut self.visible,
        );
        self.tree = tree;

        if self.selected >= self.visible.len() {
            self.selected = self.visible.len().saturating_sub(1);
        }
    }

    fn walk(
        node: &TreeNode,
        depth: usize,
        expanded: &HashSet<String>,
        filter: &str,
        out: &mut Vec<VisibleRow>,
    ) {
        for child in &node.children {
            let matches = filter.is_empty()
                || child
                    .full_path
                    .to_ascii_uppercase()
                    .contains(filter)
                || Self::subtree_matches(child, filter);

            if !matches {
                continue;
            }

            let expanded_here = expanded.contains(&child.full_path)
                || (!filter.is_empty() && child.is_dir);

            out.push(VisibleRow {
                depth,
                name: child.name.clone(),
                full_path: child.full_path.clone(),
                is_dir: child.is_dir,
                is_expanded: expanded_here,
                entry_index: child.entry_index,
            });

            if child.is_dir && expanded_here {
                Self::walk(child, depth + 1, expanded, filter, out);
            }
        }
    }

    fn subtree_matches(node: &TreeNode, filter: &str) -> bool {
        for child in &node.children {
            if child.full_path.to_ascii_uppercase().contains(filter) {
                return true;
            }
            if child.is_dir && Self::subtree_matches(child, filter) {
                return true;
            }
        }
        false
    }

    pub fn selected_row(&self) -> Option<&VisibleRow> {
        self.visible.get(self.selected)
    }

    pub fn selected_entry(&self) -> Option<&VfsEntry> {
        self.selected_row()
            .and_then(|r| r.entry_index)
            .and_then(|i| self.entries.get(i))
    }

    pub fn move_up(&mut self) {
        if self.selected > 0 {
            self.selected -= 1;
        }
    }

    pub fn move_down(&mut self) {
        if self.selected + 1 < self.visible.len() {
            self.selected += 1;
        }
    }

    pub fn page_up(&mut self, amount: usize) {
        self.selected = self.selected.saturating_sub(amount);
    }

    pub fn page_down(&mut self, amount: usize) {
        self.selected = (self.selected + amount).min(self.visible.len().saturating_sub(1));
    }

    pub fn go_home(&mut self) {
        self.selected = 0;
    }

    pub fn go_end(&mut self) {
        self.selected = self.visible.len().saturating_sub(1);
    }

    pub fn toggle_expand(&mut self) {
        if let Some(row) = self.selected_row() {
            if row.is_dir {
                let path = row.full_path.clone();
                if self.expanded.contains(&path) {
                    self.expanded.remove(&path);
                } else {
                    self.expanded.insert(path);
                }
                self.rebuild_visible();
            }
        }
    }

    pub fn collapse_or_parent(&mut self) {
        if let Some(row) = self.selected_row().cloned() {
            if row.is_dir && self.expanded.contains(&row.full_path) {
                self.expanded.remove(&row.full_path);
                self.rebuild_visible();
                return;
            }
            // Jump to parent directory row.
            if let Some(parent) = parent_path(&row.full_path) {
                if let Some(idx) = self.visible.iter().position(|r| r.full_path == parent) {
                    self.selected = idx;
                }
            }
        }
    }

    pub fn set_status(&mut self, msg: impl Into<String>) {
        self.status = msg.into();
    }

    /// Adjust scroll offset so the current selection is visible in a viewport
    /// of the given height (in rows).
    pub fn ensure_visible(&mut self, viewport_height: usize) {
        if viewport_height == 0 {
            return;
        }
        if self.selected < self.scroll_offset {
            self.scroll_offset = self.selected;
        } else if self.selected >= self.scroll_offset + viewport_height {
            self.scroll_offset = self.selected + 1 - viewport_height;
        }
        let max_offset = self.visible.len().saturating_sub(viewport_height);
        if self.scroll_offset > max_offset {
            self.scroll_offset = max_offset;
        }
    }

    pub fn zone_at(&self, col: u16, row: u16) -> Option<&Zone> {
        self.zones.iter().find(|z| {
            col >= z.rect.x
                && col < z.rect.x + z.rect.width
                && row >= z.rect.y
                && row < z.rect.y + z.rect.height
        })
    }
}

fn parent_path(p: &str) -> Option<String> {
    p.rsplit_once('/').map(|(parent, _)| parent.to_string())
}
