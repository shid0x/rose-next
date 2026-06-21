//! Sidecar manifests — `<questdata>/_quest-editor/QX-<sn>.qe.json`.
//!
//! When the editor creates a quest it also drops a small JSON manifest holding
//! the original [`QuestSpec`]. The game never reads these (they live in a `_`
//! subfolder of QUESTDATA); they exist so the wizard can list its own quests and
//! pre-fill the form for **edit**. Delete works without them (it reconstructs
//! from the data), but removes them when present.

use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};

use crate::data::resolve_stb_dir;
use crate::gen::QuestSpec;

/// On-disk manifest. Versioned so we can evolve the schema later.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Manifest {
    pub version: u32,
    pub spec: QuestSpec,
}

impl Manifest {
    pub const VERSION: u32 = 1;

    pub fn new(spec: QuestSpec) -> Self {
        Manifest {
            version: Self::VERSION,
            spec,
        }
    }
}

/// `<root>/.../QUESTDATA/_quest-editor`.
pub fn manifest_dir(root: &Path) -> Result<PathBuf> {
    let stb_dir = resolve_stb_dir(root)?;
    let parent = stb_dir
        .parent()
        .context("STB dir has no parent")?
        .join("QUESTDATA")
        .join("_quest-editor");
    Ok(parent)
}

pub fn manifest_path(root: &Path, quest_sn: i32) -> Result<PathBuf> {
    Ok(manifest_dir(root)?.join(format!("QX-{quest_sn}.qe.json")))
}

/// Write (or overwrite) the manifest for a quest.
pub fn write_manifest(root: &Path, spec: &QuestSpec) -> Result<PathBuf> {
    let dir = manifest_dir(root)?;
    fs::create_dir_all(&dir).with_context(|| format!("creating {}", dir.display()))?;
    let path = dir.join(format!("QX-{}.qe.json", spec.quest_sn));
    let json = serde_json::to_string_pretty(&Manifest::new(spec.clone()))?;
    fs::write(&path, json).with_context(|| format!("writing {}", path.display()))?;
    Ok(path)
}

pub fn read_manifest(root: &Path, quest_sn: i32) -> Result<Manifest> {
    let path = manifest_path(root, quest_sn)?;
    let json = fs::read_to_string(&path).with_context(|| format!("reading {}", path.display()))?;
    Ok(serde_json::from_str(&json)?)
}

/// All manifests found, sorted by quest SN. Unreadable files are skipped.
pub fn list_manifests(root: &Path) -> Vec<Manifest> {
    let mut out = Vec::new();
    let Ok(dir) = manifest_dir(root) else {
        return out;
    };
    let Ok(rd) = fs::read_dir(&dir) else {
        return out;
    };
    for entry in rd.flatten() {
        let path = entry.path();
        let is_manifest = path
            .file_name()
            .and_then(|n| n.to_str())
            .is_some_and(|n| n.ends_with(".qe.json"));
        if !is_manifest {
            continue;
        }
        if let Ok(json) = fs::read_to_string(&path) {
            if let Ok(m) = serde_json::from_str::<Manifest>(&json) {
                out.push(m);
            }
        }
    }
    out.sort_by_key(|m| m.spec.quest_sn);
    out
}
