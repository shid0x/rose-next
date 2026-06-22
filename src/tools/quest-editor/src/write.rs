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

use std::collections::BTreeSet;
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

const EVENT_COL_FILE: usize = 4; // game col 3 = the .CON filename (EVENT_FILENAME)

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
        changes.push(format!("WRITE manifest QX-{}.qe.json", spec.quest_sn));
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

    // Sidecar manifest so the wizard can later list / edit / delete this quest.
    match crate::manifest::write_manifest(root, spec) {
        Ok(p) => changes.push(format!("WRITE manifest {}", p.display())),
        Err(e) => eprintln!("warning: could not write quest manifest: {e:#}"),
    }

    Ok(WriteReport {
        dry_run: false,
        changes,
        backups,
    })
}

/// Delete an editor-created quest (`QX-<sn>.QSD`). Reconstructs what to undo from
/// the data + naming conventions, so it works with or without a manifest (the
/// manifest is removed too when present). Refuses to touch a quest the editor
/// didn't create.
pub fn delete_quest(root: &Path, quest_sn: i32, dry_run: bool) -> Result<WriteReport> {
    let stb_dir = resolve_stb_dir(root)?;
    let questdata_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("QUESTDATA");

    let quest_path = file_ci(&stb_dir, "LIST_QUEST.STB")?;
    let qdata_path = file_ci(&stb_dir, "LIST_QUESTDATA.STB")?;
    let mut quest = load_stb(&quest_path)?;
    let mut qdata = load_stb(&qdata_path)?;

    let qsd_name = format!("QX-{quest_sn}.QSD");
    let qsd_file = questdata_dir.join(&qsd_name);
    let kill_trigger = format!("{quest_sn}-2");

    // Guard: only editor-created quests (a LIST_QUESTDATA row points at our QSD,
    // or the file exists).
    let qdata_row_idx = qdata.data.iter().position(|r| {
        cell(r, QDATA_COL_PATH)
            .rsplit(['\\', '/'])
            .next()
            .is_some_and(|f| f.eq_ignore_ascii_case(&qsd_name))
    });
    if qdata_row_idx.is_none() && !qsd_file.exists() {
        bail!("quest {quest_sn} doesn't look editor-created (no {qsd_name}); refusing to delete");
    }

    let mut changes = Vec::new();

    // 1) Blank the LIST_QUEST row (kept in place — removing would shift later SNs).
    if (quest_sn as usize) < quest.data.len() {
        quest.data[quest_sn as usize] = empty_row(quest.cols());
        changes.push(format!("BLANK LIST_QUEST row {quest_sn}"));
    }

    // 2) Remove the LIST_QUESTDATA row (a load list — index doesn't matter).
    if let Some(i) = qdata_row_idx {
        qdata.data.remove(i);
        changes.push(format!("REMOVE LIST_QUESTDATA row -> {qsd_name}"));
    }

    // 3) Hunt cleanup: token row (reclaims the id) + monster un-wiring.
    let qitem_path = file_ci(&stb_dir, "LIST_QUESTITEM.STB")?;
    let mut qitem = load_stb(&qitem_path)?;
    let mut qitem_changed = false;
    if let Some(t) = qitem.data.iter().position(|r| {
        cell(r, QITEM_COL_BELONGING_QUEST).trim().parse::<i32>().ok() == Some(quest_sn)
    }) {
        qitem.data[t] = empty_row(qitem.cols());
        qitem_changed = true;
        changes.push(format!("BLANK LIST_QUESTITEM row {t} (token)"));
    }

    // Un-wire the monster: either we claimed its col-41, or we chained into a host.
    let npc_path = file_ci(&stb_dir, "LIST_NPC.STB")?;
    let mut npc = load_stb(&npc_path)?;
    let mut npc_changed = false;
    let mut host_qsd_edit: Option<(PathBuf, crate::qsd::QsdFile)> = None;
    if let Some(mrow) = npc
        .data
        .iter()
        .position(|r| cell(r, NPC_COL_DEAD_EVENT).trim() == kill_trigger.as_str())
    {
        // Claimed: the kill trigger lived in our own (about-to-be-removed) QSD.
        set_cell(&mut npc.data[mrow], NPC_COL_DEAD_EVENT, "");
        npc_changed = true;
        changes.push(format!("CLEAR LIST_NPC row {mrow} col-41 (was \"{kill_trigger}\")"));
    } else if let Ok((host_path, mut host, pi, ti)) = find_host_qsd(&questdata_dir, &kill_trigger) {
        // Chained: remove our trigger and hand its `check_next` back to the
        // predecessor (the exact inverse of the insertion in apply_quest).
        if ti > 0 {
            let cn = host.patterns[pi].triggers[ti].check_next;
            host.patterns[pi].triggers[ti - 1].check_next = cn;
        }
        host.patterns[pi].triggers.remove(ti);
        changes.push(format!(
            "UNCHAIN \"{kill_trigger}\" from {}",
            host_path.file_name().and_then(|s| s.to_str()).unwrap_or("?")
        ));
        host_qsd_edit = Some((host_path, host));
    }

    let manifest = crate::manifest::manifest_path(root, quest_sn)
        .ok()
        .filter(|p| p.exists());

    if dry_run {
        if qsd_file.exists() {
            changes.push(format!("DELETE {}", qsd_file.display()));
        }
        if let Some(m) = &manifest {
            changes.push(format!("DELETE {}", m.display()));
        }
        return Ok(WriteReport {
            dry_run: true,
            changes,
            backups: Vec::new(),
        });
    }

    // --- commit: back up everything touched, then write / remove ---
    let mut backups = Vec::new();
    let mut to_backup: Vec<PathBuf> = vec![quest_path.clone(), qdata_path.clone()];
    if qitem_changed {
        to_backup.push(qitem_path.clone());
    }
    if npc_changed {
        to_backup.push(npc_path.clone());
    }
    if let Some((p, _)) = &host_qsd_edit {
        to_backup.push(p.clone());
    }
    if qsd_file.exists() {
        to_backup.push(qsd_file.clone());
    }
    for p in &to_backup {
        if let Some(b) = backup_once(p)? {
            backups.push(b);
        }
    }

    write_stb(&mut quest, &quest_path)?;
    write_stb(&mut qdata, &qdata_path)?;
    if qitem_changed {
        write_stb(&mut qitem, &qitem_path)?;
    }
    if npc_changed {
        write_stb(&mut npc, &npc_path)?;
    }
    if let Some((p, host)) = host_qsd_edit {
        host.write_file(&p)
            .with_context(|| format!("writing host QSD {}", p.display()))?;
    }
    if qsd_file.exists() {
        fs::remove_file(&qsd_file).with_context(|| format!("removing {}", qsd_file.display()))?;
        changes.push(format!("DELETE {}", qsd_file.display()));
    }
    if let Some(m) = manifest {
        fs::remove_file(&m).ok();
        changes.push(format!("DELETE {}", m.display()));
    }

    Ok(WriteReport {
        dry_run: false,
        changes,
        backups,
    })
}

/// A quest the editor created, for the Manage list. `spec` is present only when a
/// sidecar manifest exists (required to pre-fill the edit form); without it the
/// quest can still be deleted.
pub struct EditorQuest {
    pub quest_sn: i32,
    pub title: String,
    pub spec: Option<crate::gen::QuestSpec>,
}

/// `QX-<sn>.QSD` (any case) -> sn.
fn editor_quest_sn(path_cell: &str) -> Option<i32> {
    let file = path_cell.rsplit(['\\', '/']).next()?;
    let upper = file.to_ascii_uppercase();
    upper
        .strip_prefix("QX-")?
        .strip_suffix(".QSD")?
        .parse::<i32>()
        .ok()
}

/// Every editor-created quest found in the data: the `QX-<sn>.QSD` rows of
/// LIST_QUESTDATA, plus any orphaned manifests. Sorted by SN.
pub fn list_editor_quests(root: &Path) -> Result<Vec<EditorQuest>> {
    let stb_dir = resolve_stb_dir(root)?;
    let quest = load_stb(&file_ci(&stb_dir, "LIST_QUEST.STB")?)?;
    let qdata = load_stb(&file_ci(&stb_dir, "LIST_QUESTDATA.STB")?)?;

    let mut sns: BTreeSet<i32> = BTreeSet::new();
    for r in &qdata.data {
        if let Some(sn) = editor_quest_sn(cell(r, QDATA_COL_PATH)) {
            sns.insert(sn);
        }
    }
    for m in crate::manifest::list_manifests(root) {
        sns.insert(m.spec.quest_sn);
    }

    Ok(sns
        .into_iter()
        .map(|sn| {
            let title = quest
                .data
                .get(sn as usize)
                .map(|r| cell(r, QUEST_COL_NAME).trim().to_string())
                .filter(|s| !s.is_empty())
                .unwrap_or_else(|| format!("Quest #{sn}"));
            // Prefer the saved manifest; otherwise reconstruct the spec from the
            // generated data so older (pre-manifest) quests are still editable.
            let spec = crate::manifest::read_manifest(root, sn)
                .ok()
                .map(|m| m.spec)
                .or_else(|| reconstruct_spec(root, sn).ok());
            EditorQuest {
                quest_sn: sn,
                title,
                spec,
            }
        })
        .collect())
}

fn rd_u8(p: &[u8], o: usize) -> u8 {
    p.get(o).copied().unwrap_or(0)
}
fn rd_i16(p: &[u8], o: usize) -> i16 {
    p.get(o..o + 2).map_or(0, |b| i16::from_le_bytes([b[0], b[1]]))
}
fn rd_i32(p: &[u8], o: usize) -> i32 {
    p.get(o..o + 4)
        .map_or(0, |b| i32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}

/// Rebuild a [`QuestSpec`] from the data the editor generated for `quest_sn`
/// (used when there's no manifest). Parses the quest's own `QX-<sn>.QSD` (the
/// complete trigger carries the count + rewards), the token quest-item row, the
/// monster wiring, and the STL text. Best-effort: text/monster may be imperfect,
/// but the structural fields come straight from the data.
pub fn reconstruct_spec(root: &Path, quest_sn: i32) -> Result<crate::gen::QuestSpec> {
    use crate::gen::{QuestKind, QuestSpec};

    let stb_dir = resolve_stb_dir(root)?;
    let questdata_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("QUESTDATA");
    let qsd_file = questdata_dir.join(format!("QX-{quest_sn}.QSD"));
    let qsd = crate::qsd::QsdFile::read_file(&qsd_file)
        .map_err(|e| anyhow!("reading {}: {e}", qsd_file.display()))?;

    // The complete trigger is the one that Finishes the quest (REWD_000 op 0).
    let complete = qsd
        .patterns
        .iter()
        .flat_map(|p| p.triggers.iter())
        .find(|t| t.rewards.iter().any(|e| e.etype == 0 && rd_u8(&e.payload, 4) == 0))
        .ok_or_else(|| anyhow!("no complete trigger in {}", qsd_file.display()))?;

    // Count + the checked item (token for Hunt, the item for Fetch) from COND_004.
    let cond4 = complete
        .conditions
        .iter()
        .find(|e| e.etype == 4)
        .ok_or_else(|| anyhow!("no item-count condition in complete trigger"))?;
    let checked_item_sn = rd_i32(&cond4.payload, 4);
    let count = rd_i32(&cond4.payload, 12);

    // Rewards (REWD_005 exp/zuly, REWD_001 give = reward item / op-0 = consume).
    let mut reward_exp = 0;
    let mut reward_zuly = 0;
    let mut reward_item = None;
    let mut consume = false;
    for e in &complete.rewards {
        match e.etype {
            5 => match rd_u8(&e.payload, 0) {
                0 => reward_exp = rd_i32(&e.payload, 4),
                1 => reward_zuly = rd_i32(&e.payload, 4),
                _ => {}
            },
            1 => match rd_u8(&e.payload, 4) {
                1 => reward_item = Some((rd_i32(&e.payload, 0), rd_i16(&e.payload, 6))),
                0 => consume = true,
                _ => {}
            },
            _ => {}
        }
    }

    // Hunt iff a token quest-item belongs to this quest.
    let qitem = load_stb(&file_ci(&stb_dir, "LIST_QUESTITEM.STB")?)?;
    let token_row = qitem.data.iter().find(|r| {
        cell(r, QITEM_COL_BELONGING_QUEST).trim().parse::<i32>().ok() == Some(quest_sn)
    });

    let kind = if let Some(trow) = token_row {
        QuestKind::Hunt {
            monster_id: monster_for_kill_trigger(&stb_dir, &questdata_dir, quest_sn).unwrap_or(0),
            token_item_sn: checked_item_sn,
            token_name: cell(trow, QITEM_COL_NAME).to_string(),
            token_desc: stl_item_desc(&stb_dir, "LIST_QUESTITEM_S.STL", &format!("QITEM_{quest_sn}"))
                .unwrap_or_default(),
            token_icon: cell(trow, QITEM_COL_ICON).trim().parse::<i32>().ok(),
            chain_into_existing: false, // recomputed from the monster on save
        }
    } else {
        QuestKind::Fetch {
            item_sn: checked_item_sn,
            item_name: String::new(), // the UI re-derives this from the item id
            consume,
        }
    };

    let (title, start_text, progress_text, complete_text) =
        stl_quest_texts(&stb_dir, quest_sn).unwrap_or_else(|_| {
            (format!("Quest #{quest_sn}"), String::new(), String::new(), String::new())
        });

    Ok(QuestSpec {
        quest_sn,
        kind,
        count,
        reward_exp,
        reward_zuly,
        reward_item,
        title,
        start_text,
        progress_text,
        complete_text,
    })
}

/// The monster whose dead event fires `<sn>-2`: either it claims col-41 directly,
/// or our trigger was spliced into its chain (walk back to the chain head).
fn monster_for_kill_trigger(stb_dir: &Path, questdata_dir: &Path, quest_sn: i32) -> Option<i32> {
    let kill = format!("{quest_sn}-2");
    let npc = load_stb(&file_ci(stb_dir, "LIST_NPC.STB").ok()?).ok()?;
    let id_of = |r: &Vec<String>| r.first().and_then(|s| s.trim().parse::<i32>().ok());

    if let Some(r) = npc.data.iter().find(|r| cell(r, NPC_COL_DEAD_EVENT).trim() == kill) {
        return id_of(r);
    }
    // Chained: find our trigger, walk back over the check_next run to the head.
    let (_, host, pi, ti) = find_host_qsd(questdata_dir, &kill).ok()?;
    let triggers = &host.patterns[pi].triggers;
    let mut j = ti;
    while j > 0 && triggers[j - 1].check_next != 0 {
        j -= 1;
    }
    let head = triggers[j].name.strip_suffix(b"\0").unwrap_or(&triggers[j].name);
    npc.data
        .iter()
        .find(|r| cell(r, NPC_COL_DEAD_EVENT).trim().as_bytes() == head)
        .and_then(id_of)
}

/// Read the 4 quest texts (title / start / progress / complete) from LIST_QUEST_S
/// for `QE_<sn>`. Keys and rows are parallel (the writer appends them together).
fn stl_quest_texts(stb_dir: &Path, quest_sn: i32) -> Result<(String, String, String, String)> {
    let stl = load_stl(&file_ci(stb_dir, "LIST_QUEST_S.STL")?)?;
    let key = format!("QE_{quest_sn}");
    let idx = stl
        .keys
        .iter()
        .position(|k| k.name == key)
        .ok_or_else(|| anyhow!("no STL key {key}"))?;
    let lt = stl.language_tables.first().ok_or_else(|| anyhow!("no language table"))?;
    match lt.rows.get(idx) {
        Some(StringTableRow::QuestRow(q)) => Ok((
            q.text.clone(),
            q.start_message.clone(),
            q.description.clone(),
            q.end_message.clone(),
        )),
        _ => bail!("STL row for {key} is not a quest row"),
    }
}

/// Read an item-table STL description for `key` (e.g. the token's `QITEM_<sn>`).
fn stl_item_desc(stb_dir: &Path, stl_name: &str, key: &str) -> Option<String> {
    let stl = load_stl(&file_ci(stb_dir, stl_name).ok()?).ok()?;
    let idx = stl.keys.iter().position(|k| k.name == key)?;
    match stl.language_tables.first()?.rows.get(idx) {
        Some(StringTableRow::ItemRow(it)) => Some(it.description.clone()),
        _ => None,
    }
}

/// Make `npc_id` give quest `qid` from dialog: generate a quest-giver `.CON`,
/// register it in `LIST_EVENT.STB`, and wire the NPC's zone-IFO placement(s) to
/// it. `complete_trig` is the QSD turn-in trigger (`<qid>-3` Hunt / `<qid>-2`
/// Fetch). Replaces the NPC's existing conversation (warns). All `.bak`.
pub fn wire_quest_giver(
    root: &Path,
    npc_id: i32,
    qid: i32,
    complete_trig: &str,
    dry_run: bool,
) -> Result<WriteReport> {
    let stb_dir = resolve_stb_dir(root)?;
    let event_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("EVENT");
    let con_name = format!("QG{qid}.con"); // basename — the disk file + IFO use this
    // LIST_EVENT col 3 is a full VFS path (retail: "3Ddata\Event\EM99-001.con"),
    // and the client loads exactly that string — a bare basename won't resolve.
    let con_cell = format!("3Ddata\\Event\\{con_name}");
    let con_path = event_dir.join(&con_name);

    let mut changes = Vec::new();

    // 1) Where is this NPC placed?
    let placements = crate::ifo::find_npc(root, npc_id)?;
    if placements.is_empty() {
        bail!("npc {npc_id} isn't placed in any zone .IFO (can't make it a quest-giver)");
    }

    // 2) Generate the conversation (self-verify it parses).
    let con_bytes = crate::convo::build_quest_giver(qid, complete_trig, Default::default());
    crate::convo::ConFile::parse(&con_bytes).context("generated .CON failed to self-parse")?;
    changes.push(format!(
        "CREATE {} ({} bytes) — accept→QF_doQuestTrigger(\"{qid}-1\"), turn-in→QF_doQuestTrigger(\"{complete_trig}\")",
        con_path.display(),
        con_bytes.len()
    ));

    // 3) LIST_EVENT row (so the filename resolves to a conversation index).
    //    Match an existing row by *basename* (idempotent / fixes an earlier row),
    //    else append. The cell holds the full VFS path.
    let event_path = file_ci(&stb_dir, "LIST_EVENT.STB")?;
    let mut event_stb = load_stb(&event_path)?;
    let basename_of =
        |c: &str| c.rsplit(['\\', '/']).next().unwrap_or(c).trim().to_string();
    let existing = event_stb
        .data
        .iter()
        .position(|r| basename_of(cell(r, EVENT_COL_FILE)).eq_ignore_ascii_case(&con_name));
    match existing {
        Some(idx) if cell(&event_stb.data[idx], EVENT_COL_FILE) == con_cell => {
            changes.push(format!("LIST_EVENT already lists \"{con_cell}\" (row {idx})"));
        }
        Some(idx) => {
            set_cell(&mut event_stb.data[idx], EVENT_COL_FILE, &con_cell);
            changes.push(format!("FIX LIST_EVENT row {idx} -> \"{con_cell}\""));
        }
        None => {
            let mut row = template_row(&event_stb, |r| !cell(r, EVENT_COL_FILE).trim().is_empty())
                .context("no LIST_EVENT template row to clone")?;
            set_cell(&mut row, 0, &event_stb.data.len().to_string());
            set_cell(&mut row, EVENT_COL_FILE, &con_cell);
            event_stb.data.push(row);
            changes.push(format!("APPEND LIST_EVENT row -> \"{con_cell}\""));
        }
    }

    // 4) Wire every placement of this NPC.
    for p in &placements {
        let ifo_name = p.ifo_path.file_name().and_then(|s| s.to_str()).unwrap_or("?");
        if !p.conversation.trim().is_empty() && !p.conversation.eq_ignore_ascii_case(&con_name) {
            changes.push(format!(
                "WARN npc {npc_id} in {ifo_name} already had conversation \"{}\" — replacing",
                p.conversation
            ));
        }
        crate::ifo::wire_npc_in_ifo(&p.ifo_path, npc_id, &con_name, dry_run)?;
        changes.push(format!("WIRE {} (npc {npc_id}) -> {con_name}", p.ifo_path.display()));
    }

    if dry_run {
        return Ok(WriteReport {
            dry_run: true,
            changes,
            backups: Vec::new(),
        });
    }

    // Commit the .CON + LIST_EVENT (the IFOs were written + backed up above).
    let mut backups = Vec::new();
    fs::create_dir_all(&event_dir).ok();
    if con_path.exists() {
        if let Some(b) = backup_once(&con_path)? {
            backups.push(b);
        }
    }
    fs::write(&con_path, &con_bytes).with_context(|| format!("writing {}", con_path.display()))?;
    if let Some(b) = backup_once(&event_path)? {
        backups.push(b);
    }
    write_stb(&mut event_stb, &event_path)?;

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
