#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod app;
mod data;
mod dds;
mod icons;
mod zones;

use std::path::PathBuf;

fn main() -> eframe::Result<()> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1280.0, 760.0])
            .with_min_inner_size([960.0, 600.0])
            .with_title("ROSE NPC Shop Editor"),
        ..Default::default()
    };

    eframe::run_native(
        "ROSE NPC Shop Editor",
        options,
        Box::new(|cc| Box::new(app::ShopEditorApp::new(cc, pick_initial_root()))),
    )
}

fn pick_initial_root() -> Option<PathBuf> {
    rfd::FileDialog::new()
        .set_title("Point to your extracted VFS folder (must contain 3DDATA/STB/LIST_NPC.STB)")
        .pick_folder()
}
