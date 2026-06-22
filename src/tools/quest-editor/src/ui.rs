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
use crate::write::{apply_quest, delete_quest, list_editor_quests};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum QuestType {
    Hunt,
    Fetch,
}

/// Top-level view: make a new quest, or manage the ones the editor made.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum View {
    Create,
    Manage,
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
    /// Lazily-scanned placement info: (placed npc ids, npc id -> existing .CON).
    placements: Option<(std::collections::HashSet<i32>, std::collections::HashMap<i32, String>)>,

    // dev knobs
    hide_in_use: bool,
    token_icon: String, // optional icon-number override (blank = template's icon)
    dry_run: bool,

    // create vs manage; when `editing` is set the form is editing that quest SN
    // (saving deletes the old one and creates a fresh one).
    view: View,
    editing: Option<i32>,
    manage_status: Option<String>,
    confirm_delete: Option<i32>,

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
            assign_giver: false,
            giver_search: String::new(),
            giver_npc: None,
            placements: None,
            hide_in_use: false,
            token_icon: String::new(),
            dry_run: false,
            view: View::Create,
            editing: None,
            manage_status: None,
            confirm_delete: None,
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
            if let Screen::Created { .. } = self.screen {
                self.ui_created(ui);
                return;
            }
            // Create / Manage tabs (only on the form screen).
            ui.horizontal(|ui| {
                if ui
                    .selectable_label(self.view == View::Create, "➕  Create")
                    .clicked()
                {
                    self.view = View::Create;
                }
                if ui
                    .selectable_label(self.view == View::Manage, "🗂  Manage")
                    .clicked()
                {
                    self.view = View::Manage;
                    self.manage_status = None;
                }
            });
            ui.separator();
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
                self.placements = None; // re-scan IFOs lazily for the new folder
            }
            Err(e) => {
                self.load_error = Some(format!("{e:#}"));
                self.data = None;
            }
        }
    }

    /// Scan all zone IFOs once (slow) to learn which NPCs are placed and which
    /// already have a conversation. Cached until the folder changes.
    fn ensure_placements(&mut self) {
        if self.placements.is_some() {
            return;
        }
        let Some(root) = self.root.clone() else {
            return;
        };
        let placed = crate::ifo::all_placed_npc_ids(&root)
            .unwrap_or_default()
            .into_iter()
            .collect();
        let with_dialog = crate::ifo::placements_with_conversation(&root)
            .unwrap_or_default()
            .into_iter()
            .collect();
        self.placements = Some((placed, with_dialog));
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

        // Editing banner.
        if let Some(sn) = self.editing {
            egui::Frame::group(ui.style())
                .fill(egui::Color32::from_rgb(40, 44, 60))
                .show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(
                            egui::RichText::new(format!(
                                "✏ Editing quest #{sn} — saving deletes it and creates an updated copy."
                            ))
                            .color(egui::Color32::from_rgb(150, 190, 255)),
                        );
                        if ui.button("Cancel edit").clicked() {
                            self.editing = None;
                            self.view = View::Manage;
                        }
                    });
                });
            ui.add_space(4.0);
        }

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

        // --- 4. Quest-giver NPC (optional) ---
        self.section(ui, "4", "Quest-giver NPC (optional)", |app, ui| {
            app.ui_giver(ui);
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
                    "Hide monsters that already have a quest (the [+chain] ones)",
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
                        egui::Color32::from_rgb(120, 170, 255),
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
            let files = match &spec.kind {
                QuestKind::Hunt { chain_into_existing: true, .. } => {
                    "LIST_Quest / LIST_QUESTITEM / STL + the monster's existing quest file"
                }
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
            let label = if self.editing.is_some() {
                "💾  Save changes"
            } else if self.dry_run {
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
            ui.heading(if summary.effective_dry {
                "🔍 Dry run — nothing was written"
            } else if summary.was_edit {
                "✅ Quest updated!"
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
            ui.colored_label(egui::Color32::from_rgb(120, 190, 120), status);
            ui.add_space(4.0);
        }

        let quests = match list_editor_quests(&root) {
            Ok(q) => q,
            Err(e) => {
                ui.colored_label(egui::Color32::LIGHT_RED, format!("{e:#}"));
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
                        Some(spec) => match &spec.kind {
                            QuestKind::Hunt { monster_id, .. } => (
                                "Hunt".to_string(),
                                format!("kill {}× {}", spec.count, self.monster_name(*monster_id)),
                            ),
                            QuestKind::Fetch { item_name, .. } => {
                                ("Fetch".to_string(), format!("bring {}× {}", spec.count, item_name))
                            }
                        },
                        None => ("—".to_string(), "(older quest — delete only)".to_string()),
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
                                .button(egui::RichText::new("Confirm").color(egui::Color32::WHITE))
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
                    self.manage_status =
                        Some(format!("Deleted quest #{sn} ({} change(s)).", rep.changes.len()));
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
                egui::RichText::new(
                    "Off: the quest is accepted/turned in via GM commands only.",
                )
                .weak()
                .small(),
            );
            return;
        }

        self.ensure_placements();
        ui.horizontal(|ui| {
            ui.label("Search NPC:");
            ui.text_edit_singleline(&mut self.giver_search);
        });

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
        egui::ScrollArea::vertical().max_height(150.0).show(ui, |ui| {
            for (id, name, conv) in &candidates {
                let label = if conv.is_some() {
                    format!("{:>5}  {}   [has dialog]", id, name)
                } else {
                    format!("{:>5}  {}", id, name)
                };
                ui.selectable_value(&mut self.giver_npc, Some(*id), label);
            }
        });

        match self.giver_npc.and_then(|id| candidates.iter().find(|(c, ..)| *c == id)) {
            Some((id, name, conv)) => {
                ui.label(egui::RichText::new(format!("Giver: {name} (npc {id})")).strong());
                if let Some(conv) = conv {
                    ui.colored_label(
                        egui::Color32::from_rgb(220, 150, 60),
                        format!(
                            "⚠ This NPC already has a conversation (\"{conv}\") — it will be \
                             REPLACED. Pick an NPC without [has dialog] unless you don't mind losing it.",
                        ),
                    );
                }
            }
            None => {
                ui.colored_label(
                    egui::Color32::from_rgb(120, 170, 255),
                    "Pick a placed NPC above to give the quest.",
                );
            }
        }
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

        let Some(spec) = self.build_spec() else {
            self.error = Some("pick a monster / item first".into());
            return;
        };
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
    let tag = if m.dead_event_is_free() { "" } else { "  [+chain]" };
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
