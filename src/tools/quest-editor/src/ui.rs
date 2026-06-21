//! Phase 5 — the friendly creation wizard (egui).
//!
//! Goal: a non-developer can create a working "kill N monsters" quest without
//! touching the CLI or knowing anything about QSD/STB internals. The flow is a
//! single scrollable form: pick a monster → set the count → set rewards → tweak
//! the text → Create. A few developer knobs live behind an "Advanced" section.

use std::path::{Path, PathBuf};

use eframe::egui;

use crate::data::{DataSet, Item, ItemCategory, Monster};
use crate::gen::{generate, QuestKind, QuestSpec};
use crate::write::apply_quest;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum QuestType {
    Hunt,
    Fetch,
}

const MAX_TOKEN_ITEM_ID: i32 = 999; // type*1000+id encoding limit (reward items)

pub fn run() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([900.0, 760.0])
            .with_min_inner_size([720.0, 560.0])
            .with_title("ROSE Quest Creator"),
        ..Default::default()
    };
    eframe::run_native(
        "ROSE Quest Creator",
        options,
        Box::new(|_cc| Box::new(QuestCreator::default())),
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

    // dev knobs
    hide_in_use: bool,
    token_icon: String, // optional icon-number override (blank = template's icon)
    dry_run: bool,

    screen: Screen,
    error: Option<String>,
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
            hide_in_use: false,
            token_icon: String::new(),
            dry_run: false,
            screen: Screen::Form,
            error: None,
        }
    }
}

impl eframe::App for QuestCreator {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::TopBottomPanel::top("top").show(ctx, |ui| {
            ui.add_space(4.0);
            ui.horizontal(|ui| {
                ui.heading("⚔  ROSE Quest Creator");
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if self.data.is_some() && ui.button("Change data folder…").clicked() {
                        self.pick_folder();
                    }
                });
            });
            if let Some(root) = &self.root {
                ui.label(
                    egui::RichText::new(format!("data: {}", root.display()))
                        .small()
                        .weak(),
                );
            }
            ui.add_space(2.0);
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            if self.data.is_none() {
                self.ui_pick_folder(ui);
                return;
            }
            match &self.screen {
                Screen::Form => {
                    egui::ScrollArea::vertical().show(ui, |ui| self.ui_form(ui));
                }
                Screen::Created { .. } => self.ui_created(ui),
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
            }
            Err(e) => {
                self.load_error = Some(format!("{e:#}"));
                self.data = None;
            }
        }
    }

    fn ui_pick_folder(&mut self, ui: &mut egui::Ui) {
        ui.add_space(60.0);
        ui.vertical_centered(|ui| {
            ui.heading("Welcome!");
            ui.add_space(8.0);
            ui.label("To start, pick your game data folder.");
            ui.label(
                egui::RichText::new("(the folder that contains 3DDATA — e.g. your repo's data\\)")
                    .weak(),
            );
            ui.add_space(16.0);
            if ui
                .add(egui::Button::new("📁  Select data folder…").min_size(egui::vec2(220.0, 36.0)))
                .clicked()
            {
                self.pick_folder();
            }
            if let Some(err) = &self.load_error {
                ui.add_space(16.0);
                ui.colored_label(egui::Color32::LIGHT_RED, err);
            }
        });
    }

    fn ui_form(&mut self, ui: &mut egui::Ui) {
        // Refresh auto-text if the monster / count changed.
        self.maybe_refresh_text();

        ui.add_space(6.0);

        // Quest type chooser.
        ui.horizontal(|ui| {
            ui.label("Quest type:");
            ui.selectable_value(&mut self.quest_type, QuestType::Hunt, "🗡 Hunt — kill monsters");
            ui.selectable_value(&mut self.quest_type, QuestType::Fetch, "📦 Fetch — bring items");
        });
        ui.add_space(4.0);

        // --- 1 (objective) and 2 (rewards) side by side ---
        let sec1_title = match self.quest_type {
            QuestType::Hunt => "Which monster do you hunt?",
            QuestType::Fetch => "Which item to bring?",
        };
        ui.columns(2, |cols| {
        self.section(&mut cols[0], "1", sec1_title, |app, ui| match app.quest_type {
            QuestType::Hunt => app.ui_monster_list(ui),
            QuestType::Fetch => app.ui_fetch_item(ui),
        });

        // --- 2. Objective + rewards (numbers laid out two-per-row) ---
        self.section(&mut cols[1], "2", "Objective & rewards", |app, ui| {
            egui::Grid::new("nums_grid")
                .num_columns(4)
                .spacing([16.0, 8.0])
                .show(ui, |ui| {
                    ui.label(match app.quest_type {
                        QuestType::Hunt => "Kill count:",
                        QuestType::Fetch => "Bring count:",
                    });
                    ui.add(egui::DragValue::new(&mut app.kill_count).clamp_range(1..=999));
                    ui.label("Experience:");
                    ui.add(egui::DragValue::new(&mut app.reward_exp).clamp_range(0..=100_000_000));
                    ui.end_row();

                    ui.label("Zuly (money):");
                    ui.add(egui::DragValue::new(&mut app.reward_zuly).clamp_range(0..=2_000_000_000));
                    ui.label("");
                    ui.label("");
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

        // --- 3. Text ---
        self.section(ui, "3", "Quest text", |app, ui| {
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

        // --- preview + create ---
        ui.add_space(8.0);
        self.ui_preview_and_create(ui);

        // --- advanced ---
        ui.add_space(8.0);
        egui::CollapsingHeader::new("⚙ Advanced (developer)")
            .default_open(false)
            .show(ui, |ui| {
                ui.checkbox(
                    &mut self.hide_in_use,
                    "Hide monsters that already have a quest hook (can't be used)",
                );
                ui.checkbox(
                    &mut self.dry_run,
                    "Dry run — preview the file changes but write nothing",
                );
                ui.horizontal(|ui| {
                    ui.label("Token icon # (blank = default):");
                    ui.add(egui::TextEdit::singleline(&mut self.token_icon).desired_width(80.0));
                });
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
        ui.label("Search by name or id:");
        ui.text_edit_singleline(&mut self.monster_search);

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

        egui::ScrollArea::vertical()
            .id_source("monster_list")
            .max_height(220.0)
            .auto_shrink([false, false])
            .show(ui, |ui| {
                for (id, label, free) in rows.iter().take(cap) {
                    let selected = self.selected_monster == Some(*id);
                    let mut text = egui::RichText::new(label);
                    if !free {
                        text = text.color(egui::Color32::from_rgb(200, 140, 60));
                    }
                    if ui.selectable_label(selected, text).clicked() {
                        self.selected_monster = Some(*id);
                        self.auto_text = true;
                    }
                }
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
                        egui::Color32::from_rgb(220, 120, 60),
                        format!(
                            "⚠ This monster already has a quest hook (\"{}\"). Pick another.",
                            m.dead_event
                        ),
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
        ui.text_edit_singleline(&mut self.fetch_item_search);
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

        ui.checkbox(&mut self.fetch_consume, "Take the items when the quest is turned in");
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

        ui.text_edit_singleline(&mut self.reward_item_search);
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

        ui.horizontal(|ui| {
            ui.label("Quantity:");
            ui.add(egui::DragValue::new(&mut self.reward_item_qty).clamp_range(1..=999));
        });
    }

    fn ui_preview_and_create(&mut self, ui: &mut egui::Ui) {
        let Some(spec) = self.build_spec() else {
            ui.colored_label(
                egui::Color32::GRAY,
                "Pick a monster / item above to see the preview.",
            );
            return;
        };
        let gen = crate::gen::generate(&spec);
        // Compute issues in a scope that drops the `self.data` borrow before the
        // Create button needs `&mut self`.
        let issues = self
            .data
            .as_ref()
            .map(|ds| crate::verify::verify(ds, &spec, &gen))
            .unwrap_or_default();

        let verb = match self.quest_type {
            QuestType::Hunt => "Kill",
            QuestType::Fetch => "Bring",
        };
        egui::Frame::group(ui.style()).show(ui, |ui| {
            ui.label(egui::RichText::new("Preview").strong());
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
            let files = match self.quest_type {
                QuestType::Hunt => "LIST_Quest / LIST_NPC / LIST_QUESTITEM / STL",
                QuestType::Fetch => "LIST_Quest / LIST_QuestDATA / STL",
            };
            ui.label(
                egui::RichText::new(format!(
                    "Writes a new {} and updates {files} (each backed up to .bak).",
                    gen.qsd_filename
                ))
                .weak()
                .small(),
            );
        });

        // Validation issues (errors block creation; warnings are advisory).
        for issue in &issues {
            let (color, icon) = match issue.level {
                crate::verify::Level::Error => (egui::Color32::from_rgb(235, 105, 105), "✖"),
                crate::verify::Level::Warning => (egui::Color32::from_rgb(220, 170, 60), "⚠"),
            };
            ui.colored_label(color, format!("{icon}  {}", issue.message));
        }

        ui.add_space(6.0);

        let can_create = !crate::verify::has_errors(&issues);
        ui.horizontal(|ui| {
            let label = if self.dry_run {
                "🔍  Preview changes (no write)"
            } else {
                "✅  Create Quest"
            };
            let btn = egui::Button::new(egui::RichText::new(label).size(16.0))
                .min_size(egui::vec2(200.0, 38.0));
            if ui.add_enabled(can_create, btn).clicked() {
                self.do_create();
            }
        });

        if let Some(err) = &self.error {
            ui.add_space(6.0);
            ui.colored_label(egui::Color32::LIGHT_RED, err);
        }
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
            ui.heading(if self.dry_run {
                "🔍 Dry run — nothing was written"
            } else {
                "✅ Quest created!"
            });
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
                    egui::RichText::new(format!("Backed up {} file(s) to .bak", summary.backups.len()))
                        .weak()
                        .small(),
                );
            }

            if !self.dry_run {
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
            if ui
                .add(egui::Button::new("➕  Create another quest").min_size(egui::vec2(200.0, 36.0)))
                .clicked()
            {
                // Reload data so the next quest gets fresh SN / token ids.
                if let Some(root) = self.root.clone() {
                    self.load_root(&root);
                }
                self.screen = Screen::Form;
                self.error = None;
            }
        });
    }

    // --- helpers ---

    /// A small numbered section wrapper for a consistent look.
    fn section(
        &mut self,
        ui: &mut egui::Ui,
        num: &str,
        title: &str,
        body: impl FnOnce(&mut Self, &mut egui::Ui),
    ) {
        egui::Frame::group(ui.style()).show(ui, |ui| {
            ui.horizontal(|ui| {
                ui.label(
                    egui::RichText::new(num)
                        .strong()
                        .color(egui::Color32::from_rgb(120, 170, 255)),
                );
                ui.label(egui::RichText::new(title).strong());
            });
            ui.add_space(4.0);
            body(self, ui);
        });
        ui.add_space(6.0);
    }

    /// Name of the objective (monster for Hunt, item for Fetch).
    fn objective_name(&self) -> Option<String> {
        let ds = self.data.as_ref()?;
        match self.quest_type {
            QuestType::Hunt => ds.find_monster(self.selected_monster?).map(|m| m.name.clone()),
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
            QuestType::Hunt => QuestKind::Hunt {
                monster_id: self.selected_monster?,
                token_item_sn: ds.next_free_token_item_sn(),
                token_name: if self.token_name.trim().is_empty() {
                    "Quest Token".to_string()
                } else {
                    self.token_name.clone()
                },
                token_desc: self.token_desc.clone(),
                token_icon: self.token_icon.trim().parse::<i32>().ok(),
            },
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
        Some(QuestSpec {
            quest_sn: ds.next_free_quest_sn(),
            kind,
            count: self.kill_count,
            reward_exp: self.reward_exp,
            reward_zuly: self.reward_zuly,
            reward_item: self.reward_item_sn(),
            title: self.title.clone(),
            start_text: self.start_text.clone(),
            progress_text: self.progress_text.clone(),
            complete_text: self.complete_text.clone(),
        })
    }

    fn do_create(&mut self) {
        let (Some(root), Some(spec)) = (self.root.clone(), self.build_spec()) else {
            return;
        };
        let gen = generate(&spec);
        match apply_quest(&root, &spec, &gen, self.dry_run) {
            Ok(report) => {
                self.error = None;
                self.screen = Screen::Created {
                    summary: CreatedSummary {
                        quest_sn: spec.quest_sn,
                        quest_type: self.quest_type,
                        objective_name: self.objective_name().unwrap_or_default(),
                        count: spec.count,
                        complete_trigger: gen.complete_trigger.clone(),
                        register_trigger: gen.register_trigger.clone(),
                        changes: report.changes,
                        backups: report.backups,
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
                self.start_text = format!("Defeat {} {name} threatening the area.", self.kill_count);
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

fn monster_label(m: &Monster) -> String {
    let tag = if m.dead_event_is_free() { "" } else { "  [in use]" };
    format!("{:>5}  {}  (Lv {}){}", m.id, m.name, m.level, tag)
}

fn item_label(it: &Item) -> String {
    format!("{:>4}  {}", it.id, it.name)
}
