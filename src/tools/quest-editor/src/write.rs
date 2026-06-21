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
use crate::gen::{GeneratedQuest, QuestKind, QuestSpec};

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

/// Apply (or preview) all the data changes for a generated quest (Hunt or Fetch).
pub fn apply_quest(
    root: &Path,
    spec: &QuestSpec,
    gen: &GeneratedQuest,
    dry_run: bool,
) -> Result<WriteReport> {
    let stb_dir = resolve_stb_dir(root)?;
    let questdata_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("QUESTDATA");

    // --- common files (always changed) ---
    let quest_path = file_ci(&stb_dir, "LIST_QUEST.STB")?;
    let qdata_path = file_ci(&stb_dir, "LIST_QUESTDATA.STB")?;
    let stl_path = file_ci(&stb_dir, "LIST_QUEST_S.STL")?;
    let mut quest = load_stb(&quest_path)?;
    let mut qdata = load_stb(&qdata_path)?;
    let mut stl = load_stl(&stl_path)?;

    let qsd_file = questdata_dir.join(&gen.qsd_filename);
    let qsd_rel = format!("3DDATA\\QUESTDATA\\{}", gen.qsd_filename);
    let stl_key = format!("QE_{}", spec.quest_sn);

    // --- common validation ---
    if gen.quest_sn as usize != quest.data.len() {
        bail!(
            "quest SN {} != LIST_QUEST row count {} — data changed since generation; regenerate",
            gen.quest_sn,
            quest.data.len()
        );
    }
    if qsd_file.exists() {
        bail!("QSD file already exists: {}", qsd_file.display());
    }
    if stl.keys.iter().any(|k| k.name == stl_key) {
        bail!("STL key already exists: {stl_key}");
    }

    let mut changes = Vec::new();

    // 1) New QSD file.
    let qsd_bytes = gen.qsd.to_bytes();
    changes.push(format!(
        "CREATE {} ({} bytes, {} triggers)",
        qsd_file.display(),
        qsd_bytes.len(),
        gen.qsd.patterns.iter().map(|p| p.triggers.len()).sum::<usize>()
    ));

    // 2) LIST_QUESTDATA: append row registering the QSD.
    let mut qdata_row = empty_row(qdata.cols());
    set_cell(&mut qdata_row, 0, &format!("QX-{}", spec.quest_sn));
    set_cell(&mut qdata_row, QDATA_COL_PATH, &qsd_rel);
    qdata.data.push(qdata_row);
    changes.push(format!("APPEND LIST_QUESTDATA row -> \"{qsd_rel}\""));

    // 3) LIST_QUEST: append row at index == quest_sn (clone a real template).
    let mut quest_row = template_row(&quest, |r| {
        !cell(r, QUEST_COL_NAME).trim().is_empty()
            && cell(r, QUEST_COL_APPLICATION).trim() == "0"
            && cell_is_int(r, QUEST_COL_ICON)
    })
    .context("no individual-quest template row to clone")?;
    set_cell(&mut quest_row, 0, "");
    set_cell(&mut quest_row, QUEST_COL_NAME, &spec.title);
    set_cell(&mut quest_row, QUEST_COL_TIME, "0");
    set_cell(&mut quest_row, QUEST_COL_STL_LINK, &stl_key);
    quest.data.push(quest_row);
    changes.push(format!(
        "APPEND LIST_QUEST row {} (name \"{}\", STL \"{}\")",
        spec.quest_sn, spec.title, stl_key
    ));

    // 4) LIST_QUEST_S.STL: append key + a row per language.
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

    // --- Hunt-specific: token quest-item + (col-41 merge OR host-QSD chain) +
    //     token STL. Held as Options so the commit only writes what changed. ---
    let mut hunt_qitem: Option<(PathBuf, STB)> = None;
    let mut hunt_npc: Option<(PathBuf, STB)> = None;
    let mut hunt_qitem_stl: Option<(PathBuf, STL)> = None;
    let mut hunt_host_qsd: Option<(PathBuf, crate::qsd::QsdFile)> = None;

    if let QuestKind::Hunt {
        monster_id,
        token_item_sn,
        token_name,
        token_desc,
        token_icon,
        chain_into_existing,
    } = &spec.kind
    {
        let token_type = token_item_sn / 1000;
        let token_id = (token_item_sn % 1000) as usize;
        if token_type != QUEST_ITEM_TYPE {
            bail!(
                "token item SN {token_item_sn} is not a quest item (type {token_type} != {QUEST_ITEM_TYPE})"
            );
        }
        if token_id > 999 {
            bail!("token item id {token_id} exceeds 999 (type*1000+id encoding limit)");
        }

        let qitem_path = file_ci(&stb_dir, "LIST_QUESTITEM.STB")?;
        let npc_path = file_ci(&stb_dir, "LIST_NPC.STB")?;
        let qitem_stl_path = file_ci(&stb_dir, "LIST_QUESTITEM_S.STL")?;
        let mut qitem = load_stb(&qitem_path)?;
        let mut npc = load_stb(&npc_path)?;
        let mut qitem_stl = load_stl(&qitem_stl_path)?;
        let token_stl_key = format!("QITEM_{}", spec.quest_sn);

        // Hunt validation.
        if token_id >= qitem.data.len() {
            bail!(
                "token item id {token_id} is beyond LIST_QUESTITEM ({} rows); no free placeholder",
                qitem.data.len()
            );
        }
        if !cell(&qitem.data[token_id], QITEM_COL_NAME).is_empty() {
            bail!(
                "token item row {token_id} is already in use (\"{}\")",
                cell(&qitem.data[token_id], QITEM_COL_NAME)
            );
        }
        let kill_trigger = gen
            .kill_trigger
            .as_ref()
            .ok_or_else(|| anyhow!("hunt quest has no kill trigger"))?;
        let monster_row = npc
            .data
            .iter()
            .position(|r| r.first().and_then(|s| s.trim().parse::<i32>().ok()) == Some(*monster_id))
            .ok_or_else(|| anyhow!("monster {monster_id} not found in LIST_NPC"))?;
        let existing_de = cell(&npc.data[monster_row], NPC_COL_DEAD_EVENT)
            .trim()
            .to_string();
        if !chain_into_existing && !existing_de.is_empty() {
            bail!(
                "monster {monster_id} already has a dead-event trigger (\"{existing_de}\"); \
                 enable chaining to add to it (ownership rule)"
            );
        }
        if *chain_into_existing && existing_de.is_empty() {
            bail!("monster {monster_id} has no existing trigger to chain onto");
        }
        if qitem_stl.keys.iter().any(|k| k.name == token_stl_key) {
            bail!("quest-item STL key already exists: {token_stl_key}");
        }

        // 5) token row.
        let mut token_row = template_row(&qitem, |r| {
            !cell(r, QITEM_COL_NAME).trim().is_empty() && cell_is_int(r, QITEM_COL_TYPE)
        })
        .context("no quest-item template row to clone")?;
        set_cell(&mut token_row, 0, "");
        set_cell(&mut token_row, QITEM_COL_NAME, token_name);
        set_cell(&mut token_row, QITEM_COL_BELONGING_QUEST, &spec.quest_sn.to_string());
        set_cell(&mut token_row, QITEM_COL_STL_LINK, &token_stl_key);
        if let Some(icon) = token_icon {
            set_cell(&mut token_row, QITEM_COL_ICON, &icon.to_string());
        }
        qitem.data[token_id] = token_row;
        changes.push(format!(
            "SET LIST_QUESTITEM row {token_id} = token \"{token_name}\" (SN {token_item_sn})"
        ));

        // 6) Wire the kill trigger: claim col-41 (free monster) or splice into
        //    the monster's existing dead-event chain (chaining).
        if *chain_into_existing {
            let mut our_kill = gen
                .host_kill_trigger
                .clone()
                .ok_or_else(|| anyhow!("chaining requested but no kill trigger was generated"))?;
            let (host_path, mut host_qsd, pi, ti) = find_host_qsd(&questdata_dir, &existing_de)?;
            // Our trigger inherits the entry's original `check_next` (so the rest
            // of the chain is preserved), and the entry now chains to ours.
            our_kill.check_next = host_qsd.patterns[pi].triggers[ti].check_next;
            host_qsd.patterns[pi].triggers[ti].check_next = 1;
            host_qsd.patterns[pi].triggers.insert(ti + 1, our_kill);
            changes.push(format!(
                "CHAIN kill trigger \"{kill_trigger}\" into {} after \"{existing_de}\"",
                host_path.file_name().and_then(|s| s.to_str()).unwrap_or("?")
            ));
            hunt_host_qsd = Some((host_path, host_qsd));
        } else {
            set_cell(&mut npc.data[monster_row], NPC_COL_DEAD_EVENT, kill_trigger);
            changes.push(format!(
                "MERGE LIST_NPC row {monster_row} (npc {monster_id}) col-41 = \"{kill_trigger}\""
            ));
            hunt_npc = Some((npc_path, npc));
        }

        // 7) token STL.
        qitem_stl.keys.push(StringTableKey {
            id: *token_item_sn as u32,
            name: token_stl_key.clone(),
        });
        for lt in &mut qitem_stl.language_tables {
            lt.rows.push(StringTableRow::ItemRow(ItemRowData {
                text: token_name.clone(),
                description: token_desc.clone(),
            }));
        }
        changes.push(format!(
            "APPEND LIST_QUESTITEM_S.STL key \"{token_stl_key}\" -> \"{token_name}\""
        ));

        hunt_qitem = Some((qitem_path, qitem));
        hunt_qitem_stl = Some((qitem_stl_path, qitem_stl));
    }

    if dry_run {
        return Ok(WriteReport {
            dry_run: true,
            changes,
            backups: Vec::new(),
        });
    }

    // --- commit: back up then write only the files that changed ---
    let mut backups = Vec::new();
    let mut to_backup: Vec<&Path> = vec![&quest_path, &qdata_path, &stl_path];
    if let Some((p, _)) = &hunt_qitem {
        to_backup.push(p);
    }
    if let Some((p, _)) = &hunt_npc {
        to_backup.push(p);
    }
    if let Some((p, _)) = &hunt_qitem_stl {
        to_backup.push(p);
    }
    if let Some((p, _)) = &hunt_host_qsd {
        to_backup.push(p);
    }
    for p in to_backup {
        if let Some(b) = backup_once(p)? {
            backups.push(b);
        }
    }

    fs::create_dir_all(&questdata_dir).ok();
    fs::write(&qsd_file, &qsd_bytes)
        .with_context(|| format!("writing {}", qsd_file.display()))?;
    write_stb(&mut quest, &quest_path)?;
    write_stb(&mut qdata, &qdata_path)?;
    write_stl(&mut stl, &stl_path)?;
    if let Some((p, mut s)) = hunt_qitem {
        write_stb(&mut s, &p)?;
    }
    if let Some((p, mut s)) = hunt_npc {
        write_stb(&mut s, &p)?;
    }
    if let Some((p, mut s)) = hunt_qitem_stl {
        write_stl(&mut s, &p)?;
    }
    if let Some((p, host)) = hunt_host_qsd {
        host.write_file(&p)
            .with_context(|| format!("writing host QSD {}", p.display()))?;
    }

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

/// Find which `.QSD` under `questdata_dir` defines a trigger named `entry_name`,
/// returning (path, parsed file, pattern index, trigger index). Used to splice a
/// new kill trigger into a monster's existing dead-event chain.
fn find_host_qsd(
    questdata_dir: &Path,
    entry_name: &str,
) -> Result<(PathBuf, crate::qsd::QsdFile, usize, usize)> {
    let target = entry_name.trim().as_bytes();
    for entry in fs::read_dir(questdata_dir)
        .with_context(|| format!("reading {}", questdata_dir.display()))?
    {
        let path = entry?.path();
        let is_qsd = path
            .extension()
            .and_then(|e| e.to_str())
            .is_some_and(|e| e.eq_ignore_ascii_case("qsd"));
        if !is_qsd {
            continue;
        }
        let qsd = match crate::qsd::QsdFile::read_file(&path) {
            Ok(q) => q,
            Err(_) => continue, // skip unparseable files
        };
        for (pi, pat) in qsd.patterns.iter().enumerate() {
            for (ti, t) in pat.triggers.iter().enumerate() {
                let name = t.name.strip_suffix(b"\0").unwrap_or(&t.name);
                if name == target {
                    return Ok((path, qsd, pi, ti));
                }
            }
        }
    }
    bail!(
        "could not find the host trigger \"{entry_name}\" in any .QSD under {}",
        questdata_dir.display()
    )
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
