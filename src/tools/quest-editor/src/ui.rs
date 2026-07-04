//! Phase 5 — the friendly creation wizard (egui).
//!
//! Goal: a non-developer can create a working "kill N monsters" quest without
//! touching the CLI or knowing anything about QSD/STB internals. The flow is a
//! single scrollable form: pick a monster → set the count → set rewards → tweak
//! the text → Create. A few developer knobs live behind an "Advanced" section.

use std::path::{Path, PathBuf};

use eframe::egui;

use crate::data::{DataSet, Item, ItemCategory, Monster};
use crate::gen::{generate, Objective, QuestKind, QuestSpec};
use crate::write::{apply_quest, delete_quest, list_editor_quests};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum QuestType {
    Hunt,
    Fetch,
}

/// One editable *extra* objective in the wizard (on top of the primary one). The
/// fields mirror both objective types; only the ones for `kind` are read.
struct ObjectiveDraft {
    kind: QuestType,
    count: i32,
    // Hunt
    monster_search: String,
    monster_id: Option<i32>,
    token_name: String,
    token_icon: String,
    open_icon_grid: bool,
    // Fetch
    item_category: ItemCategory,
    item_search: String,
    item_id: Option<i32>,
    consume: bool,
}

impl ObjectiveDraft {
    fn new(kind: QuestType) -> Self {
        Self {
            kind,
            count: if kind == QuestType::Hunt { 10 } else { 3 },
            monster_search: String::new(),
            monster_id: None,
            token_name: String::new(),
            token_icon: String::new(),
            open_icon_grid: false,
            item_category: ItemCategory::UseItem,
            item_search: String::new(),
            item_id: None,
            consume: true,
        }
    }
    /// A draft is "ready" once its target is picked; otherwise it's skipped when
    /// building the spec and the wizard blocks creation.
    fn is_ready(&self) -> bool {
        match self.kind {
            QuestType::Hunt => self.monster_id.is_some(),
            QuestType::Fetch => self.item_id.is_some(),
        }
    }
}

/// Top-level view: make a new quest, or manage the ones the editor made.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum View {
    Create,
    Manage,
}

/// Zone-IFO scan result: (placed npc ids, npc id -> existing .CON name).
type PlacementInfo = (
    std::collections::HashSet<i32>,
    std::collections::HashMap<i32, String>,
);

const MAX_TOKEN_ITEM_ID: i32 = 999; // type*1000+id encoding limit (reward items)

/// Visual theme: one dark, rose-accented palette shared by every screen so the
/// wizard looks intentional instead of default-egui gray.
mod theme {
    use eframe::egui::Color32;

    pub const ACCENT: Color32 = Color32::from_rgb(236, 130, 152); // rose highlight
    pub const ACCENT_STRONG: Color32 = Color32::from_rgb(178, 62, 88); // filled buttons / badges
    pub const ACCENT_DIM: Color32 = Color32::from_rgb(96, 46, 60); // selection background

    pub const BG_APP: Color32 = Color32::from_rgb(26, 28, 35); // central panel
    pub const BG_BAR: Color32 = Color32::from_rgb(20, 22, 28); // header + action bar
    pub const BG_CARD: Color32 = Color32::from_rgb(33, 36, 45); // section cards
    pub const BG_NESTED: Color32 = Color32::from_rgb(28, 30, 38); // frames inside cards
    pub const BG_INPUT: Color32 = Color32::from_rgb(17, 19, 25); // text edits / list boxes
    pub const OUTLINE: Color32 = Color32::from_rgb(56, 61, 75);

    pub const TEXT: Color32 = Color32::from_rgb(216, 220, 230);
    pub const GOOD: Color32 = Color32::from_rgb(125, 200, 140);
    pub const WARN: Color32 = Color32::from_rgb(232, 181, 86);
    pub const ERR: Color32 = Color32::from_rgb(240, 115, 115);
    pub const INFO: Color32 = Color32::from_rgb(125, 175, 255);
    pub const IN_USE: Color32 = Color32::from_rgb(205, 145, 75); // monsters that already have a quest
}

/// One-time egui style setup: fonts a touch larger, consistent rounding, and
/// the theme palette wired into every widget state.
fn apply_style(ctx: &egui::Context) {
    use egui::{FontFamily, FontId, Rounding, Stroke, TextStyle};
    let mut style = (*ctx.style()).clone();
    // Always start from dark visuals — the palette below assumes them, and the
    // system theme may be light (which made titles render near-black).
    style.visuals = egui::Visuals::dark();

    style.text_styles.insert(
        TextStyle::Heading,
        FontId::new(19.0, FontFamily::Proportional),
    );
    style
        .text_styles
        .insert(TextStyle::Body, FontId::new(13.5, FontFamily::Proportional));
    style.text_styles.insert(
        TextStyle::Button,
        FontId::new(13.5, FontFamily::Proportional),
    );
    style.text_styles.insert(
        TextStyle::Small,
        FontId::new(11.0, FontFamily::Proportional),
    );
    style.text_styles.insert(
        TextStyle::Monospace,
        FontId::new(12.5, FontFamily::Monospace),
    );

    style.spacing.item_spacing = egui::vec2(8.0, 6.0);
    style.spacing.button_padding = egui::vec2(10.0, 5.0);
    style.spacing.interact_size.y = 24.0;

    let v = &mut style.visuals;
    v.panel_fill = theme::BG_APP;
    v.window_fill = theme::BG_CARD;
    v.extreme_bg_color = theme::BG_INPUT;
    v.faint_bg_color = egui::Color32::from_rgb(40, 44, 55);
    v.selection.bg_fill = theme::ACCENT_DIM;
    v.selection.stroke = Stroke::new(1.0, theme::ACCENT);
    v.hyperlink_color = theme::ACCENT;
    v.slider_trailing_fill = true;

    let rounding = Rounding::same(5.0);
    for w in [
        &mut v.widgets.noninteractive,
        &mut v.widgets.inactive,
        &mut v.widgets.hovered,
        &mut v.widgets.active,
        &mut v.widgets.open,
    ] {
        w.rounding = rounding;
    }
    v.widgets.noninteractive.bg_stroke = Stroke::new(1.0, theme::OUTLINE);
    v.widgets.noninteractive.fg_stroke.color = theme::TEXT;
    v.widgets.inactive.weak_bg_fill = egui::Color32::from_rgb(45, 50, 62);
    v.widgets.inactive.fg_stroke.color = egui::Color32::from_rgb(200, 205, 216);
    v.widgets.hovered.weak_bg_fill = egui::Color32::from_rgb(58, 64, 80);
    v.widgets.hovered.bg_stroke = Stroke::new(1.0, theme::ACCENT_DIM);
    v.widgets.active.weak_bg_fill = theme::ACCENT_DIM;

    ctx.set_style(style);
}

/// A recessed "list box" wrapper so searchable result lists read as one input
/// control instead of naked rows floating in the card.
fn list_frame(ui: &mut egui::Ui, body: impl FnOnce(&mut egui::Ui)) {
    egui::Frame::none()
        .fill(theme::BG_INPUT)
        .stroke(egui::Stroke::new(1.0, theme::OUTLINE))
        .rounding(egui::Rounding::same(5.0))
        .inner_margin(egui::Margin::same(4.0))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            body(ui);
        });
}

pub fn run() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([960.0, 800.0])
            .with_min_inner_size([760.0, 600.0])
            .with_title("ROSE Quest Creator"),
        // The custom palette is dark; never follow a light system theme (it
        // re-applies light visuals over ours and text goes black-on-dark).
        follow_system_theme: false,
        default_theme: eframe::Theme::Dark,
        ..Default::default()
    };
    eframe::run_native(
        "ROSE Quest Creator",
        options,
        Box::new(|cc| {
            apply_style(&cc.egui_ctx);
            Box::new(QuestCreator::default())
        }),
    )
}

/// After a successful create we show a results screen instead of the form.
enum Screen {
    Form,
    Created { summary: CreatedSummary },
}

#[derive(Clone)]
struct CreatedSummary {
    quest_sn: i32,
    quest_type: QuestType,
    objective_name: String,
    count: i32,
    complete_trigger: String,
    register_trigger: String,
    changes: Vec<String>,
    backups: Vec<PathBuf>,
    /// Whether this was a dry run (an edit always commits, regardless of the
    /// dry-run toggle, because it already deleted the old quest).
    effective_dry: bool,
    /// True if this was an edit (delete old + create new) rather than a fresh create.
    was_edit: bool,
}

struct QuestCreator {
    root: Option<PathBuf>,
    data: Option<DataSet>,
    load_error: Option<String>,

    // form state
    quest_type: QuestType,
    monster_search: String,
    selected_monster: Option<i32>,
    // Fetch: which existing item the player must bring
    fetch_item_category: ItemCategory,
    fetch_item_search: String,
    fetch_item_id: Option<i32>,
    fetch_consume: bool,
    kill_count: i32,
    // extra objectives layered on top of the primary one (kill A and B, …)
    extra_objectives: Vec<ObjectiveDraft>,
    repeatable: bool,
    reward_exp: i32,
    reward_zuly: i32,

    reward_item_enabled: bool,
    reward_item_category: ItemCategory,
    reward_item_search: String,
    reward_item_id: Option<i32>,
    reward_item_qty: i32,

    title: String,
    start_text: String,
    progress_text: String,
    complete_text: String,
    token_name: String,
    token_desc: String,
    // when true, the text fields are auto-derived and regenerate as the monster
    // / count change; set false the moment the user edits any text field.
    auto_text: bool,
    text_basis: Option<(QuestType, i32, i32)>,

    // quest-giver NPC (optional): assign an NPC to give the quest from dialog
    assign_giver: bool,
    giver_search: String,
    giver_npc: Option<i32>,
    /// Zone-IFO placement info: (placed npc ids, npc id -> existing .CON).
    /// Scanned once per data folder on a background thread (the scan reads
    /// every zone IFO and would freeze the UI if done inline).
    placements: Option<PlacementInfo>,
    /// Receiver for an in-flight background placement scan.
    placements_rx: Option<std::sync::mpsc::Receiver<PlacementInfo>>,

    // dev knobs
    hide_in_use: bool,
    token_icon: String, // optional icon-number override (blank = template's icon)
    dry_run: bool,

    // icon picker (lazy): the item-icon atlas + whether the browse grid is open
    icons: Option<crate::icons::IconStore>,
    show_icon_grid: bool,

    // create vs manage; when `editing` is set the form is editing that quest SN
    // (saving deletes the old one and creates a fresh one).
    view: View,
    editing: Option<i32>,
    manage_status: Option<String>,
    confirm_delete: Option<i32>,

    screen: Screen,
    error: Option<String>,

    // cached by `ui_preview` each frame for the sticky bottom action bar (the
    // bar renders before the form, so it reads last frame's values)
    bar_can_create: bool,
    bar_summary: String,
}

impl Default for QuestCreator {
    fn default() -> Self {
        Self {
            root: None,
            data: None,
            load_error: None,
            quest_type: QuestType::Hunt,
            monster_search: String::new(),
            selected_monster: None,
            fetch_item_category: ItemCategory::UseItem,
            fetch_item_search: String::new(),
            fetch_item_id: None,
            fetch_consume: true,
            kill_count: 10,
            extra_objectives: Vec::new(),
            repeatable: true,
            reward_exp: 1000,
            reward_zuly: 500,
            reward_item_enabled: false,
            reward_item_category: ItemCategory::UseItem,
            reward_item_search: String::new(),
            reward_item_id: None,
            reward_item_qty: 1,
            title: String::new(),
            start_text: String::new(),
            progress_text: String::new(),
            complete_text: String::new(),
            token_name: String::new(),
            token_desc: String::new(),
            auto_text: true,
            text_basis: None,
            assign_giver: false,
            giver_search: String::new(),
            giver_npc: None,
            placements: None,
            placements_rx: None,
            hide_in_use: false,
            token_icon: String::new(),
            dry_run: false,
            icons: None,
            show_icon_grid: false,
            view: View::Create,
            editing: None,
            manage_status: None,
            confirm_delete: None,
            screen: Screen::Form,
            error: None,
            bar_can_create: false,
            bar_summary: String::new(),
        }
    }
}

impl eframe::App for QuestCreator {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::TopBottomPanel::top("top")
            .frame(
                egui::Frame::none()
                    .fill(theme::BG_BAR)
                    .inner_margin(egui::Margin::symmetric(12.0, 8.0)),
            )
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    ui.label(egui::RichText::new("⚔").size(20.0).color(theme::ACCENT));
                    ui.label(
                        egui::RichText::new("ROSE Quest Creator")
                            .size(17.0)
                            .strong(),
                    );
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if self.data.is_some() {
                            if ui.button("📁 Change folder…").clicked() {
                                self.pick_folder();
                            }
                            if let Some(root) = &self.root {
                                ui.add(
                                    egui::Label::new(
                                        egui::RichText::new(root.display().to_string())
                                            .weak()
                                            .small(),
                                    )
                                    .truncate(true),
                                );
                            }
                        }
                    });
                });
            });

        // Sticky action bar (summary + create button) — only on the create form,
        // so the main action never scrolls out of reach.
        if self.data.is_some() && matches!(self.screen, Screen::Form) && self.view == View::Create {
            egui::TopBottomPanel::bottom("action_bar")
                .frame(
                    egui::Frame::none()
                        .fill(theme::BG_BAR)
                        .inner_margin(egui::Margin::symmetric(12.0, 9.0)),
                )
                .show(ctx, |ui| self.ui_action_bar(ui));
        }

        egui::CentralPanel::default()
            .frame(
                egui::Frame::none()
                    .fill(theme::BG_APP)
                    .inner_margin(egui::Margin::symmetric(14.0, 10.0)),
            )
            .show(ctx, |ui| {
                if self.data.is_none() {
                    self.ui_pick_folder(ui);
                    return;
                }
                if let Screen::Created { .. } = self.screen {
                    self.ui_created(ui);
                    return;
                }
                // Create / Manage tabs (only on the form screen).
                ui.horizontal(|ui| {
                    for (v, label) in [(View::Create, "➕  Create"), (View::Manage, "🗂  Manage")]
                    {
                        let active = self.view == v;
                        let text = if active {
                            egui::RichText::new(label).strong().color(theme::ACCENT)
                        } else {
                            egui::RichText::new(label)
                        };
                        if ui.selectable_label(active, text).clicked() {
                            self.view = v;
                            if v == View::Manage {
                                self.manage_status = None;
                            }
                        }
                    }
                });
                ui.add_space(2.0);
                ui.separator();
                ui.add_space(6.0);
                match self.view {
                    View::Create => {
                        egui::ScrollArea::vertical().show(ui, |ui| self.ui_form(ui));
                    }
                    View::Manage => {
                        egui::ScrollArea::vertical().show(ui, |ui| self.ui_manage(ui));
                    }
                }
            });
    }
}

impl QuestCreator {
    fn pick_folder(&mut self) {
        if let Some(dir) = rfd::FileDialog::new()
            .set_title("Pick your game data folder (the one containing 3DDATA)")
            .pick_folder()
        {
            self.load_root(&dir);
        }
    }

    fn load_root(&mut self, dir: &Path) {
        match DataSet::load(dir) {
            Ok(ds) => {
                self.data = Some(ds);
                self.root = Some(dir.to_path_buf());
                self.load_error = None;
                self.screen = Screen::Form;
                self.error = None;
                self.placements = None; // stale for the new folder
                self.placements_rx = None; // orphan any in-flight scan (its send just fails)
                self.icons = None; // reload the icon atlas for the new folder
                                   // Kick the IFO scan off right away so the results are usually
                                   // ready long before the quest-giver checkbox is ticked.
                self.start_placement_scan();
            }
            Err(e) => {
                self.load_error = Some(format!("{e:#}"));
                self.data = None;
            }
        }
    }

    /// Start the zone-IFO scan on a background thread (one pass over every IFO,
    /// so the UI never freezes). Results are cached until the folder changes.
    fn start_placement_scan(&mut self) {
        if self.placements.is_some() || self.placements_rx.is_some() {
            return;
        }
        let Some(root) = self.root.clone() else {
            return;
        };
        let (tx, rx) = std::sync::mpsc::channel();
        self.placements_rx = Some(rx);
        std::thread::spawn(move || {
            let (placed, with_dialog) = crate::ifo::scan_npc_placements(&root).unwrap_or_default();
            // The receiver may be gone if the user switched folders — ignore.
            let _ = tx.send((
                placed.into_iter().collect(),
                with_dialog.into_iter().collect(),
            ));
        });
    }

    /// Poll a running placement scan; returns true once results are available.
    fn poll_placements(&mut self) -> bool {
        if self.placements.is_some() {
            return true;
        }
        self.start_placement_scan();
        if let Some(rx) = &self.placements_rx {
            match rx.try_recv() {
                Ok(info) => {
                    self.placements = Some(info);
                    self.placements_rx = None;
                }
                Err(std::sync::mpsc::TryRecvError::Empty) => {}
                Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                    // Scan thread died — treat as "nothing placed" instead of
                    // spinning forever.
                    self.placements = Some(Default::default());
                    self.placements_rx = None;
                }
            }
        }
        self.placements.is_some()
    }

    fn ui_pick_folder(&mut self, ui: &mut egui::Ui) {
        ui.add_space(90.0);
        ui.vertical_centered(|ui| {
            egui::Frame::none()
                .fill(theme::BG_CARD)
                .stroke(egui::Stroke::new(1.0, theme::OUTLINE))
                .rounding(egui::Rounding::same(10.0))
                .inner_margin(egui::Margin::symmetric(30.0, 24.0))
                .show(ui, |ui| {
                    ui.set_max_width(430.0);
                    ui.vertical_centered(|ui| {
                        ui.label(egui::RichText::new("⚔").size(36.0).color(theme::ACCENT));
                        ui.add_space(4.0);
                        ui.heading("Welcome!");
                        ui.add_space(8.0);
                        ui.label("To start, pick your game data folder.");
                        ui.label(
                            egui::RichText::new(
                                "(the folder that contains 3DDATA — e.g. your repo's data\\)",
                            )
                            .weak(),
                        );
                        ui.add_space(18.0);
                        let btn = egui::Button::new(
                            egui::RichText::new("📁  Select data folder…")
                                .size(15.0)
                                .strong()
                                .color(egui::Color32::WHITE),
                        )
                        .fill(theme::ACCENT_STRONG)
                        .rounding(egui::Rounding::same(6.0))
                        .min_size(egui::vec2(230.0, 38.0));
                        if ui.add(btn).clicked() {
                            self.pick_folder();
                        }
                        if let Some(err) = &self.load_error {
                            ui.add_space(14.0);
                            ui.colored_label(theme::ERR, err);
                        }
                    });
                });
        });
    }

    fn ui_form(&mut self, ui: &mut egui::Ui) {
        // Refresh auto-text if the monster / count changed.
        self.maybe_refresh_text();

        ui.add_space(2.0);

        // Editing banner.
        if let Some(sn) = self.editing {
            egui::Frame::none()
                .fill(theme::BG_NESTED)
                .stroke(egui::Stroke::new(1.0, theme::INFO.gamma_multiply(0.4)))
                .rounding(egui::Rounding::same(6.0))
                .inner_margin(egui::Margin::symmetric(10.0, 8.0))
                .show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.horizontal(|ui| {
                        ui.label(
                            egui::RichText::new(format!(
                                "✏ Editing quest #{sn} — saving deletes it and creates an updated copy."
                            ))
                            .color(theme::INFO),
                        );
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if ui.button("Cancel edit").clicked() {
                                self.editing = None;
                                self.view = View::Manage;
                            }
                        });
                    });
                });
            ui.add_space(8.0);
        }

        // --- 1 (target) and 2 (rewards) side by side ---
        let sec1_title = match self.quest_type {
            QuestType::Hunt => "Target — which monster?",
            QuestType::Fetch => "Target — which item?",
        };
        ui.columns(2, |cols| {
            self.section(&mut cols[0], "1", sec1_title, |app, ui| {
                ui.horizontal(|ui| {
                    ui.selectable_value(
                        &mut app.quest_type,
                        QuestType::Hunt,
                        "🗡  Hunt — kill monsters",
                    );
                    ui.selectable_value(
                        &mut app.quest_type,
                        QuestType::Fetch,
                        "📦  Fetch — bring items",
                    );
                });
                ui.horizontal(|ui| {
                    ui.checkbox(&mut app.repeatable, "Repeatable");
                    ui.label(
                        egui::RichText::new(if app.repeatable {
                            "(can be taken again after completing)"
                        } else {
                            "(one-time — once per character)"
                        })
                        .weak()
                        .small(),
                    );
                });
                ui.add_space(4.0);
                match app.quest_type {
                    QuestType::Hunt => {
                        app.ui_monster_list(ui);
                        ui.add_space(4.0);
                        ui.horizontal(|ui| {
                            ui.label("Kill count:");
                            ui.add(egui::DragValue::new(&mut app.kill_count).clamp_range(1..=999));
                        });
                        // The token icon belongs next to the monster it drops from —
                        // same layout as the extra-objective rows.
                        ui.add_space(4.0);
                        app.ui_icon_picker(ui);
                    }
                    QuestType::Fetch => app.ui_fetch_item(ui),
                }
            });

            // --- 2. Rewards ---
            self.section(&mut cols[1], "2", "Rewards", |app, ui| {
                egui::Grid::new("nums_grid")
                    .num_columns(2)
                    .spacing([16.0, 8.0])
                    .show(ui, |ui| {
                        ui.label("Experience:");
                        ui.add(
                            egui::DragValue::new(&mut app.reward_exp).clamp_range(0..=100_000_000),
                        );
                        ui.end_row();

                        ui.label("Zuly (money):");
                        ui.add(
                            egui::DragValue::new(&mut app.reward_zuly)
                                .clamp_range(0..=2_000_000_000),
                        );
                        ui.end_row();
                    });
                ui.label(
                    egui::RichText::new(
                        "One hidden quest-token drops per kill. EXP/zuly scale a little with the \
                     player (normal ROSE quest behavior).",
                    )
                    .weak()
                    .small(),
                );

                ui.add_space(6.0);
                ui.checkbox(&mut app.reward_item_enabled, "Also give an item reward");
                if app.reward_item_enabled {
                    app.ui_reward_item(ui);
                }
            });
        }); // end columns(1 + 2)

        // --- 3. Additional objectives (optional) ---
        self.section(ui, "3", "Additional objectives (optional)", |app, ui| {
            app.ui_objectives(ui);
        });

        // --- 4. Text ---
        self.section(ui, "4", "Quest text", |app, ui| {
            let edited = |app: &mut QuestCreator| app.auto_text = false;
            egui::Grid::new("text_grid")
                .num_columns(2)
                .spacing([12.0, 8.0])
                .show(ui, |ui| {
                    ui.label("Title:");
                    if ui.text_edit_singleline(&mut app.title).changed() {
                        edited(app);
                    }
                    ui.end_row();
                    ui.label("Start message:");
                    if ui.text_edit_multiline(&mut app.start_text).changed() {
                        edited(app);
                    }
                    ui.end_row();
                    ui.label("In-progress hint:");
                    if ui.text_edit_multiline(&mut app.progress_text).changed() {
                        edited(app);
                    }
                    ui.end_row();
                    ui.label("Completion message:");
                    if ui.text_edit_multiline(&mut app.complete_text).changed() {
                        edited(app);
                    }
                    ui.end_row();

                    // The token item only exists for Hunt quests.
                    if app.quest_type == QuestType::Hunt {
                        ui.label("Token item name:");
                        if ui.text_edit_singleline(&mut app.token_name).changed() {
                            edited(app);
                        }
                        ui.end_row();
                        ui.label("Token item info:");
                        if ui.text_edit_multiline(&mut app.token_desc).changed() {
                            edited(app);
                        }
                        ui.end_row();
                    }
                });
            if app.quest_type == QuestType::Hunt {
                ui.label(
                    egui::RichText::new(
                        "The token is the item the player collects on each kill — it shows in \
                         the quest-item inventory with this name.",
                    )
                    .weak()
                    .small(),
                );
            }
            ui.horizontal(|ui| {
                if ui.button("↺ Reset text to auto").clicked() {
                    app.auto_text = true;
                    app.text_basis = None;
                }
                ui.label(
                    egui::RichText::new(if app.auto_text {
                        "(auto — follows the monster name)"
                    } else {
                        "(custom)"
                    })
                    .weak()
                    .small(),
                );
            });
        });

        // --- 5. Quest-giver NPC (optional) ---
        self.section(ui, "5", "Quest-giver NPC (optional)", |app, ui| {
            app.ui_giver(ui);
        });

        // --- summary & checks (the create button lives in the bottom action bar) ---
        self.ui_preview(ui);

        // --- advanced ---
        ui.add_space(8.0);
        egui::CollapsingHeader::new("⚙ Advanced (developer)")
            .default_open(false)
            .show(ui, |ui| {
                ui.checkbox(
                    &mut self.hide_in_use,
                    "Hide monsters that already have a quest (the [+chain] ones)",
                );
                ui.checkbox(
                    &mut self.dry_run,
                    "Dry run — preview the file changes but write nothing",
                );
                if let Some(ds) = &self.data {
                    ui.add_space(4.0);
                    ui.label(
                        egui::RichText::new(format!(
                            "next quest SN: {}   ·   next token item: 13:{}   ·   monsters: {}",
                            ds.next_free_quest_sn(),
                            ds.next_free_quest_item_id(),
                            ds.monsters.len()
                        ))
                        .weak()
                        .small(),
                    );
                }
            });
    }

    /// Hunt objective: searchable monster list (in-use ones flagged).
    fn ui_monster_list(&mut self, ui: &mut egui::Ui) {
        ui.add(
            egui::TextEdit::singleline(&mut self.monster_search)
                .hint_text("🔍  Search monsters by name or id…")
                .desired_width(f32::INFINITY),
        );

        // Collect matching rows first so the data borrow drops before we mutate.
        let search = self.monster_search.trim().to_lowercase();
        let hide_in_use = self.hide_in_use;
        let rows: Vec<(i32, String, bool)> = {
            let data = self.data.as_ref().unwrap();
            data.monsters
                .iter()
                .filter(|m| !hide_in_use || m.dead_event_is_free())
                .filter(|m| {
                    search.is_empty()
                        || m.name.to_lowercase().contains(&search)
                        || m.id.to_string().contains(&search)
                })
                .map(|m| (m.id, monster_label(m), m.dead_event_is_free()))
                .collect()
        };
        let total = rows.len();
        let cap = 300usize;

        list_frame(ui, |ui| {
            egui::ScrollArea::vertical()
                .id_source("monster_list")
                .max_height(220.0)
                .auto_shrink([false, false])
                .show(ui, |ui| {
                    for (id, label, free) in rows.iter().take(cap) {
                        let selected = self.selected_monster == Some(*id);
                        let mut text = egui::RichText::new(label);
                        if !free {
                            text = text.color(theme::IN_USE);
                        }
                        if ui.selectable_label(selected, text).clicked() {
                            self.selected_monster = Some(*id);
                            self.auto_text = true;
                        }
                    }
                });
        });

        if total > cap {
            ui.label(
                egui::RichText::new(format!("…and {} more — narrow your search.", total - cap))
                    .weak()
                    .small(),
            );
        }

        if let Some(id) = self.selected_monster {
            if let Some(m) = self.data.as_ref().and_then(|d| d.find_monster(id)) {
                ui.add_space(4.0);
                ui.label(egui::RichText::new(format!("Selected: {}", m.name)).strong());
                if !m.dead_event_is_free() {
                    ui.colored_label(
                        theme::INFO,
                        "ℹ This monster already has a quest — yours will be chained onto it.",
                    );
                }
            }
        }
    }

    /// Fetch objective: pick the existing item the player must bring.
    fn ui_fetch_item(&mut self, ui: &mut egui::Ui) {
        egui::ComboBox::from_label("Item type")
            .selected_text(self.fetch_item_category.display())
            .show_ui(ui, |ui| {
                for &cat in ItemCategory::ALL {
                    if ui
                        .selectable_label(self.fetch_item_category == cat, cat.display())
                        .clicked()
                    {
                        self.fetch_item_category = cat;
                        self.fetch_item_id = None;
                        self.auto_text = true;
                    }
                }
            });
        ui.add(
            egui::TextEdit::singleline(&mut self.fetch_item_search)
                .hint_text("🔍  Search items by name or id…")
                .desired_width(f32::INFINITY),
        );
        let search = self.fetch_item_search.trim().to_lowercase();

        let items: Vec<(i32, String)> = self
            .data
            .as_ref()
            .unwrap()
            .item_db
            .by_category
            .get(&self.fetch_item_category)
            .map(|v| {
                v.iter()
                    .filter(|it| it.id <= MAX_TOKEN_ITEM_ID) // type*1000+id limit
                    .filter(|it| {
                        search.is_empty()
                            || it.name.to_lowercase().contains(&search)
                            || it.id.to_string().contains(&search)
                    })
                    .map(|it| (it.id, item_label(it)))
                    .collect()
            })
            .unwrap_or_default();

        list_frame(ui, |ui| {
            egui::ScrollArea::vertical()
                .id_source("fetch_item_list")
                .max_height(200.0)
                .auto_shrink([false, false])
                .show(ui, |ui| {
                    for (id, label) in &items {
                        let selected = self.fetch_item_id == Some(*id);
                        if ui.selectable_label(selected, label).clicked() {
                            self.fetch_item_id = Some(*id);
                            self.auto_text = true;
                        }
                    }
                });
        });

        ui.add_space(4.0);
        ui.horizontal(|ui| {
            ui.label("Bring count:");
            ui.add(egui::DragValue::new(&mut self.kill_count).clamp_range(1..=999));
        });
        ui.checkbox(
            &mut self.fetch_consume,
            "Take the items when the quest is turned in",
        );
    }

    /// The list of extra objectives: each is a compact target picker + count. The
    /// quest completes only when the primary AND every extra objective is done.
    fn ui_objectives(&mut self, ui: &mut egui::Ui) {
        ui.label(
            egui::RichText::new(
                "Require more than one thing. Each objective is checked together; the quest \
                 finishes only when all of them are met.",
            )
            .weak()
            .small(),
        );
        ui.horizontal(|ui| {
            if ui.button("➕ Add kill objective").clicked() {
                self.extra_objectives
                    .push(ObjectiveDraft::new(QuestType::Hunt));
            }
            if ui.button("➕ Add fetch objective").clicked() {
                self.extra_objectives
                    .push(ObjectiveDraft::new(QuestType::Fetch));
            }
        });

        // Take the drafts out so we can borrow `self.data` / `self.icons` while
        // editing them (disjoint field borrows bound to locals before the loop).
        let mut drafts = std::mem::take(&mut self.extra_objectives);
        let data = self.data.as_ref();
        let icons = &mut self.icons;
        let root = self.root.as_deref();
        let mut remove: Option<usize> = None;
        for (i, d) in drafts.iter_mut().enumerate() {
            ui.add_space(4.0);
            egui::Frame::none()
                .fill(theme::BG_NESTED)
                .stroke(egui::Stroke::new(1.0, theme::OUTLINE))
                .rounding(egui::Rounding::same(6.0))
                .inner_margin(egui::Margin::symmetric(10.0, 8.0))
                .show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new(format!("Objective {}", i + 2)).strong());
                        ui.selectable_value(&mut d.kind, QuestType::Hunt, "🗡 Kill");
                        ui.selectable_value(&mut d.kind, QuestType::Fetch, "📦 Bring");
                        ui.add(egui::DragValue::new(&mut d.count).clamp_range(1..=999));
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if ui.button("🗑 Remove").clicked() {
                                remove = Some(i);
                            }
                        });
                    });
                    match d.kind {
                        QuestType::Hunt => Self::ui_obj_monster(data, d, ui, i, icons, root),
                        QuestType::Fetch => Self::ui_obj_item(data, d, ui, i),
                    }
                    if !d.is_ready() {
                        ui.colored_label(
                            theme::WARN,
                            "⚠ Pick a target — this objective is ignored until you do.",
                        );
                    }
                });
        }
        if let Some(i) = remove {
            drafts.remove(i);
        }
        self.extra_objectives = drafts;
    }

    /// Compact monster picker + token-icon control for one extra objective.
    fn ui_obj_monster(
        data: Option<&DataSet>,
        d: &mut ObjectiveDraft,
        ui: &mut egui::Ui,
        idx: usize,
        icons: &mut Option<crate::icons::IconStore>,
        root: Option<&Path>,
    ) {
        ui.add(
            egui::TextEdit::singleline(&mut d.monster_search)
                .hint_text("🔍  Search monsters…")
                .desired_width(f32::INFINITY),
        );
        let search = d.monster_search.trim().to_lowercase();
        let rows: Vec<(i32, String, bool)> = data
            .map(|ds| {
                ds.monsters
                    .iter()
                    .filter(|m| {
                        search.is_empty()
                            || m.name.to_lowercase().contains(&search)
                            || m.id.to_string().contains(&search)
                    })
                    .take(60)
                    .map(|m| (m.id, monster_label(m), m.dead_event_is_free()))
                    .collect()
            })
            .unwrap_or_default();
        list_frame(ui, |ui| {
            egui::ScrollArea::vertical()
                .id_source(("obj_monster", idx))
                .max_height(120.0)
                .auto_shrink([false, false])
                .show(ui, |ui| {
                    for (id, label, free) in &rows {
                        let mut text = egui::RichText::new(label);
                        if !*free {
                            text = text.color(theme::IN_USE);
                        }
                        if ui
                            .selectable_label(d.monster_id == Some(*id), text)
                            .clicked()
                        {
                            d.monster_id = Some(*id);
                            if d.token_name.trim().is_empty() {
                                if let Some(m) = data.and_then(|ds| ds.find_monster(*id)) {
                                    d.token_name = format!("{} Mark", m.name);
                                }
                            }
                        }
                    }
                });
        });
        // Per-objective token icon (default = the cloned template's icon).
        icon_picker_control(
            icons,
            root,
            ui,
            "Token icon:",
            &mut d.token_icon,
            &mut d.open_icon_grid,
            ("obj_icon", idx),
        );
    }

    /// Compact item picker for one extra objective.
    fn ui_obj_item(data: Option<&DataSet>, d: &mut ObjectiveDraft, ui: &mut egui::Ui, idx: usize) {
        ui.horizontal(|ui| {
            egui::ComboBox::from_id_source(("obj_item_cat", idx))
                .selected_text(d.item_category.display())
                .show_ui(ui, |ui| {
                    for &cat in ItemCategory::ALL {
                        if ui
                            .selectable_label(d.item_category == cat, cat.display())
                            .clicked()
                        {
                            d.item_category = cat;
                            d.item_id = None;
                        }
                    }
                });
            ui.add(
                egui::TextEdit::singleline(&mut d.item_search)
                    .hint_text("🔍  Search items…")
                    .desired_width(f32::INFINITY),
            );
        });
        let search = d.item_search.trim().to_lowercase();
        let items: Vec<(i32, String)> = data
            .and_then(|ds| ds.item_db.by_category.get(&d.item_category))
            .map(|v| {
                v.iter()
                    .filter(|it| it.id <= MAX_TOKEN_ITEM_ID)
                    .filter(|it| {
                        search.is_empty()
                            || it.name.to_lowercase().contains(&search)
                            || it.id.to_string().contains(&search)
                    })
                    .take(60)
                    .map(|it| (it.id, item_label(it)))
                    .collect()
            })
            .unwrap_or_default();
        list_frame(ui, |ui| {
            egui::ScrollArea::vertical()
                .id_source(("obj_item", idx))
                .max_height(120.0)
                .auto_shrink([false, false])
                .show(ui, |ui| {
                    for (id, label) in &items {
                        if ui.selectable_label(d.item_id == Some(*id), label).clicked() {
                            d.item_id = Some(*id);
                        }
                    }
                });
        });
        ui.checkbox(&mut d.consume, "Take these items on turn-in");
    }

    fn ui_reward_item(&mut self, ui: &mut egui::Ui) {
        egui::ComboBox::from_label("Category")
            .selected_text(self.reward_item_category.display())
            .show_ui(ui, |ui| {
                for &cat in ItemCategory::ALL {
                    if ui
                        .selectable_label(self.reward_item_category == cat, cat.display())
                        .clicked()
                    {
                        self.reward_item_category = cat;
                        self.reward_item_id = None;
                    }
                }
            });

        ui.add(
            egui::TextEdit::singleline(&mut self.reward_item_search)
                .hint_text("🔍  Search items by name or id…")
                .desired_width(f32::INFINITY),
        );
        let search = self.reward_item_search.trim().to_lowercase();

        // Collect matching items first (drops the `self.data` borrow).
        let items: Vec<(i32, String)> = self
            .data
            .as_ref()
            .unwrap()
            .item_db
            .by_category
            .get(&self.reward_item_category)
            .map(|v| {
                v.iter()
                    .filter(|it| it.id <= MAX_TOKEN_ITEM_ID) // type*1000+id limit
                    .filter(|it| {
                        search.is_empty()
                            || it.name.to_lowercase().contains(&search)
                            || it.id.to_string().contains(&search)
                    })
                    .map(|it| (it.id, item_label(it)))
                    .collect()
            })
            .unwrap_or_default();

        list_frame(ui, |ui| {
            egui::ScrollArea::vertical()
                .id_source("reward_item_list")
                .max_height(140.0)
                .auto_shrink([false, false])
                .show(ui, |ui| {
                    for (id, label) in &items {
                        let selected = self.reward_item_id == Some(*id);
                        if ui.selectable_label(selected, label).clicked() {
                            self.reward_item_id = Some(*id);
                        }
                    }
                });
        });

        ui.horizontal(|ui| {
            ui.label("Quantity:");
            ui.add(egui::DragValue::new(&mut self.reward_item_qty).clamp_range(1..=999));
        });
    }

    /// Live summary of what will be created plus validation results. The create
    /// button itself lives in the sticky bottom action bar (`ui_action_bar`);
    /// this caches `bar_can_create` / `bar_summary` for it.
    fn ui_preview(&mut self, ui: &mut egui::Ui) {
        let Some(spec) = self.build_spec() else {
            self.bar_can_create = false;
            self.bar_summary = "Pick a monster / item above to enable creation.".into();
            ui.colored_label(
                egui::Color32::GRAY,
                "Pick a monster / item above to see the summary.",
            );
            return;
        };
        let gen = crate::gen::generate(&spec);
        let issues = self
            .data
            .as_ref()
            .map(|ds| crate::verify::verify(ds, &spec, &gen))
            .unwrap_or_default();

        let verb = match self.quest_type {
            QuestType::Hunt => "Kill",
            QuestType::Fetch => "Bring",
        };
        egui::Frame::none()
            .fill(theme::BG_CARD)
            .stroke(egui::Stroke::new(1.0, theme::ACCENT_DIM))
            .rounding(egui::Rounding::same(8.0))
            .inner_margin(egui::Margin::symmetric(12.0, 10.0))
            .show(ui, |ui| {
                ui.set_width(ui.available_width());
                ui.label(
                    egui::RichText::new("Summary")
                        .strong()
                        .size(15.0)
                        .color(theme::ACCENT),
                );
                ui.add_space(2.0);
                ui.separator();
                ui.add_space(4.0);
                ui.label(format!(
                    "Quest #{} — “{}”",
                    gen.quest_sn,
                    String::from_utf8_lossy(&gen.qsd.description)
                ));
                ui.label(format!(
                    "{verb} {}× {} → rewards{}.",
                    self.kill_count,
                    self.objective_name().unwrap_or_default(),
                    self.reward_summary()
                ));
                for d in self.extra_objectives.iter().filter(|d| d.is_ready()) {
                    let (v, name) = match d.kind {
                        QuestType::Hunt => (
                            "and kill",
                            d.monster_id
                                .and_then(|id| {
                                    self.data.as_ref().and_then(|ds| ds.find_monster(id))
                                })
                                .map(|m| m.name.clone())
                                .unwrap_or_default(),
                        ),
                        QuestType::Fetch => (
                            "and bring",
                            d.item_id
                                .and_then(|id| {
                                    self.data
                                        .as_ref()
                                        .and_then(|ds| ds.item_db.lookup(d.item_category, id))
                                })
                                .map(|it| it.name.clone())
                                .unwrap_or_default(),
                        ),
                    };
                    ui.label(egui::RichText::new(format!("{v} {}× {name}", d.count)).weak());
                }
                let files = match &spec.kind {
                    QuestKind::Hunt {
                        chain_into_existing: true,
                        ..
                    } => "LIST_Quest / LIST_QUESTITEM / STL + the monster's existing quest file",
                    QuestKind::Hunt { .. } => "LIST_Quest / LIST_NPC / LIST_QUESTITEM / STL",
                    QuestKind::Fetch { .. } => "LIST_Quest / LIST_QuestDATA / STL",
                };
                ui.label(
                    egui::RichText::new(format!(
                        "Writes a new {} and updates {files} (each backed up to .bak).",
                        gen.qsd_filename
                    ))
                    .weak()
                    .small(),
                );

                // Validation issues (errors block creation; warnings are advisory).
                if !issues.is_empty() {
                    ui.add_space(4.0);
                }
                for issue in &issues {
                    let (color, icon) = match issue.level {
                        crate::verify::Level::Error => (theme::ERR, "✖"),
                        crate::verify::Level::Warning => (theme::WARN, "⚠"),
                    };
                    ui.colored_label(color, format!("{icon}  {}", issue.message));
                }
            });

        let unfinished = self.extra_objectives.iter().any(|d| !d.is_ready());
        if unfinished {
            ui.colored_label(
                theme::WARN,
                "⚠  Finish or remove the additional objective(s) above before creating.",
            );
        }

        self.bar_can_create = !crate::verify::has_errors(&issues) && !unfinished;
        let mut summary = format!(
            "Quest #{} · {verb} {}× {}{}",
            gen.quest_sn,
            self.kill_count,
            self.objective_name().unwrap_or_default(),
            self.reward_summary()
        );
        let extra = self
            .extra_objectives
            .iter()
            .filter(|d| d.is_ready())
            .count();
        if extra > 0 {
            summary.push_str(&format!(" · +{extra} extra objective(s)"));
        }
        self.bar_summary = summary;
    }

    /// Sticky bottom bar: compact status on the left, the create/save button on
    /// the right — always visible, no scrolling to reach the main action.
    fn ui_action_bar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let label = if self.editing.is_some() {
                    "💾  Save changes"
                } else if self.dry_run {
                    "🔍  Preview changes (no write)"
                } else {
                    "✅  Create Quest"
                };
                let btn = egui::Button::new(
                    egui::RichText::new(label)
                        .size(15.0)
                        .strong()
                        .color(egui::Color32::WHITE),
                )
                .fill(theme::ACCENT_STRONG)
                .rounding(egui::Rounding::same(6.0))
                .min_size(egui::vec2(200.0, 34.0));
                if ui.add_enabled(self.bar_can_create, btn).clicked() {
                    self.do_create();
                }
                ui.with_layout(
                    egui::Layout::left_to_right(egui::Align::Center),
                    |ui| match self.error.clone() {
                        Some(err) => {
                            ui.add(
                                egui::Label::new(
                                    egui::RichText::new(format!("✖ {err}")).color(theme::ERR),
                                )
                                .truncate(true),
                            )
                            .on_hover_text(err);
                        }
                        None => {
                            ui.add(
                                egui::Label::new(
                                    egui::RichText::new(self.bar_summary.as_str()).weak(),
                                )
                                .truncate(true),
                            );
                        }
                    },
                );
            });
        });
    }

    fn ui_created(&mut self, ui: &mut egui::Ui) {
        // Clone so we don't hold a borrow on self.screen while the "Create
        // another" button mutates self.
        let summary = match &self.screen {
            Screen::Created { summary } => summary.clone(),
            _ => return,
        };
        let summary = &summary;
        egui::ScrollArea::vertical().show(ui, |ui| {
            ui.add_space(8.0);
            let (title, color) = if summary.effective_dry {
                ("🔍 Dry run — nothing was written", theme::INFO)
            } else if summary.was_edit {
                ("✅ Quest updated!", theme::GOOD)
            } else {
                ("✅ Quest created!", theme::GOOD)
            };
            ui.heading(egui::RichText::new(title).color(color));
            ui.add_space(6.0);
            let verb = match summary.quest_type {
                QuestType::Hunt => "kill",
                QuestType::Fetch => "bring",
            };
            ui.label(format!(
                "Quest #{} — {verb} {}× {}.",
                summary.quest_sn, summary.count, summary.objective_name
            ));

            ui.add_space(10.0);
            ui.label(egui::RichText::new("Files changed:").strong());
            for c in &summary.changes {
                ui.label(egui::RichText::new(format!("• {c}")).small());
            }
            if !summary.backups.is_empty() {
                ui.add_space(4.0);
                ui.label(
                    egui::RichText::new(format!(
                        "Backed up {} file(s) to .bak",
                        summary.backups.len()
                    ))
                    .weak()
                    .small(),
                );
            }

            if !summary.effective_dry {
                ui.add_space(12.0);
                ui.label(egui::RichText::new("Next steps").strong());
                ui.label("1. Bake/deploy your data to the client VFS, and restart the servers.");
                ui.label("2. In-game (with a GM account), test with these chat commands:");
                let cmds = format!(
                    "    /quest {reg}        (accept the quest)\n    \
                     {verb} {n}× {obj}\n    \
                     /quest {comp}        (turn it in)",
                    reg = summary.register_trigger,
                    verb = verb,
                    n = summary.count,
                    obj = summary.objective_name,
                    comp = summary.complete_trigger,
                );
                ui.add(
                    egui::TextEdit::multiline(&mut cmds.clone())
                        .font(egui::TextStyle::Monospace)
                        .desired_width(f32::INFINITY)
                        .interactive(false),
                );
            }

            ui.add_space(14.0);
            let another = egui::Button::new(
                egui::RichText::new("➕  Create another quest")
                    .size(15.0)
                    .strong()
                    .color(egui::Color32::WHITE),
            )
            .fill(theme::ACCENT_STRONG)
            .rounding(egui::Rounding::same(6.0))
            .min_size(egui::vec2(210.0, 36.0));
            if ui.add(another).clicked() {
                // Reload data so the next quest gets fresh SN / token ids.
                if let Some(root) = self.root.clone() {
                    self.load_root(&root);
                }
                self.screen = Screen::Form;
                self.error = None;
            }
        });
    }

    fn ui_manage(&mut self, ui: &mut egui::Ui) {
        let Some(root) = self.root.clone() else {
            return;
        };
        ui.add_space(6.0);
        ui.label(egui::RichText::new("Quests you created with this tool").strong());
        ui.label(
            egui::RichText::new(
                "Edit re-creates a quest (it gets a new number). Delete removes it and frees its monster.",
            )
            .weak()
            .small(),
        );
        ui.add_space(6.0);

        if let Some(status) = self.manage_status.clone() {
            ui.colored_label(theme::GOOD, status);
            ui.add_space(4.0);
        }

        let quests = match list_editor_quests(&root) {
            Ok(q) => q,
            Err(e) => {
                ui.colored_label(theme::ERR, format!("{e:#}"));
                return;
            }
        };
        if quests.is_empty() {
            ui.add_space(10.0);
            ui.weak("No editor-created quests found — make one in the Create tab.");
            return;
        }

        let mut do_edit: Option<QuestSpec> = None;
        let mut do_delete: Option<i32> = None;
        egui::Frame::none()
            .fill(theme::BG_CARD)
            .stroke(egui::Stroke::new(1.0, theme::OUTLINE))
            .rounding(egui::Rounding::same(8.0))
            .inner_margin(egui::Margin::symmetric(12.0, 10.0))
            .show(ui, |ui| {
                ui.set_width(ui.available_width());
                egui::Grid::new("manage_grid")
                    .num_columns(4)
                    .spacing([14.0, 6.0])
                    .striped(true)
                    .show(ui, |ui| {
                        ui.label(egui::RichText::new("Quest").strong());
                        ui.label(egui::RichText::new("Type").strong());
                        ui.label(egui::RichText::new("Goal").strong());
                        ui.label("");
                        ui.end_row();
                        for q in &quests {
                            let sn = q.quest_sn;
                            ui.label(format!("#{}  {}", sn, q.title));
                            let (ty, goal) = match &q.spec {
                                Some(spec) => {
                                    let (mut ty, mut goal) = match &spec.kind {
                                        QuestKind::Hunt { monster_id, .. } => (
                                            "Hunt".to_string(),
                                            format!(
                                                "kill {}× {}",
                                                spec.count,
                                                self.monster_name(*monster_id)
                                            ),
                                        ),
                                        QuestKind::Fetch { item_name, .. } => (
                                            "Fetch".to_string(),
                                            format!("bring {}× {}", spec.count, item_name),
                                        ),
                                    };
                                    if !spec.extra_objectives.is_empty() {
                                        ty = "Multi".to_string();
                                        goal = format!(
                                            "{goal}  (+{} more)",
                                            spec.extra_objectives.len()
                                        );
                                    }
                                    (ty, goal)
                                }
                                None => {
                                    ("—".to_string(), "(older quest — delete only)".to_string())
                                }
                            };
                            ui.label(ty);
                            ui.label(egui::RichText::new(goal).weak());
                            ui.horizontal(|ui| {
                                match &q.spec {
                                    Some(spec) => {
                                        if ui.button("✏ Edit").clicked() {
                                            do_edit = Some(spec.clone());
                                        }
                                    }
                                    None => {
                                        ui.add_enabled(false, egui::Button::new("✏ Edit"))
                                    .on_disabled_hover_text(
                                        "Made before manifests — delete and recreate to change it.",
                                    );
                                    }
                                }
                                if self.confirm_delete == Some(sn) {
                                    if ui
                                        .button(
                                            egui::RichText::new("Confirm")
                                                .color(egui::Color32::WHITE),
                                        )
                                        .clicked()
                                    {
                                        do_delete = Some(sn);
                                    }
                                    if ui.button("Cancel").clicked() {
                                        self.confirm_delete = None;
                                    }
                                } else if ui.button("🗑 Delete").clicked() {
                                    self.confirm_delete = Some(sn);
                                }
                            });
                            ui.end_row();
                        }
                    });
            });

        if let Some(spec) = do_edit {
            self.load_spec_into_form(&spec);
            self.editing = Some(spec.quest_sn);
            self.confirm_delete = None;
            self.manage_status = None;
            self.view = View::Create;
        }
        if let Some(sn) = do_delete {
            self.confirm_delete = None;
            match delete_quest(&root, sn, false) {
                Ok(rep) => {
                    self.manage_status = Some(format!(
                        "Deleted quest #{sn} ({} change(s)).",
                        rep.changes.len()
                    ));
                    self.load_root(&root); // refresh data + manifest list
                }
                Err(e) => self.manage_status = Some(format!("Delete failed: {e:#}")),
            }
        }
    }

    fn ui_giver(&mut self, ui: &mut egui::Ui) {
        ui.checkbox(
            &mut self.assign_giver,
            "Make an NPC give this quest from a dialog (accept / turn-in at the NPC)",
        );
        if !self.assign_giver {
            ui.label(
                egui::RichText::new("Off: the quest is accepted/turned in via GM commands only.")
                    .weak()
                    .small(),
            );
            return;
        }

        if !self.poll_placements() {
            ui.horizontal(|ui| {
                ui.spinner();
                ui.label(
                    egui::RichText::new("Scanning zone files for placed NPCs… (first time only)")
                        .weak(),
                );
            });
            // Keep frames coming while we wait; egui only repaints on input.
            ui.ctx()
                .request_repaint_after(std::time::Duration::from_millis(100));
            return;
        }
        ui.add(
            egui::TextEdit::singleline(&mut self.giver_search)
                .hint_text("🔍  Search placed NPCs…")
                .desired_width(f32::INFINITY),
        );

        // Collect matching placed giver NPCs into an owned list (drops borrows
        // before the selectable list mutates `giver_npc`).
        let needle = self.giver_search.to_lowercase();
        let candidates: Vec<(i32, String, Option<String>)> = match (&self.placements, &self.data) {
            (Some((placed, with_dialog)), Some(ds)) => ds
                .givers
                .iter()
                .filter(|g| placed.contains(&g.id))
                .filter(|g| needle.is_empty() || g.name.to_lowercase().contains(&needle))
                .map(|g| (g.id, g.name.clone(), with_dialog.get(&g.id).cloned()))
                .take(300)
                .collect(),
            _ => Vec::new(),
        };

        if candidates.is_empty() {
            ui.weak("No placed town NPC matches (only NPCs spawned in a zone can give quests).");
        }
        list_frame(ui, |ui| {
            egui::ScrollArea::vertical()
                .max_height(150.0)
                .auto_shrink([false, true])
                .show(ui, |ui| {
                    for (id, name, conv) in &candidates {
                        let label = if conv.is_some() {
                            format!("{:>5}  {}   [has dialog]", id, name)
                        } else {
                            format!("{:>5}  {}", id, name)
                        };
                        ui.selectable_value(&mut self.giver_npc, Some(*id), label);
                    }
                });
        });

        match self
            .giver_npc
            .and_then(|id| candidates.iter().find(|(c, ..)| *c == id))
        {
            Some((id, name, conv)) => {
                ui.label(egui::RichText::new(format!("Giver: {name} (npc {id})")).strong());
                if let Some(conv) = conv {
                    ui.colored_label(
                        theme::WARN,
                        format!(
                            "⚠ This NPC already has a conversation (\"{conv}\") — it will be \
                             REPLACED. Pick an NPC without [has dialog] unless you don't mind losing it.",
                        ),
                    );
                }
            }
            None => {
                ui.colored_label(theme::INFO, "Pick a placed NPC above to give the quest.");
            }
        }
    }

    /// Primary token-icon control (section 1, under the monster picker).
    fn ui_icon_picker(&mut self, ui: &mut egui::Ui) {
        icon_picker_control(
            &mut self.icons,
            self.root.as_deref(),
            ui,
            "Token icon:",
            &mut self.token_icon,
            &mut self.show_icon_grid,
            "primary_icon",
        );
    }

    fn monster_name(&self, id: i32) -> String {
        self.data
            .as_ref()
            .and_then(|d| d.find_monster(id))
            .map(|m| m.name.clone())
            .unwrap_or_else(|| format!("monster {id}"))
    }

    /// Pre-fill the form from a saved spec (for editing).
    fn load_spec_into_form(&mut self, spec: &QuestSpec) {
        self.kill_count = spec.count;
        self.repeatable = spec.one_time_switch.is_none();
        self.reward_exp = spec.reward_exp;
        self.reward_zuly = spec.reward_zuly;
        match spec.reward_item {
            Some((sn, qty)) => {
                self.reward_item_enabled = true;
                if let Some((cat, id)) = decode_item_sn(sn) {
                    self.reward_item_category = cat;
                    self.reward_item_id = Some(id);
                }
                self.reward_item_qty = qty as i32;
            }
            None => {
                self.reward_item_enabled = false;
                self.reward_item_id = None;
            }
        }
        self.title = spec.title.clone();
        self.start_text = spec.start_text.clone();
        self.progress_text = spec.progress_text.clone();
        self.complete_text = spec.complete_text.clone();
        self.auto_text = false; // keep the loaded text as-is
        self.text_basis = None;

        // Rebuild the extra-objective drafts from the saved spec.
        self.extra_objectives = spec
            .extra_objectives
            .iter()
            .map(|o| match o {
                Objective::Hunt {
                    monster_id,
                    count,
                    token_name,
                    token_icon,
                    ..
                } => {
                    let mut d = ObjectiveDraft::new(QuestType::Hunt);
                    d.count = *count;
                    d.monster_id = (*monster_id > 0).then_some(*monster_id);
                    d.token_name = token_name.clone();
                    d.token_icon = token_icon.map(|i| i.to_string()).unwrap_or_default();
                    d
                }
                Objective::Fetch {
                    item_sn,
                    count,
                    consume,
                    ..
                } => {
                    let mut d = ObjectiveDraft::new(QuestType::Fetch);
                    d.count = *count;
                    if let Some((cat, id)) = decode_item_sn(*item_sn) {
                        d.item_category = cat;
                        d.item_id = Some(id);
                    }
                    d.consume = *consume;
                    d
                }
            })
            .collect();
        match &spec.kind {
            QuestKind::Hunt {
                monster_id,
                token_name,
                token_desc,
                token_icon,
                ..
            } => {
                self.quest_type = QuestType::Hunt;
                // 0 = monster couldn't be reconstructed; leave unselected.
                self.selected_monster = (*monster_id > 0).then_some(*monster_id);
                self.token_name = token_name.clone();
                self.token_desc = token_desc.clone();
                self.token_icon = token_icon.map(|i| i.to_string()).unwrap_or_default();
            }
            QuestKind::Fetch {
                item_sn, consume, ..
            } => {
                self.quest_type = QuestType::Fetch;
                if let Some((cat, id)) = decode_item_sn(*item_sn) {
                    self.fetch_item_category = cat;
                    self.fetch_item_id = Some(id);
                }
                self.fetch_consume = *consume;
            }
        }

        // Restore the quest-giver NPC from the manifest (so editing re-offers it).
        self.assign_giver = false;
        self.giver_npc = None;
        if let Some(root) = &self.root {
            if let Ok(m) = crate::manifest::read_manifest(root, spec.quest_sn) {
                if let Some(g) = m.givers.first() {
                    self.giver_npc = Some(g.npc_id);
                    self.assign_giver = true;
                }
            }
        }
    }

    // --- helpers ---

    /// A numbered "card" section: rose badge + title header, a divider, then
    /// the body — the main visual building block of the form.
    fn section(
        &mut self,
        ui: &mut egui::Ui,
        num: &str,
        title: &str,
        body: impl FnOnce(&mut Self, &mut egui::Ui),
    ) {
        egui::Frame::none()
            .fill(theme::BG_CARD)
            .stroke(egui::Stroke::new(1.0, theme::OUTLINE))
            .rounding(egui::Rounding::same(8.0))
            .inner_margin(egui::Margin::symmetric(12.0, 10.0))
            .show(ui, |ui| {
                ui.set_width(ui.available_width());
                ui.horizontal(|ui| {
                    egui::Frame::none()
                        .fill(theme::ACCENT_STRONG)
                        .rounding(egui::Rounding::same(10.0))
                        .inner_margin(egui::Margin::symmetric(8.0, 1.0))
                        .show(ui, |ui| {
                            ui.label(
                                egui::RichText::new(num)
                                    .strong()
                                    .color(egui::Color32::WHITE),
                            );
                        });
                    ui.label(egui::RichText::new(title).strong().size(15.0));
                });
                ui.add_space(2.0);
                ui.separator();
                ui.add_space(4.0);
                body(self, ui);
            });
        ui.add_space(10.0);
    }

    /// Name of the objective (monster for Hunt, item for Fetch).
    fn objective_name(&self) -> Option<String> {
        let ds = self.data.as_ref()?;
        match self.quest_type {
            QuestType::Hunt => ds
                .find_monster(self.selected_monster?)
                .map(|m| m.name.clone()),
            QuestType::Fetch => ds
                .item_db
                .lookup(self.fetch_item_category, self.fetch_item_id?)
                .map(|it| it.name.clone()),
        }
    }

    fn reward_item_sn(&self) -> Option<(i32, i16)> {
        if !self.reward_item_enabled {
            return None;
        }
        let id = self.reward_item_id?;
        Some((
            crate::data::encode_item_no(self.reward_item_category, id),
            self.reward_item_qty as i16,
        ))
    }

    fn reward_summary(&self) -> String {
        let mut parts = Vec::new();
        if self.reward_exp > 0 {
            parts.push(format!("{} exp", self.reward_exp));
        }
        if self.reward_zuly > 0 {
            parts.push(format!("{} zuly", self.reward_zuly));
        }
        if let Some((_, qty)) = self.reward_item_sn() {
            parts.push(format!("{qty}× item"));
        }
        if parts.is_empty() {
            " (none)".to_string()
        } else {
            format!(": {}", parts.join(", "))
        }
    }

    fn build_spec(&self) -> Option<QuestSpec> {
        let ds = self.data.as_ref()?;
        let kind = match self.quest_type {
            QuestType::Hunt => {
                let monster_id = self.selected_monster?;
                let free = ds
                    .find_monster(monster_id)
                    .map(|m| m.dead_event_is_free())
                    .unwrap_or(true);
                QuestKind::Hunt {
                    monster_id,
                    token_item_sn: ds.next_free_token_item_sn(),
                    token_name: if self.token_name.trim().is_empty() {
                        "Quest Token".to_string()
                    } else {
                        self.token_name.clone()
                    },
                    token_desc: self.token_desc.clone(),
                    token_icon: self.token_icon.trim().parse::<i32>().ok(),
                    chain_into_existing: !free,
                }
            }
            QuestType::Fetch => {
                let id = self.fetch_item_id?;
                QuestKind::Fetch {
                    item_sn: crate::data::encode_item_no(self.fetch_item_category, id),
                    item_name: ds
                        .item_db
                        .lookup(self.fetch_item_category, id)
                        .map(|it| it.name.clone())
                        .unwrap_or_default(),
                    consume: self.fetch_consume,
                }
            }
        };
        // Extra objectives. Each ready Hunt draft gets the next free token SN
        // (offset 1+ when the primary is also a hunt, since it took offset 0).
        let mut hunt_offset = if self.quest_type == QuestType::Hunt {
            1
        } else {
            0
        };
        let extra_objectives: Vec<Objective> = self
            .extra_objectives
            .iter()
            .filter(|d| d.is_ready())
            .filter_map(|d| match d.kind {
                QuestType::Hunt => {
                    let monster_id = d.monster_id?;
                    let free = ds
                        .find_monster(monster_id)
                        .map(|m| m.dead_event_is_free())
                        .unwrap_or(true);
                    let token_item_sn = ds.token_item_sn_at(hunt_offset);
                    hunt_offset += 1;
                    let name = ds
                        .find_monster(monster_id)
                        .map(|m| m.name.clone())
                        .unwrap_or_default();
                    Some(Objective::Hunt {
                        monster_id,
                        count: d.count,
                        token_item_sn,
                        token_name: if d.token_name.trim().is_empty() {
                            format!("{name} Mark")
                        } else {
                            d.token_name.clone()
                        },
                        token_desc: format!("Proof of a defeated {name}."),
                        token_icon: d.token_icon.trim().parse::<i32>().ok(),
                        chain_into_existing: !free,
                    })
                }
                QuestType::Fetch => {
                    let id = d.item_id?;
                    Some(Objective::Fetch {
                        item_sn: crate::data::encode_item_no(d.item_category, id),
                        item_name: ds
                            .item_db
                            .lookup(d.item_category, id)
                            .map(|it| it.name.clone())
                            .unwrap_or_default(),
                        count: d.count,
                        consume: d.consume,
                    })
                }
            })
            .collect();

        Some(QuestSpec {
            quest_sn: ds.next_free_quest_sn(),
            kind,
            count: self.kill_count,
            reward_exp: self.reward_exp,
            reward_zuly: self.reward_zuly,
            reward_item: self.reward_item_sn(),
            // Allocated at create time (scan) when `repeatable` is off; the preview
            // just needs to know it's one-time, which `!repeatable` conveys.
            one_time_switch: if self.repeatable { None } else { Some(-1) },
            extra_objectives,
            title: self.title.clone(),
            start_text: self.start_text.clone(),
            progress_text: self.progress_text.clone(),
            complete_text: self.complete_text.clone(),
        })
    }

    fn do_create(&mut self) {
        let Some(root) = self.root.clone() else {
            return;
        };
        // Editing = delete the old quest first (always a real write), then create
        // a fresh one. Reload data in between so the new SN / token id are correct.
        let editing = self.editing.take();
        let mut pre_changes = Vec::new();
        if let Some(old_sn) = editing {
            match delete_quest(&root, old_sn, false) {
                Ok(rep) => pre_changes = rep.changes,
                Err(e) => {
                    self.editing = Some(old_sn);
                    self.error = Some(format!("edit: could not remove old quest #{old_sn}: {e:#}"));
                    return;
                }
            }
            self.load_root(&root);
            self.view = View::Create;
        }

        let Some(mut spec) = self.build_spec() else {
            self.error = Some("pick a monster / item first".into());
            return;
        };
        // One-time: allocate a real free character switch (build_spec only marks it).
        if self.repeatable {
            spec.one_time_switch = None;
        } else {
            match crate::write::next_free_switch(&root) {
                Ok(s) => spec.one_time_switch = Some(s),
                Err(e) => {
                    self.error = Some(format!("one-time switch allocation failed: {e:#}"));
                    return;
                }
            }
        }
        let gen = generate(&spec);
        let dry = self.dry_run && editing.is_none();
        match apply_quest(&root, &spec, &gen, dry) {
            Ok(mut report) => {
                self.error = None;
                let mut changes = pre_changes;
                changes.append(&mut report.changes);

                // Optionally make an NPC give the quest (real write only).
                if !dry {
                    if let Some(npc_id) = self.giver_npc.filter(|_| self.assign_giver) {
                        let text = crate::write::GiverText {
                            greeting: spec.start_text.clone(),
                            in_progress: spec.progress_text.clone(),
                            after_complete: spec.complete_text.clone(),
                        };
                        match crate::write::wire_quest_giver(
                            &root,
                            npc_id,
                            spec.quest_sn,
                            &gen.complete_trigger,
                            Some(&text),
                            false,
                        ) {
                            Ok(rep) => changes.extend(rep.changes),
                            Err(e) => changes.push(format!("(quest-giver wiring FAILED: {e:#})")),
                        }
                    }
                }

                self.screen = Screen::Created {
                    summary: CreatedSummary {
                        quest_sn: spec.quest_sn,
                        quest_type: self.quest_type,
                        objective_name: self.objective_name().unwrap_or_default(),
                        count: spec.count,
                        complete_trigger: gen.complete_trigger.clone(),
                        register_trigger: gen.register_trigger.clone(),
                        changes,
                        backups: report.backups,
                        effective_dry: dry,
                        was_edit: editing.is_some(),
                    },
                };
            }
            Err(e) => self.error = Some(format!("{e:#}")),
        }
    }

    fn maybe_refresh_text(&mut self) {
        if !self.auto_text {
            return;
        }
        let obj_id = match self.quest_type {
            QuestType::Hunt => self.selected_monster,
            QuestType::Fetch => self.fetch_item_id,
        };
        let Some(obj_id) = obj_id else {
            return;
        };
        let basis = (self.quest_type, obj_id, self.kill_count);
        if self.text_basis == Some(basis) {
            return;
        }
        let name = self.objective_name().unwrap_or_else(|| "it".into());
        match self.quest_type {
            QuestType::Hunt => {
                self.title = format!("Hunt: {name}");
                self.start_text =
                    format!("Defeat {} {name} threatening the area.", self.kill_count);
                self.progress_text = format!("Keep hunting {name}.");
                self.complete_text = "Well done, adventurer!".to_string();
                self.token_name = format!("{name} Mark");
                self.token_desc = format!("Proof of a defeated {name}.");
            }
            QuestType::Fetch => {
                self.title = format!("Gather: {name}");
                self.start_text = format!("Bring {} {name}.", self.kill_count);
                self.progress_text = format!("Collect {} {name}.", self.kill_count);
                self.complete_text = "Thank you, adventurer!".to_string();
            }
        }
        self.text_basis = Some(basis);
    }
}

/// A reusable token-icon control: a live preview of the current icon, a number
/// field, and a togglable browsable grid over the item-icon atlas. Writes the
/// chosen icon number into `value`. Shared by the primary picker and every extra
/// hunt objective (so each token can have its own icon). `grid_id` must be unique
/// per control so the open grids don't share scroll/egui state.
fn icon_picker_control(
    icons: &mut Option<crate::icons::IconStore>,
    root: Option<&Path>,
    ui: &mut egui::Ui,
    label: &str,
    value: &mut String,
    open: &mut bool,
    grid_id: impl std::hash::Hash,
) {
    let current: Option<i32> = value.trim().parse().ok();
    ui.horizontal(|ui| {
        ui.label(label);
        if let Some(n) = current {
            if let Some(tex) = icons.as_mut().and_then(|s| s.icon_texture(ui.ctx(), n)) {
                ui.add(egui::Image::new(&tex).fit_to_exact_size(egui::vec2(32.0, 32.0)));
            }
        }
        ui.add(
            egui::TextEdit::singleline(value)
                .desired_width(56.0)
                .hint_text("default"),
        );
        if ui
            .button(if *open { "Close" } else { "🖼 Browse…" })
            .clicked()
        {
            *open = !*open;
            if *open && icons.is_none() {
                if let Some(r) = root {
                    *icons = Some(
                        crate::icons::IconStore::load(r)
                            .unwrap_or_else(|_| crate::icons::IconStore::empty(r)),
                    );
                }
            }
        }
    });

    if !*open {
        return;
    }
    let count = icons.as_ref().map_or(0, |s| s.icon_count());
    if count == 0 {
        ui.weak("(no icons — couldn't load 3DDATA/CONTROL/RES/ITEM1.TSI)");
        return;
    }

    let cols = 14usize;
    let cell = 34.0_f32;
    let rows = count.div_ceil(cols);
    let mut clicked: Option<i32> = None;
    egui::ScrollArea::vertical()
        .id_source(("icon_grid", grid_id))
        .max_height(280.0)
        .auto_shrink([false, false])
        .show_rows(ui, cell, rows, |ui, row_range| {
            for row in row_range {
                ui.horizontal(|ui| {
                    for c in 0..cols {
                        let idx = row * cols + c;
                        if idx >= count {
                            break;
                        }
                        let size = egui::vec2(cell - 4.0, cell - 4.0);
                        let tex = icons
                            .as_mut()
                            .and_then(|s| s.icon_texture(ui.ctx(), idx as i32));
                        let resp = match tex {
                            Some(t) => ui.add(egui::ImageButton::new(
                                egui::Image::new(&t).fit_to_exact_size(size),
                            )),
                            None => ui.add_sized(size, egui::Button::new("")),
                        };
                        if resp.on_hover_text(format!("icon {idx}")).clicked() {
                            clicked = Some(idx as i32);
                        }
                    }
                });
            }
        });

    if let Some(n) = clicked {
        *value = n.to_string();
        *open = false;
    }
}

fn monster_label(m: &Monster) -> String {
    let tag = if m.dead_event_is_free() {
        ""
    } else {
        "  [+chain]"
    };
    format!("{:>5}  {}  (Lv {}){}", m.id, m.name, m.level, tag)
}

fn item_label(it: &Item) -> String {
    format!("{:>4}  {}", it.id, it.name)
}

/// Decode a packed item SN (`type*1000+id`) into (category, id).
fn decode_item_sn(sn: i32) -> Option<(ItemCategory, i32)> {
    let (ty, id) = (sn / 1000, sn % 1000);
    ItemCategory::ALL
        .iter()
        .copied()
        .find(|c| *c as i32 == ty)
        .map(|c| (c, id))
}
