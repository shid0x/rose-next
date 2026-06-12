use std::path::PathBuf;

use egui::{Color32, RichText, ScrollArea, Vec2};

use crate::data::{decode_item_no, encode_item_no, DataSet, ItemCategory, Npc};
use crate::icons::IconStore;

pub struct ShopEditorApp {
    data: Option<DataSet>,
    icons: IconStore,
    root: Option<PathBuf>,

    // UI state
    npc_search: String,
    item_search: String,
    selected_npc: Option<usize>,
    selected_tab: usize, // 0..=3
    item_filter_category: Option<ItemCategory>,
    status: String,
    load_error: Option<String>,
    cow_notice_for_tab: Option<usize>, // tab row we warned about
}

impl ShopEditorApp {
    pub fn new(_cc: &eframe::CreationContext<'_>, root: Option<PathBuf>) -> Self {
        let mut app = Self {
            data: None,
            icons: IconStore::empty(std::path::Path::new(".")),
            root: None,
            npc_search: String::new(),
            item_search: String::new(),
            selected_npc: None,
            selected_tab: 0,
            item_filter_category: None,
            status: String::new(),
            load_error: None,
            cow_notice_for_tab: None,
        };
        if let Some(r) = root {
            app.load_root(r);
        }
        app
    }

    fn load_root(&mut self, root: PathBuf) {
        self.root = Some(root.clone());
        match DataSet::load(&root) {
            Ok(ds) => {
                self.data = Some(ds);
                self.icons = IconStore::load(&root).unwrap_or_else(|e| {
                    log::warn!("icon load failed: {}", e);
                    self.status = format!("Icons unavailable: {}", e);
                    IconStore::empty(&root)
                });
                self.status = format!("Loaded {}", root.display());
                self.load_error = None;
            }
            Err(e) => {
                self.load_error = Some(format!("Failed to load: {}", e));
                self.data = None;
            }
        }
    }
}

impl eframe::App for ShopEditorApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        top_bar(self, ctx);

        if self.data.is_none() {
            egui::CentralPanel::default().show(ctx, |ui| {
                ui.vertical_centered(|ui| {
                    ui.add_space(60.0);
                    ui.heading("ROSE NPC Shop Editor");
                    ui.add_space(20.0);
                    if let Some(err) = &self.load_error {
                        ui.colored_label(Color32::LIGHT_RED, err);
                        ui.add_space(10.0);
                    }
                    if ui.button("Pick extracted VFS folder…").clicked() {
                        if let Some(p) = rfd::FileDialog::new()
                            .set_title("Select extracted VFS root")
                            .pick_folder()
                        {
                            self.load_root(p);
                        }
                    }
                });
            });
            return;
        }

        egui::SidePanel::left("sidebar")
            .resizable(true)
            .default_width(260.0)
            .show(ctx, |ui| sidebar_ui(self, ui));

        egui::SidePanel::right("item_browser")
            .resizable(true)
            .default_width(340.0)
            .show(ctx, |ui| item_browser_ui(self, ctx, ui));

        egui::CentralPanel::default().show(ctx, |ui| center_ui(self, ctx, ui));
    }
}

fn top_bar(app: &mut ShopEditorApp, ctx: &egui::Context) {
    egui::TopBottomPanel::top("top").show(ctx, |ui| {
        ui.horizontal(|ui| {
            ui.heading("ROSE NPC Shop Editor");
            ui.separator();
            if let Some(r) = &app.root {
                ui.label(
                    RichText::new(format!("Root: {}", r.display()))
                        .color(Color32::LIGHT_BLUE),
                );
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let save_enabled = app.data.is_some();
                if ui
                    .add_enabled(save_enabled, egui::Button::new("Save"))
                    .clicked()
                {
                    if let Some(data) = app.data.as_mut() {
                        match data.save() {
                            Ok(()) => app.status = "Saved.".to_string(),
                            Err(e) => app.status = format!("Save failed: {}", e),
                        }
                    }
                }
                if !app.status.is_empty() {
                    ui.label(RichText::new(&app.status).color(Color32::LIGHT_GREEN));
                }
            });
        });
    });
}

fn sidebar_ui(app: &mut ShopEditorApp, ui: &mut egui::Ui) {
    let data = app.data.as_ref().unwrap();
    ui.label(RichText::new("Shopkeepers").strong());
    ui.text_edit_singleline(&mut app.npc_search);
    ui.separator();

    let filter = app.npc_search.to_lowercase();

    // id -> index lookup so zone entries (which store NPC ids) can select.
    let id_to_idx: std::collections::HashMap<i32, usize> = data
        .npcs
        .iter()
        .enumerate()
        .filter(|(_, n)| n.has_shop())
        .map(|(i, n)| (n.id, i))
        .collect();
    let mut shopkeeper_ids: Vec<i32> = id_to_idx.keys().copied().collect();
    shopkeeper_ids.sort_unstable();

    let mut click: Option<usize> = None;

    ScrollArea::vertical()
        .auto_shrink([false, false])
        .show(ui, |ui| {
            egui::CollapsingHeader::new("All shopkeepers")
                .default_open(true)
                .show(ui, |ui| {
                    for (idx, npc) in data.npcs.iter().enumerate() {
                        if !npc.has_shop() {
                            continue;
                        }
                        if !filter.is_empty()
                            && !npc.name.to_lowercase().contains(&filter)
                            && !npc.id.to_string().contains(&filter)
                        {
                            continue;
                        }
                        let selected = app.selected_npc == Some(idx);
                        let label = format!("[{}] {}", npc.id, npc.name);
                        if ui.selectable_label(selected, label).clicked() {
                            click = Some(idx);
                        }
                    }
                });

            ui.add_space(4.0);

            egui::CollapsingHeader::new("Zones")
                .default_open(false)
                .show(ui, |ui| {
                    let grouped =
                        crate::zones::group_npcs_by_zone(&data.zones, &shopkeeper_ids);
                    for zone in &data.zones {
                        let Some(ids) = grouped.get(&Some(zone.id)) else {
                            continue;
                        };
                        let header =
                            format!("{} ({})", zone.name, ids.len());
                        egui::CollapsingHeader::new(header)
                            .id_source(("zone", zone.id))
                            .show(ui, |ui| {
                                for id in ids {
                                    let Some(&idx) = id_to_idx.get(id) else {
                                        continue;
                                    };
                                    let npc = &data.npcs[idx];
                                    if !filter.is_empty()
                                        && !npc.name.to_lowercase().contains(&filter)
                                        && !npc.id.to_string().contains(&filter)
                                    {
                                        continue;
                                    }
                                    let selected = app.selected_npc == Some(idx);
                                    let label = format!("[{}] {}", npc.id, npc.name);
                                    if ui.selectable_label(selected, label).clicked() {
                                        click = Some(idx);
                                    }
                                }
                            });
                    }
                    if let Some(leftover) = grouped.get(&None) {
                        egui::CollapsingHeader::new(format!(
                            "(Unzoned) ({})",
                            leftover.len()
                        ))
                        .id_source("zone_none")
                        .show(ui, |ui| {
                            for id in leftover {
                                let Some(&idx) = id_to_idx.get(id) else {
                                    continue;
                                };
                                let npc = &data.npcs[idx];
                                if !filter.is_empty()
                                    && !npc.name.to_lowercase().contains(&filter)
                                    && !npc.id.to_string().contains(&filter)
                                {
                                    continue;
                                }
                                let selected = app.selected_npc == Some(idx);
                                let label = format!("[{}] {}", npc.id, npc.name);
                                if ui.selectable_label(selected, label).clicked() {
                                    click = Some(idx);
                                }
                            }
                        });
                    }
                });
        });

    if let Some(idx) = click {
        app.selected_npc = Some(idx);
        let npc = &app.data.as_ref().unwrap().npcs[idx];
        app.selected_tab = first_valid_tab(npc);
        app.cow_notice_for_tab = None;
    }
}

fn first_valid_tab(npc: &Npc) -> usize {
    npc.shop_tab_rows
        .iter()
        .position(|r| *r > 0)
        .unwrap_or(0)
}

fn center_ui(app: &mut ShopEditorApp, ctx: &egui::Context, ui: &mut egui::Ui) {
    let Some(npc_idx) = app.selected_npc else {
        ui.vertical_centered(|ui| {
            ui.add_space(80.0);
            ui.label("Select an NPC from the left to edit their shop.");
        });
        return;
    };
    let data = app.data.as_ref().unwrap();
    let npc = data.npcs[npc_idx].clone();
    ui.horizontal(|ui| {
        ui.heading(format!("[{}] {}", npc.id, npc.name));
    });
    ui.separator();

    // Tab strip
    ui.horizontal(|ui| {
        for (i, row) in npc.shop_tab_rows.iter().enumerate() {
            let enabled = *row > 0;
            let label = if enabled {
                let data = app.data.as_mut().unwrap();
                let tab = data.get_or_load_tab(*row as usize);
                tab.map(|t| {
                    if t.name.is_empty() {
                        format!("Tab {} (row {})", i + 1, row)
                    } else {
                        format!("{}", t.name)
                    }
                })
                .unwrap_or_else(|| format!("Tab {}", i + 1))
            } else {
                format!("(empty)")
            };
            let selected = app.selected_tab == i;
            let resp = ui.add_enabled(
                enabled,
                egui::SelectableLabel::new(selected, label),
            );
            if resp.clicked() && enabled {
                app.selected_tab = i;
                app.cow_notice_for_tab = None;
            }
        }
    });
    ui.separator();

    // Current tab details
    let current_row = npc.shop_tab_rows[app.selected_tab];
    if current_row <= 0 {
        ui.label("This tab slot has no shop assigned.");
        return;
    }
    let current_row_usize = current_row as usize;
    let ref_count = app.data.as_ref().unwrap().ref_count(current_row_usize);

    ui.horizontal(|ui| {
        ui.label(format!("LIST_SELL row: {}", current_row_usize));
        ui.separator();
        if ref_count > 1 {
            ui.colored_label(
                Color32::YELLOW,
                format!(
                    "⚠ Shared with {} other NPC(s) — first edit will copy this tab.",
                    ref_count - 1
                ),
            );
        } else {
            ui.colored_label(Color32::LIGHT_GREEN, "Exclusive to this NPC");
        }
    });

    // Tab name editor
    {
        let data = app.data.as_mut().unwrap();
        if let Some(tab) = data.get_or_load_tab(current_row_usize) {
            ui.horizontal(|ui| {
                ui.label("Tab name:");
                if ui.text_edit_singleline(&mut tab.name).changed() {
                    tab.dirty = true;
                }
            });
        }
    }
    ui.separator();

    // Items table
    let remove_at: Option<usize>;
    {
        let data = app.data.as_ref().unwrap();
        let tab = data.shop_tabs.get(&current_row_usize);
        let items: Vec<(usize, i32)> = tab
            .map(|t| {
                t.items
                    .iter()
                    .enumerate()
                    .map(|(i, v)| (i, *v))
                    .collect()
            })
            .unwrap_or_default();

        let mut remove: Option<usize> = None;
        ScrollArea::vertical()
            .id_source("shop_items")
            .auto_shrink([false, false])
            .show(ui, |ui| {
                egui::Grid::new("items_grid")
                    .num_columns(5)
                    .striped(true)
                    .spacing([8.0, 4.0])
                    .show(ui, |ui| {
                        ui.label(RichText::new("Slot").strong());
                        ui.label(RichText::new("Icon").strong());
                        ui.label(RichText::new("Item").strong());
                        ui.label(RichText::new("Category").strong());
                        ui.label("");
                        ui.end_row();

                        for (slot, full) in &items {
                            ui.label(format!("{}", slot + 1));
                            if *full == 0 {
                                ui.label("—");
                                ui.label(RichText::new("(empty)").weak());
                                ui.label("");
                                ui.label("");
                                ui.end_row();
                                continue;
                            }
                            if let Some((cat, id)) = decode_item_no(*full) {
                                let item = data.item_db.lookup(cat, id);
                                let icon_no =
                                    item.map(|i| i.icon_no).unwrap_or(0);
                                draw_icon(ctx, &mut app.icons, ui, icon_no);
                                ui.label(
                                    item.map(|i| i.name.clone())
                                        .unwrap_or_else(|| format!("#{}", id)),
                                );
                                ui.label(cat.display());
                                if ui.small_button("Remove").clicked() {
                                    remove = Some(*slot);
                                }
                                ui.end_row();
                            } else {
                                ui.label("—");
                                ui.label(format!("raw {}", full));
                                ui.label("?");
                                if ui.small_button("Remove").clicked() {
                                    remove = Some(*slot);
                                }
                                ui.end_row();
                            }
                        }
                    });
            });
        remove_at = remove;
    }

    if let Some(slot) = remove_at {
        apply_tab_mutation(app, npc_idx, app.selected_tab, |items| {
            if slot < items.len() {
                items[slot] = 0;
            }
        });
    }
}

fn item_browser_ui(app: &mut ShopEditorApp, ctx: &egui::Context, ui: &mut egui::Ui) {
    ui.label(RichText::new("Item Browser").strong());
    ui.horizontal(|ui| {
        egui::ComboBox::from_id_source("cat_filter")
            .selected_text(
                app.item_filter_category
                    .map(|c| c.display())
                    .unwrap_or("All Types"),
            )
            .show_ui(ui, |ui| {
                ui.selectable_value(&mut app.item_filter_category, None, "All Types");
                for cat in ItemCategory::ALL {
                    ui.selectable_value(
                        &mut app.item_filter_category,
                        Some(*cat),
                        cat.display(),
                    );
                }
            });
    });
    ui.text_edit_singleline(&mut app.item_search);
    ui.separator();

    let Some(npc_idx) = app.selected_npc else {
        ui.label("Select an NPC first.");
        return;
    };
    let selected_tab = app.selected_tab;
    let npc_has_tab = app
        .data
        .as_ref()
        .map(|d| d.npcs[npc_idx].shop_tab_rows[selected_tab] > 0)
        .unwrap_or(false);
    if !npc_has_tab {
        ui.label("Selected tab slot is empty — no target to add into.");
        return;
    }

    let filter = app.item_search.to_lowercase();
    let cat_filter = app.item_filter_category;

    // Collect matches into a buffer so we don't borrow `data` across the add action.
    let matches: Vec<(ItemCategory, i32, String, i32)> = {
        let data = app.data.as_ref().unwrap();
        let mut v: Vec<(ItemCategory, i32, String, i32)> = data
            .item_db
            .all()
            .filter(|it| cat_filter.map(|c| c == it.category).unwrap_or(true))
            .filter(|it| {
                filter.is_empty()
                    || it.name.to_lowercase().contains(&filter)
                    || it.id.to_string().contains(&filter)
            })
            .map(|it| (it.category, it.id, it.name.clone(), it.icon_no))
            .collect();
        // HashMap iteration is unordered; sort by (category, id) so the list
        // is stable across runs and easy to scan.
        v.sort_by(|a, b| (a.0 as i32, a.1).cmp(&(b.0 as i32, b.1)));
        v
    };

    ui.label(
        RichText::new(format!("{} match(es)", matches.len()))
            .weak()
            .small(),
    );

    let mut add_target: Option<(ItemCategory, i32)> = None;
    ScrollArea::vertical()
        .id_source("item_browser")
        .auto_shrink([false, false])
        .show(ui, |ui| {
            for (cat, id, name, icon_no) in &matches {
                ui.horizontal(|ui| {
                    draw_icon(ctx, &mut app.icons, ui, *icon_no);
                    ui.vertical(|ui| {
                        ui.label(name);
                        ui.label(
                            RichText::new(format!("{} #{}", cat.display(), id))
                                .weak()
                                .small(),
                        );
                    });
                    if ui.small_button("Add").clicked() {
                        add_target = Some((*cat, *id));
                    }
                });
                ui.separator();
            }
        });

    if let Some((cat, id)) = add_target {
        let full = encode_item_no(cat, id);
        apply_tab_mutation(app, npc_idx, selected_tab, |items| {
            if let Some(empty) = items.iter().position(|v| *v == 0) {
                items[empty] = full;
            }
        });
    }
}

fn apply_tab_mutation(
    app: &mut ShopEditorApp,
    npc_idx: usize,
    tab_slot: usize,
    mutate: impl FnOnce(&mut Vec<i32>),
) {
    let data = app.data.as_mut().unwrap();
    let new_row = match data.begin_tab_edit(npc_idx, tab_slot) {
        Ok(r) => r,
        Err(e) => {
            app.status = format!("edit failed: {}", e);
            return;
        }
    };
    if let Some(tab) = data.get_or_load_tab(new_row) {
        mutate(&mut tab.items);
        tab.dirty = true;
    }
    app.status = "Modified.".to_string();
}

fn draw_icon(
    ctx: &egui::Context,
    icons: &mut IconStore,
    ui: &mut egui::Ui,
    icon_no: i32,
) {
    let size = Vec2::splat(32.0);
    if let Some(tex) = icons.icon_texture(ctx, icon_no) {
        ui.add(egui::Image::new(&tex).fit_to_exact_size(size));
    } else {
        let (rect, _) = ui.allocate_exact_size(size, egui::Sense::hover());
        ui.painter()
            .rect_filled(rect, 2.0, Color32::from_gray(40));
    }
}
