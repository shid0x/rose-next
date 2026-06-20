//! Phase 4 — append-only / merge-only writers.
//!
//! Turns a [`gen::GeneratedQuest`] + [`gen::HuntQuestSpec`] into actual game-data
//! edits. **Safety model** (see PROGRESS.md):
//! - new `.QSD` file (never touches existing ones);
//! - **append** rows to LIST_Quest / LIST_QuestDATA / LIST_QUESTITEM;
//! - **append** an STL key + row to LIST_QUEST_S.STL;
//! - **merge** the monster's LIST_NPC col-41 (refuse if already occupied);
//! - every existing file backed up to `.bak` before the first overwrite;
//! - a dry-run reports every change and writes nothing.
//!
//! All edits are validated first; if any guard fails we bail before writing a
//! single byte.

use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{anyhow, bail, Context, Result};
use roselib::files::stl::{ItemRowData, QuestRowData, StringTableKey, StringTableRow};
use roselib::files::{STB, STL};
use roselib::io::RoseFile;

use crate::data::resolve_stb_dir;
use crate::gen::{GeneratedQuest, HuntQuestSpec};

// roselib column indices (game col + 1; root column is index 0).
const QUEST_COL_NAME: usize = 1;
const QUEST_COL_TIME: usize = 2;
const QUEST_COL_APPLICATION: usize = 3; // 0 = individual quest
const QUEST_COL_ICON: usize = 4;
const QUEST_COL_STL_LINK: usize = 5;

const QITEM_COL_TYPE: usize = 5;

const QITEM_COL_NAME: usize = 1;
const QITEM_COL_ICON: usize = 10; // game col 9 = "Icon Number"
const QITEM_COL_BELONGING_QUEST: usize = 32;
const QITEM_COL_STL_LINK: usize = 33;

const QDATA_COL_PATH: usize = 1; // col 0 is a label

const NPC_COL_DEAD_EVENT: usize = 42; // game col 41

const QUEST_ITEM_TYPE: i32 = 13;

/// Result of an apply (or dry-run): a human-readable change list and the files
/// that were (or would be) backed up.
pub struct WriteReport {
    pub dry_run: bool,
    pub changes: Vec<String>,
    pub backups: Vec<PathBuf>,
}

impl WriteReport {
    pub fn print(&self) {
        println!(
            "{}",
            if self.dry_run {
                "=== DRY RUN — no files written ==="
            } else {
                "=== APPLIED ==="
            }
        );
        for c in &self.changes {
            println!("  {c}");
        }
        if !self.backups.is_empty() {
            println!("backups:");
            for b in &self.backups {
                println!("  {}", b.display());
            }
        }
    }
}

/// Apply (or preview) all the data changes for a generated Hunt quest.
pub fn apply_hunt_quest(
    root: &Path,
    spec: &HuntQuestSpec,
    gen: &GeneratedQuest,
    dry_run: bool,
) -> Result<WriteReport> {
    let stb_dir = resolve_stb_dir(root)?;
    let questdata_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("QUESTDATA");

    // --- load everything fresh (we mutate + write these) ---
    let quest_path = file_ci(&stb_dir, "LIST_QUEST.STB")?;
    let qdata_path = file_ci(&stb_dir, "LIST_QUESTDATA.STB")?;
    let qitem_path = file_ci(&stb_dir, "LIST_QUESTITEM.STB")?;
    let npc_path = file_ci(&stb_dir, "LIST_NPC.STB")?;
    let stl_path = file_ci(&stb_dir, "LIST_QUEST_S.STL")?;
    let qitem_stl_path = file_ci(&stb_dir, "LIST_QUESTITEM_S.STL")?;

    let mut quest = load_stb(&quest_path)?;
    let mut qdata = load_stb(&qdata_path)?;
    let mut qitem = load_stb(&qitem_path)?;
    let mut npc = load_stb(&npc_path)?;
    let mut stl = load_stl(&stl_path)?;
    let mut qitem_stl = load_stl(&qitem_stl_path)?;

    let qsd_file = questdata_dir.join(&gen.qsd_filename);
    let qsd_rel = format!("3DDATA\\QUESTDATA\\{}", gen.qsd_filename);
    let stl_key = format!("QEST_HUNT_{}", spec.quest_sn);
    let token_stl_key = format!("QITEM_HUNT_{}", spec.quest_sn);

    // --- token item id/type from the SN (type*1000+id; id must be <= 999) ---
    let token_type = spec.token_item_sn / 1000;
    let token_id = (spec.token_item_sn % 1000) as usize;
    if token_type != QUEST_ITEM_TYPE {
        bail!(
            "token item SN {} is not a quest item (type {} != {})",
            spec.token_item_sn,
            token_type,
            QUEST_ITEM_TYPE
        );
    }
    if token_id > 999 {
        bail!("token item id {token_id} exceeds 999 (type*1000+id encoding limit)");
    }

    // ----------------------------------------------------------------------
    // Validate every guard BEFORE mutating anything.
    // ----------------------------------------------------------------------

    // Quest SN must be the append index.
    if gen.quest_sn as usize != quest.data.len() {
        bail!(
            "quest SN {} != LIST_QUEST row count {} — data changed since generation; regenerate",
            gen.quest_sn,
            quest.data.len()
        );
    }
    // Token row must exist as an empty placeholder (id <= 999, < row count).
    if token_id >= qitem.data.len() {
        bail!(
            "token item id {token_id} is beyond LIST_QUESTITEM ({} rows); no free placeholder \
             row <= 999 — cannot allocate a token without breaking the type*1000+id encoding",
            qitem.data.len()
        );
    }
    if !cell(&qitem.data[token_id], QITEM_COL_NAME).is_empty() {
        bail!(
            "token item row {token_id} is already in use (\"{}\")",
            cell(&qitem.data[token_id], QITEM_COL_NAME)
        );
    }
    // Monster col-41 must be free (the ownership rule).
    let (monster_id, kill_trigger) = &gen.npc_col41_assignment;
    let monster_row = npc
        .data
        .iter()
        .position(|r| r.first().and_then(|s| s.trim().parse::<i32>().ok()) == Some(*monster_id))
        .ok_or_else(|| anyhow!("monster {monster_id} not found in LIST_NPC"))?;
    let existing_de = cell(&npc.data[monster_row], NPC_COL_DEAD_EVENT);
    if !existing_de.trim().is_empty() {
        bail!(
            "monster {monster_id} already has a dead-event trigger (\"{existing_de}\"); refusing \
             to overwrite (ownership rule)"
        );
    }
    // QSD file must not already exist.
    if qsd_file.exists() {
        bail!("QSD file already exists: {}", qsd_file.display());
    }
    // STL keys must be unique.
    if stl.keys.iter().any(|k| k.name == stl_key) {
        bail!("STL key already exists: {stl_key}");
    }
    if qitem_stl.keys.iter().any(|k| k.name == token_stl_key) {
        bail!("quest-item STL key already exists: {token_stl_key}");
    }

    let mut changes = Vec::new();

    // ----------------------------------------------------------------------
    // 1) New QSD file.
    // ----------------------------------------------------------------------
    let qsd_bytes = gen.qsd.to_bytes();
    changes.push(format!(
        "CREATE {} ({} bytes, {} triggers)",
        qsd_file.display(),
        qsd_bytes.len(),
        gen.qsd.patterns.iter().map(|p| p.triggers.len()).sum::<usize>()
    ));

    // ----------------------------------------------------------------------
    // 2) LIST_QUESTDATA: append row registering the QSD.
    // ----------------------------------------------------------------------
    let mut qdata_row = empty_row(qdata.cols());
    set_cell(&mut qdata_row, 0, &format!("QX-{}", spec.quest_sn));
    set_cell(&mut qdata_row, QDATA_COL_PATH, &qsd_rel);
    qdata.data.push(qdata_row);
    changes.push(format!("APPEND LIST_QUESTDATA row -> \"{qsd_rel}\""));

    // ----------------------------------------------------------------------
    // 3) LIST_QUEST: append row at index == quest_sn (clone a template).
    // ----------------------------------------------------------------------
    // Template: a real individual quest (Application==0) with a numeric icon —
    // skips the schema/description row whose cells are comment strings.
    let mut quest_row = template_row(&quest, |r| {
        !cell(r, QUEST_COL_NAME).trim().is_empty()
            && cell(r, QUEST_COL_APPLICATION).trim() == "0"
            && cell_is_int(r, QUEST_COL_ICON)
    })
    .context("no individual-quest template row to clone")?;
    set_cell(&mut quest_row, 0, ""); // root stays empty (SN == row index)
    set_cell(&mut quest_row, QUEST_COL_NAME, &spec.title);
    set_cell(&mut quest_row, QUEST_COL_TIME, "0"); // no time limit
    set_cell(&mut quest_row, QUEST_COL_STL_LINK, &stl_key);
    quest.data.push(quest_row);
    changes.push(format!(
        "APPEND LIST_QUEST row {} (name \"{}\", STL \"{}\")",
        spec.quest_sn, spec.title, stl_key
    ));

    // ----------------------------------------------------------------------
    // 4) LIST_QUESTITEM: fill the placeholder token row (clone a template).
    // ----------------------------------------------------------------------
    let mut token_row = template_row(&qitem, |r| {
        !cell(r, QITEM_COL_NAME).trim().is_empty() && cell_is_int(r, QITEM_COL_TYPE)
    })
    .context("no quest-item template row to clone")?;
    set_cell(&mut token_row, 0, "");
    set_cell(&mut token_row, QITEM_COL_NAME, &spec.token_name); // server-side name
    set_cell(&mut token_row, QITEM_COL_BELONGING_QUEST, &spec.quest_sn.to_string());
    set_cell(&mut token_row, QITEM_COL_STL_LINK, &token_stl_key); // client name via STL
    if let Some(icon) = spec.token_icon {
        set_cell(&mut token_row, QITEM_COL_ICON, &icon.to_string());
    }
    qitem.data[token_id] = token_row;
    changes.push(format!(
        "SET LIST_QUESTITEM row {token_id} = token \"{}\" (SN {}, belongs to quest {})",
        spec.token_name, spec.token_item_sn, spec.quest_sn
    ));

    // ----------------------------------------------------------------------
    // 5) LIST_NPC: merge col-41 dead-event on the monster.
    // ----------------------------------------------------------------------
    set_cell(&mut npc.data[monster_row], NPC_COL_DEAD_EVENT, kill_trigger);
    changes.push(format!(
        "MERGE LIST_NPC row {monster_row} (npc {monster_id}) col-41 dead-event = \"{kill_trigger}\""
    ));

    // ----------------------------------------------------------------------
    // 6) LIST_QUEST_S.STL: append key + a row per language.
    // ----------------------------------------------------------------------
    stl.keys.push(StringTableKey {
        id: spec.quest_sn as u32,
        name: stl_key.clone(),
    });
    for lt in &mut stl.language_tables {
        lt.rows.push(StringTableRow::QuestRow(QuestRowData {
            text: spec.title.clone(),
            description: spec.progress_text.clone(),
            start_message: spec.start_text.clone(),
            end_message: spec.complete_text.clone(),
        }));
    }
    changes.push(format!(
        "APPEND LIST_QUEST_S.STL key \"{stl_key}\" (+1 row in {} language table(s))",
        stl.language_tables.len()
    ));

    // ----------------------------------------------------------------------
    // 7) LIST_QUESTITEM_S.STL: append key + row so the token has a real name.
    // ----------------------------------------------------------------------
    qitem_stl.keys.push(StringTableKey {
        id: spec.token_item_sn as u32,
        name: token_stl_key.clone(),
    });
    for lt in &mut qitem_stl.language_tables {
        lt.rows.push(StringTableRow::ItemRow(ItemRowData {
            text: spec.token_name.clone(),
            description: spec.token_desc.clone(),
        }));
    }
    changes.push(format!(
        "APPEND LIST_QUESTITEM_S.STL key \"{token_stl_key}\" -> \"{}\"",
        spec.token_name
    ));

    if dry_run {
        return Ok(WriteReport {
            dry_run: true,
            changes,
            backups: Vec::new(),
        });
    }

    // ----------------------------------------------------------------------
    // Commit: back up existing files, then write. The new QSD has no backup.
    // ----------------------------------------------------------------------
    let mut backups = Vec::new();
    for p in [
        &quest_path,
        &qdata_path,
        &qitem_path,
        &npc_path,
        &stl_path,
        &qitem_stl_path,
    ] {
        if let Some(b) = backup_once(p)? {
            backups.push(b);
        }
    }

    fs::create_dir_all(&questdata_dir).ok();
    fs::write(&qsd_file, &qsd_bytes)
        .with_context(|| format!("writing {}", qsd_file.display()))?;

    write_stb(&mut quest, &quest_path)?;
    write_stb(&mut qdata, &qdata_path)?;
    write_stb(&mut qitem, &qitem_path)?;
    write_stb(&mut npc, &npc_path)?;
    write_stl(&mut stl, &stl_path)?;
    write_stl(&mut qitem_stl, &qitem_stl_path)?;

    Ok(WriteReport {
        dry_run: false,
        changes,
        backups,
    })
}

// --- helpers ---------------------------------------------------------------

fn cell(row: &[String], i: usize) -> &str {
    row.get(i).map(String::as_str).unwrap_or("")
}

fn set_cell(row: &mut Vec<String>, i: usize, v: &str) {
    if row.len() <= i {
        row.resize(i + 1, String::new());
    }
    row[i] = v.to_string();
}

fn empty_row(cols: usize) -> Vec<String> {
    vec![String::new(); cols.max(1)]
}

/// Clone the first *genuine data* row matching `pred` — a known-good template
/// with the right column count and sane defaults. The predicate must reject the
/// schema/description row (row 0 of many STBs), whose non-key cells hold column
/// descriptions like "(0: Individual / 1: Party)" instead of real values.
fn template_row<F: Fn(&[String]) -> bool>(stb: &STB, pred: F) -> Option<Vec<String>> {
    stb.data.iter().find(|r| pred(r)).cloned()
}

fn cell_is_int(row: &[String], i: usize) -> bool {
    cell(row, i).trim().parse::<i32>().is_ok()
}

fn file_ci(dir: &Path, name: &str) -> Result<PathBuf> {
    let exact = dir.join(name);
    if exact.exists() {
        return Ok(exact);
    }
    if let Ok(rd) = fs::read_dir(dir) {
        for entry in rd.flatten() {
            if entry.file_name().to_string_lossy().eq_ignore_ascii_case(name) {
                return Ok(entry.path());
            }
        }
    }
    Err(anyhow!("file not found: {}/{}", dir.display(), name))
}

fn load_stb(path: &Path) -> Result<STB> {
    STB::from_path(path).map_err(|e| anyhow!("reading {}: {e}", path.display()))
}
fn load_stl(path: &Path) -> Result<STL> {
    STL::from_path(path).map_err(|e| anyhow!("reading {}: {e}", path.display()))
}
fn write_stb(stb: &mut STB, path: &Path) -> Result<()> {
    stb.write_to_path(path)
        .map_err(|e| anyhow!("writing {}: {e}", path.display()))
}
fn write_stl(stl: &mut STL, path: &Path) -> Result<()> {
    stl.write_to_path(path)
        .map_err(|e| anyhow!("writing {}: {e}", path.display()))
}

/// Copy `path` to `path.bak` once (never overwrites an existing backup).
fn backup_once(path: &Path) -> Result<Option<PathBuf>> {
    let mut ext = path
        .extension()
        .map(|e| e.to_string_lossy().to_string())
        .unwrap_or_default();
    ext.push_str(".bak");
    let bak = path.with_extension(ext);
    if bak.exists() || !path.exists() {
        return Ok(None);
    }
    fs::copy(path, &bak)
        .with_context(|| format!("backing up {} -> {}", path.display(), bak.display()))?;
    Ok(Some(bak))
}
