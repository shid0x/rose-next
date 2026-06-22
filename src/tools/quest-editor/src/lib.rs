//! ROSE Next quest editor — library crate.
//!
//! Phase 1 exposes only the QSD structural codec. Higher layers (typed
//! condition/reward views, the template generator, STB/STL writers, and the
//! egui UI) are added in later phases — see `PROGRESS.md`.

pub mod convo;
pub mod data;
pub mod gen;
pub mod ifo;
pub mod ltb;
pub mod manifest;
pub mod qsd;
pub mod ui;
pub mod verify;
pub mod write;
