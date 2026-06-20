// The friendly quest-creation wizard. Built as its own GUI binary so the
// `quest-editor` CLI stays a plain console app. windows_subsystem="windows"
// suppresses the console window in release builds.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() -> eframe::Result<()> {
    quest_editor::ui::run()
}
