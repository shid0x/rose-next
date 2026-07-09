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
use crate::gen::{GeneratedQuest, QuestSpec};

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
        gen.qsd
            .patterns
            .iter()
            .map(|p| p.triggers.len())
            .sum::<usize>()
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

    // --- Hunt-specific: a token quest-item + (col-41 merge OR host-QSD chain) +
    //     token STL **per hunt objective** (primary + extras). The shared tables
    //     (LIST_QUESTITEM / LIST_NPC / LIST_QUESTITEM_S.STL) are loaded once and
    //     mutated across every objective; chained kill triggers accumulate in
    //     `host_qsds` (keyed by path, so two objectives can share a host). Held as
    //     Options so the commit only writes what actually changed. ---
    let mut hunt_qitem: Option<(PathBuf, STB)> = None;
    let mut hunt_npc: Option<(PathBuf, STB)> = None;
    let mut hunt_qitem_stl: Option<(PathBuf, STL)> = None;
    let mut host_qsds: Vec<(PathBuf, crate::qsd::QsdFile)> = Vec::new();

    if !gen.hunts.is_empty() {
        let qitem_path = file_ci(&stb_dir, "LIST_QUESTITEM.STB")?;
        let npc_path = file_ci(&stb_dir, "LIST_NPC.STB")?;
        let qitem_stl_path = file_ci(&stb_dir, "LIST_QUESTITEM_S.STL")?;
        let mut qitem = load_stb(&qitem_path)?;
        let mut npc = load_stb(&npc_path)?;
        let mut qitem_stl = load_stl(&qitem_stl_path)?;
        let mut npc_changed = false;

        for (idx, h) in gen.hunts.iter().enumerate() {
            let token_type = h.token_item_sn / 1000;
            let token_id = (h.token_item_sn % 1000) as usize;
            if token_type != QUEST_ITEM_TYPE {
                bail!(
                    "token item SN {} is not a quest item (type {token_type} != {QUEST_ITEM_TYPE})",
                    h.token_item_sn
                );
            }
            if token_id > 999 {
                bail!("token item id {token_id} exceeds 999 (type*1000+id encoding limit)");
            }
            if token_id >= qitem.data.len() {
                bail!(
                    "token item id {token_id} is beyond LIST_QUESTITEM ({} rows); no free placeholder",
                    qitem.data.len()
                );
            }
            // Checks the *working* table, so a duplicate token id across this
            // quest's objectives is caught here too.
            if !cell(&qitem.data[token_id], QITEM_COL_NAME).is_empty() {
                bail!(
                    "token item row {token_id} is already in use (\"{}\")",
                    cell(&qitem.data[token_id], QITEM_COL_NAME)
                );
            }
            let token_stl_key = format!("QITEM_{}_{idx}", spec.quest_sn);
            if qitem_stl.keys.iter().any(|k| k.name == token_stl_key) {
                bail!("quest-item STL key already exists: {token_stl_key}");
            }

            let monster_row = npc
                .data
                .iter()
                .position(|r| {
                    r.first().and_then(|s| s.trim().parse::<i32>().ok()) == Some(h.monster_id)
                })
                .ok_or_else(|| anyhow!("monster {} not found in LIST_NPC", h.monster_id))?;
            let existing_de = cell(&npc.data[monster_row], NPC_COL_DEAD_EVENT)
                .trim()
                .to_string();
            if !h.chain_into_existing && !existing_de.is_empty() {
                bail!(
                    "monster {} already has a dead-event trigger (\"{existing_de}\"); \
                     enable chaining to add to it (ownership rule)",
                    h.monster_id
                );
            }
            if h.chain_into_existing && existing_de.is_empty() {
                bail!(
                    "monster {} has no existing trigger to chain onto",
                    h.monster_id
                );
            }

            // token row.
            let mut token_row = template_row(&qitem, |r| {
                !cell(r, QITEM_COL_NAME).trim().is_empty() && cell_is_int(r, QITEM_COL_TYPE)
            })
            .context("no quest-item template row to clone")?;
            set_cell(&mut token_row, 0, "");
            set_cell(&mut token_row, QITEM_COL_NAME, &h.token_name);
            set_cell(
                &mut token_row,
                QITEM_COL_BELONGING_QUEST,
                &spec.quest_sn.to_string(),
            );
            set_cell(&mut token_row, QITEM_COL_STL_LINK, &token_stl_key);
            if let Some(icon) = h.token_icon {
                set_cell(&mut token_row, QITEM_COL_ICON, &icon.to_string());
            }
            qitem.data[token_id] = token_row;
            changes.push(format!(
                "SET LIST_QUESTITEM row {token_id} = token \"{}\" (SN {})",
                h.token_name, h.token_item_sn
            ));

            // Wire the kill trigger: claim col-41 (free monster) or splice into
            // the monster's existing dead-event chain (chaining).
            if h.chain_into_existing {
                let mut our_kill = h.host_kill_trigger.clone().ok_or_else(|| {
                    anyhow!("chaining requested but no kill trigger was generated")
                })?;
                // Resolve the host file (path) once, then operate on a single
                // working copy per path so multiple objectives can chain into it.
                let (host_path, _, _, _) = find_host_qsd(&questdata_dir, &existing_de)?;
                if !host_qsds.iter().any(|(p, _)| p == &host_path) {
                    let q = crate::qsd::QsdFile::read_file(&host_path)
                        .map_err(|e| anyhow!("reading host QSD {}: {e}", host_path.display()))?;
                    host_qsds.push((host_path.clone(), q));
                }
                let host = &mut host_qsds
                    .iter_mut()
                    .find(|(p, _)| p == &host_path)
                    .unwrap()
                    .1;
                let (pi, ti) = find_trigger_in(host, existing_de.as_bytes())
                    .ok_or_else(|| anyhow!("host trigger \"{existing_de}\" vanished"))?;
                our_kill.check_next = host.patterns[pi].triggers[ti].check_next;
                host.patterns[pi].triggers[ti].check_next = 1;
                host.patterns[pi].triggers.insert(ti + 1, our_kill);
                changes.push(format!(
                    "CHAIN kill trigger \"{}\" into {} after \"{existing_de}\"",
                    h.kill_trigger,
                    host_path
                        .file_name()
                        .and_then(|s| s.to_str())
                        .unwrap_or("?")
                ));
            } else {
                set_cell(
                    &mut npc.data[monster_row],
                    NPC_COL_DEAD_EVENT,
                    &h.kill_trigger,
                );
                npc_changed = true;
                changes.push(format!(
                    "MERGE LIST_NPC row {monster_row} (npc {}) col-41 = \"{}\"",
                    h.monster_id, h.kill_trigger
                ));
            }

            // token STL.
            qitem_stl.keys.push(StringTableKey {
                id: h.token_item_sn as u32,
                name: token_stl_key.clone(),
            });
            for lt in &mut qitem_stl.language_tables {
                lt.rows.push(StringTableRow::ItemRow(ItemRowData {
                    text: h.token_name.clone(),
                    description: h.token_desc.clone(),
                }));
            }
            changes.push(format!(
                "APPEND LIST_QUESTITEM_S.STL key \"{token_stl_key}\" -> \"{}\"",
                h.token_name
            ));
        }

        hunt_qitem = Some((qitem_path, qitem));
        hunt_qitem_stl = Some((qitem_stl_path, qitem_stl));
        if npc_changed {
            hunt_npc = Some((npc_path, npc));
        }
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
    for (p, _) in &host_qsds {
        to_backup.push(p);
    }
    for p in to_backup {
        if let Some(b) = backup_once(p)? {
            backups.push(b);
        }
    }

    fs::create_dir_all(&questdata_dir).ok();
    fs::write(&qsd_file, &qsd_bytes).with_context(|| format!("writing {}", qsd_file.display()))?;
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
    for (p, host) in host_qsds {
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

    // 3) Hunt cleanup: blank EVERY token row this quest owns (a quest can have
    //    several, one per hunt objective), then un-wire every kill trigger.
    let qitem_path = file_ci(&stb_dir, "LIST_QUESTITEM.STB")?;
    let mut qitem = load_stb(&qitem_path)?;
    let mut qitem_changed = false;
    let token_rows: Vec<usize> = qitem
        .data
        .iter()
        .enumerate()
        .filter(|(_, r)| {
            cell(r, QITEM_COL_BELONGING_QUEST)
                .trim()
                .parse::<i32>()
                .ok()
                == Some(quest_sn)
        })
        .map(|(i, _)| i)
        .collect();
    for t in token_rows {
        qitem.data[t] = empty_row(qitem.cols());
        qitem_changed = true;
        changes.push(format!("BLANK LIST_QUESTITEM row {t} (token)"));
    }

    // Un-wire every kill trigger named `<sn>-<n>` — both the ones we claimed on a
    // monster's col-41 and the ones we chained into another quest's host QSD. The
    // quest's own QX-<sn>.QSD is removed wholesale, so its triggers need no
    // explicit un-splicing (skip it in the host scan).
    let npc_path = file_ci(&stb_dir, "LIST_NPC.STB")?;
    let mut npc = load_stb(&npc_path)?;
    let mut npc_changed = false;
    for i in 0..npc.data.len() {
        let de = cell(&npc.data[i], NPC_COL_DEAD_EVENT).trim().to_string();
        if trigger_belongs_to_quest(quest_sn, &de) {
            set_cell(&mut npc.data[i], NPC_COL_DEAD_EVENT, "");
            npc_changed = true;
            changes.push(format!("CLEAR LIST_NPC row {i} col-41 (was \"{de}\")"));
        }
    }
    let host_qsd_edits =
        unchain_quest_kill_triggers(&questdata_dir, &qsd_name, quest_sn, &mut changes)?;

    let manifest = crate::manifest::manifest_path(root, quest_sn)
        .ok()
        .filter(|p| p.exists());

    // --- dialog cleanup: undo any quest-giver wiring for QG<sn>.con ---
    let event_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("EVENT");
    let con_name = format!("QG{quest_sn}.con");
    let con_path = event_dir.join(&con_name);
    let con_exists = con_path.exists();
    let givers = crate::manifest::read_manifest(root, quest_sn)
        .map(|m| m.givers)
        .unwrap_or_default();
    // LIST_EVENT row that points at our .CON (blank its filename so it matches no NPC).
    let mut event_cleanup: Option<(PathBuf, STB)> = None;
    if let Ok(ep) = file_ci(&stb_dir, "LIST_EVENT.STB") {
        if let Ok(mut ev) = load_stb(&ep) {
            if let Some(idx) = ev.data.iter().position(|r| {
                cell(r, EVENT_COL_FILE)
                    .rsplit(['\\', '/'])
                    .next()
                    .is_some_and(|f| f.eq_ignore_ascii_case(&con_name))
            }) {
                set_cell(&mut ev.data[idx], EVENT_COL_FILE, "");
                changes.push(format!("CLEAR LIST_EVENT row {idx} (was {con_name})"));
                event_cleanup = Some((ep, ev));
            }
        }
    }
    for g in givers.iter().filter(|g| !g.append) {
        let to = if g.original_conversation.trim().is_empty() {
            "(none)".to_string()
        } else {
            g.original_conversation.clone()
        };
        let name = Path::new(&g.ifo_path)
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("?");
        changes.push(format!("RESTORE npc {} in {name} -> {to}", g.npc_id));
    }
    if con_exists {
        changes.push(format!("DELETE {}", con_path.display()));
    }

    // Appended dialog options (QEX1): strip our QE<sn>_ items from any .CON that
    // has them. Scan-based (like the kill-trigger un-wiring) so it works without
    // a manifest.
    let mut con_strips: Vec<(PathBuf, Vec<u8>)> = Vec::new();
    if let Ok(rd) = fs::read_dir(&event_dir) {
        for entry in rd.flatten() {
            let path = entry.path();
            let is_con = path
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("con"));
            let is_own_giver = path
                .file_name()
                .and_then(|n| n.to_str())
                .is_some_and(|n| n.eq_ignore_ascii_case(&con_name));
            if !is_con || is_own_giver {
                continue; // QG<sn>.con (dedicated giver) is deleted wholesale above
            }
            let Ok(mut con) = crate::convo::ConFile::read_file(&path) else {
                continue;
            };
            if !crate::convo::quest_option_qids(&con).contains(&quest_sn) {
                continue;
            }
            crate::convo::remove_quest_option(&mut con, quest_sn);
            changes.push(format!(
                "STRIP quest {quest_sn} dialog option from {}",
                path.display()
            ));
            con_strips.push((path, con.rebuild()));
        }
    }

    // Deleting the quest blanks its LIST_QUEST row, but characters that already
    // accepted it keep quest {sn} in their saved quest log (DB) — it will show
    // as a blank entry in their quest window until they abandon it in-game.
    changes.push(format!(
        "NOTE: characters that already accepted quest {quest_sn} still have it in their \
         quest log; they should abandon the blank entry in-game (or clear it in the DB)"
    ));

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
    for (p, _) in &host_qsd_edits {
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
    for (p, host) in host_qsd_edits {
        host.write_file(&p)
            .with_context(|| format!("writing host QSD {}", p.display()))?;
    }
    if qsd_file.exists() {
        fs::remove_file(&qsd_file).with_context(|| format!("removing {}", qsd_file.display()))?;
        changes.push(format!("DELETE {}", qsd_file.display()));
    }
    // Dialog cleanup: restore each NPC's original conversation, clear the
    // LIST_EVENT row, and remove the generated .CON. Append-mode wirings never
    // touched the IFO — their cleanup is the .CON strip below.
    for g in givers.iter().filter(|g| !g.append) {
        let ifo = PathBuf::from(&g.ifo_path);
        if ifo.exists() {
            crate::ifo::wire_npc_in_ifo(&ifo, g.npc_id, &g.original_conversation, false).ok();
        }
    }
    for (path, bytes) in &con_strips {
        if let Some(b) = backup_once(path)? {
            backups.push(b);
        }
        fs::write(path, bytes).with_context(|| format!("writing {}", path.display()))?;
    }
    if let Some((p, mut ev)) = event_cleanup {
        if let Some(b) = backup_once(&p)? {
            backups.push(b);
        }
        write_stb(&mut ev, &p)?;
    }
    if con_exists {
        if let Some(b) = backup_once(&con_path)? {
            backups.push(b);
        }
        fs::remove_file(&con_path).ok();
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
    p.get(o..o + 2)
        .map_or(0, |b| i16::from_le_bytes([b[0], b[1]]))
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
        .find(|t| {
            t.rewards
                .iter()
                .any(|e| e.etype == 0 && rd_u8(&e.payload, 4) == 0)
        })
        .ok_or_else(|| anyhow!("no complete trigger in {}", qsd_file.display()))?;

    // One-time iff a COND_014 switch guard is present (on the register trigger).
    let one_time_switch = qsd
        .patterns
        .iter()
        .flat_map(|p| p.triggers.iter())
        .flat_map(|t| t.conditions.iter())
        .find(|e| e.etype == 14 && e.payload.len() >= 2)
        .map(|e| i16::from_le_bytes([e.payload[0], e.payload[1]]) as i32);

    // One COND_004 per objective, in trigger order: [primary, extra, extra, …].
    let cond4s: Vec<(i32, i32)> = complete
        .conditions
        .iter()
        .filter(|e| e.etype == 4)
        .map(|e| (rd_i32(&e.payload, 4), rd_i32(&e.payload, 12)))
        .collect();
    let (primary_item_sn, count) = *cond4s
        .first()
        .ok_or_else(|| anyhow!("no item-count condition in complete trigger"))?;

    // Rewards: REWD_005 exp/zuly; REWD_001 op-1 = bonus reward item, op-0 = a
    // consumed fetch item (there can be several — collect every consumed SN).
    let mut reward_exp = 0;
    let mut reward_zuly = 0;
    let mut reward_item = None;
    let mut consumed: BTreeSet<i32> = BTreeSet::new();
    for e in &complete.rewards {
        match e.etype {
            5 => match rd_u8(&e.payload, 0) {
                0 => reward_exp = rd_i32(&e.payload, 4),
                1 => reward_zuly = rd_i32(&e.payload, 4),
                _ => {}
            },
            1 => match rd_u8(&e.payload, 4) {
                1 => reward_item = Some((rd_i32(&e.payload, 0), rd_i16(&e.payload, 6))),
                0 => {
                    consumed.insert(rd_i32(&e.payload, 0));
                }
                _ => {}
            },
            _ => {}
        }
    }

    // This quest's token items (one per hunt objective); a checked item that's a
    // token => Hunt, otherwise => Fetch.
    let qitem = load_stb(&file_ci(&stb_dir, "LIST_QUESTITEM.STB")?)?;
    let token_sns: BTreeSet<i32> = qitem
        .data
        .iter()
        .enumerate()
        .filter(|(_, r)| {
            cell(r, QITEM_COL_BELONGING_QUEST)
                .trim()
                .parse::<i32>()
                .ok()
                == Some(quest_sn)
        })
        .map(|(i, _)| QUEST_ITEM_TYPE * 1000 + i as i32)
        .collect();

    // Resolve a hunt token's (monster, name, desc, icon) from its row (the token
    // id is the row index) + its kill-trigger's monster.
    let make_hunt = |token_sn: i32| -> (i32, String, String, Option<i32>) {
        match qitem.data.get((token_sn % 1000) as usize) {
            Some(row) => (
                monster_for_token(&stb_dir, &questdata_dir, quest_sn, token_sn).unwrap_or(0),
                cell(row, QITEM_COL_NAME).to_string(),
                stl_item_desc(
                    &stb_dir,
                    "LIST_QUESTITEM_S.STL",
                    cell(row, QITEM_COL_STL_LINK),
                )
                .unwrap_or_default(),
                cell(row, QITEM_COL_ICON).trim().parse::<i32>().ok(),
            ),
            None => (0, String::new(), String::new(), None),
        }
    };

    let kind = if token_sns.contains(&primary_item_sn) {
        let (monster_id, token_name, token_desc, token_icon) = make_hunt(primary_item_sn);
        QuestKind::Hunt {
            monster_id,
            token_item_sn: primary_item_sn,
            token_name,
            token_desc,
            token_icon,
            chain_into_existing: false, // recomputed from the monster on save
        }
    } else {
        QuestKind::Fetch {
            item_sn: primary_item_sn,
            item_name: String::new(), // the UI re-derives this from the item id
            consume: consumed.contains(&primary_item_sn),
        }
    };

    // Extra objectives = the remaining COND_004s (best-effort; manifest-backed
    // quests reconstruct perfectly, this only runs for manifest-less ones).
    let extra_objectives: Vec<crate::gen::Objective> = cond4s
        .iter()
        .skip(1)
        .map(|&(item_sn, cnt)| {
            if token_sns.contains(&item_sn) {
                let (monster_id, token_name, token_desc, token_icon) = make_hunt(item_sn);
                crate::gen::Objective::Hunt {
                    monster_id,
                    count: cnt,
                    token_item_sn: item_sn,
                    token_name,
                    token_desc,
                    token_icon,
                    chain_into_existing: false,
                }
            } else {
                crate::gen::Objective::Fetch {
                    item_sn,
                    item_name: String::new(),
                    count: cnt,
                    consume: consumed.contains(&item_sn),
                }
            }
        })
        .collect();

    let (title, start_text, progress_text, complete_text) = stl_quest_texts(&stb_dir, quest_sn)
        .unwrap_or_else(|_| {
            (
                format!("Quest #{quest_sn}"),
                String::new(),
                String::new(),
                String::new(),
            )
        });

    Ok(QuestSpec {
        quest_sn,
        kind,
        count,
        reward_exp,
        reward_zuly,
        reward_item,
        one_time_switch,
        extra_objectives,
        title,
        start_text,
        progress_text,
        complete_text,
    })
}

/// The monster that drives a hunt token: find the kill trigger that grants the
/// token, then the NPC whose dead event fires it.
fn monster_for_token(
    stb_dir: &Path,
    questdata_dir: &Path,
    quest_sn: i32,
    token_sn: i32,
) -> Option<i32> {
    let kill = kill_trigger_name_for_token(questdata_dir, quest_sn, token_sn)?;
    monster_for_trigger_name(stb_dir, questdata_dir, &kill)
}

/// Scan every `.QSD` for the kill trigger of quest `quest_sn` that grants
/// `token_sn` (a `REWD_001` op-1 give of that item), returning its name.
fn kill_trigger_name_for_token(
    questdata_dir: &Path,
    quest_sn: i32,
    token_sn: i32,
) -> Option<String> {
    for entry in fs::read_dir(questdata_dir).ok()?.flatten() {
        let path = entry.path();
        if !path
            .extension()
            .and_then(|e| e.to_str())
            .is_some_and(|e| e.eq_ignore_ascii_case("qsd"))
        {
            continue;
        }
        let Ok(qsd) = crate::qsd::QsdFile::read_file(&path) else {
            continue;
        };
        for pat in &qsd.patterns {
            for t in &pat.triggers {
                let name = t.name.strip_suffix(b"\0").unwrap_or(&t.name);
                let Ok(name_str) = std::str::from_utf8(name) else {
                    continue;
                };
                if !trigger_belongs_to_quest(quest_sn, name_str) {
                    continue;
                }
                let gives = t.rewards.iter().any(|e| {
                    e.etype == 1 && rd_u8(&e.payload, 4) == 1 && rd_i32(&e.payload, 0) == token_sn
                });
                if gives {
                    return Some(name_str.to_string());
                }
            }
        }
    }
    None
}

/// The NPC whose dead event fires trigger `kill`: either it claims col-41
/// directly, or the trigger was spliced into its chain (walk back to the head).
fn monster_for_trigger_name(stb_dir: &Path, questdata_dir: &Path, kill: &str) -> Option<i32> {
    let npc = load_stb(&file_ci(stb_dir, "LIST_NPC.STB").ok()?).ok()?;
    let id_of = |r: &Vec<String>| r.first().and_then(|s| s.trim().parse::<i32>().ok());

    if let Some(r) = npc
        .data
        .iter()
        .find(|r| cell(r, NPC_COL_DEAD_EVENT).trim() == kill)
    {
        return id_of(r);
    }
    // Chained: find our trigger, walk back over the check_next run to the head.
    let (_, host, pi, ti) = find_host_qsd(questdata_dir, kill).ok()?;
    let triggers = &host.patterns[pi].triggers;
    let mut j = ti;
    while j > 0 && triggers[j - 1].check_next != 0 {
        j -= 1;
    }
    let head = triggers[j]
        .name
        .strip_suffix(b"\0")
        .unwrap_or(&triggers[j].name);
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
    let lt = stl
        .language_tables
        .first()
        .ok_or_else(|| anyhow!("no language table"))?;
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

/// The quest's own narrative lines for the dialog (so the NPC's words match the
/// quest). The option-button labels stay generic. `None` fields fall back to a
/// generic line.
#[derive(Debug, Default, Clone)]
pub struct GiverText {
    pub greeting: String,
    pub in_progress: String,
    pub after_complete: String,
    /// Append mode: the option line added to the NPC's existing dialog
    /// ("I heard you need some help...").
    pub hook: String,
    /// The player's accept choice under the start message.
    pub accept: String,
    /// The player's decline choice under the start message (append mode) /
    /// the dedicated giver's close line (replace mode).
    pub decline: String,
    /// The NPC's reply right after the player accepts.
    pub after_accept: String,
    /// The player's turn-in choice (shown when the quest can be handed in).
    pub turnin: String,
    /// The player's in-progress line (shown while the quest is unfinished).
    pub progress: String,
}

/// Upsert the quest-giver's dialog strings into the event-string table (keyed by
/// `QG<qid>_*` so re-wiring is idempotent) and return their ids.
fn giver_strings(
    ltb: &mut crate::ltb::LtbTable,
    qid: i32,
    text: Option<&GiverText>,
) -> crate::convo::GiverStrings {
    let k = |s: &str| format!("QG{qid}_{s}");
    let or = |s: &str, fallback: &str| {
        if s.trim().is_empty() {
            fallback.to_string()
        } else {
            s.to_string()
        }
    };
    let greeting = text.map_or_else(
        || "Greetings, traveler! I have a task that needs doing.".to_string(),
        |t| {
            or(
                &t.greeting,
                "Greetings, traveler! I have a task that needs doing.",
            )
        },
    );
    let in_progress = text.map_or_else(
        || "You haven't finished yet. Keep at it!".to_string(),
        |t| or(&t.in_progress, "You haven't finished yet. Keep at it!"),
    );
    let after_complete = text.map_or_else(
        || "Well done! Here is your reward.".to_string(),
        |t| or(&t.after_complete, "Well done! Here is your reward."),
    );
    let hook = text.map_or_else(
        || "I heard you might need some help.".to_string(),
        |t| or(&t.hook, "I heard you might need some help."),
    );
    let accept = text.map_or_else(
        || "I'll help you. (Accept quest)".to_string(),
        |t| or(&t.accept, "I'll help you. (Accept quest)"),
    );
    let decline = text.map_or_else(
        || "Maybe another time. (Decline)".to_string(),
        |t| or(&t.decline, "Maybe another time. (Decline)"),
    );
    // The dedicated giver's close line doubles as its decline — same custom
    // text, its own fallback (it also shows on turn-in / in-progress visits).
    let bye = text.map_or_else(
        || "Maybe another time. (Close)".to_string(),
        |t| or(&t.decline, "Maybe another time. (Close)"),
    );
    let after_accept = text.map_or_else(
        || "Thank you! Return to me when it is done.".to_string(),
        |t| or(&t.after_accept, "Thank you! Return to me when it is done."),
    );
    let turnin = text.map_or_else(
        || "I've completed the task. (Turn in)".to_string(),
        |t| or(&t.turnin, "I've completed the task. (Turn in)"),
    );
    let progress = text.map_or_else(
        || "I'm still working on it.".to_string(),
        |t| or(&t.progress, "I'm still working on it."),
    );
    crate::convo::GiverStrings {
        greeting: ltb.set_or_append(&k("greet"), &greeting) as i32,
        accept_option: ltb.set_or_append(&k("accept"), &accept) as i32,
        complete_option: ltb.set_or_append(&k("turnin"), &turnin) as i32,
        progress_option: ltb.set_or_append(&k("progopt"), &progress) as i32,
        bye_option: ltb.set_or_append(&k("bye"), &bye) as i32,
        after_accept: ltb.set_or_append(&k("afteracc"), &after_accept) as i32,
        after_complete: ltb.set_or_append(&k("afterdone"), &after_complete) as i32,
        in_progress: ltb.set_or_append(&k("inprog"), &in_progress) as i32,
        response_close: ltb.set_or_append(&k("close"), "(Close)") as i32,
        hook_option: ltb.set_or_append(&k("hook"), &hook) as i32,
        decline_option: ltb.set_or_append(&k("decline"), &decline) as i32,
    }
}

/// Read back the option texts previously written for `qid` (the `QG<qid>_*`
/// LTB keys) so the edit form can pre-fill them. Only the option-line fields
/// are filled (the narrative fields live in the quest STL); missing file /
/// keys / blank cells stay empty, meaning "use the default".
pub fn saved_giver_option_texts(root: &Path, qid: i32) -> GiverText {
    let ltb = resolve_stb_dir(root)
        .ok()
        .and_then(|d| d.parent().map(|p| p.join("EVENT")))
        .and_then(|e| file_ci(&e, "ulngtb_con.ltb").ok())
        .and_then(|p| crate::ltb::LtbTable::read_file(&p).ok());
    let Some(ltb) = ltb else {
        return GiverText::default();
    };
    let get = |s: &str| {
        ltb.get(&format!("QG{qid}_{s}"))
            .filter(|t| !t.trim().is_empty())
            .unwrap_or_default()
    };
    GiverText {
        hook: get("hook"),
        accept: get("accept"),
        decline: get("decline"),
        after_accept: get("afteracc"),
        turnin: get("turnin"),
        progress: get("progopt"),
        ..GiverText::default()
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
    text: Option<&GiverText>,
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

    // 2) Dialog text: upsert our strings into the event-string table, then build
    //    the conversation pointing at the new ids.
    let ltb_path = file_ci(&event_dir, "ulngtb_con.ltb")?;
    let mut ltb = crate::ltb::LtbTable::read_file(&ltb_path)?;
    let giver = giver_strings(&mut ltb, qid, text);
    let con_bytes = crate::convo::build_quest_giver(qid, complete_trig, giver);
    crate::convo::ConFile::parse(&con_bytes).context("generated .CON failed to self-parse")?;
    changes.push(format!(
        "CREATE {} ({} bytes) — accept→QF_doQuestTrigger(\"{qid}-1\"), turn-in→QF_doQuestTrigger(\"{complete_trig}\")",
        con_path.display(),
        con_bytes.len()
    ));
    changes.push(format!("UPSERT dialog strings into {}", ltb_path.display()));

    // 3) LIST_EVENT row (so the filename resolves to a conversation index).
    //    Match an existing row by *basename* (idempotent / fixes an earlier row),
    //    else append. The cell holds the full VFS path.
    let event_path = file_ci(&stb_dir, "LIST_EVENT.STB")?;
    let mut event_stb = load_stb(&event_path)?;
    let basename_of = |c: &str| c.rsplit(['\\', '/']).next().unwrap_or(c).trim().to_string();
    let existing = event_stb
        .data
        .iter()
        .position(|r| basename_of(cell(r, EVENT_COL_FILE)).eq_ignore_ascii_case(&con_name));
    match existing {
        Some(idx) if cell(&event_stb.data[idx], EVENT_COL_FILE) == con_cell => {
            changes.push(format!(
                "LIST_EVENT already lists \"{con_cell}\" (row {idx})"
            ));
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

    // 4) Wire every placement of this NPC. Record what it had before (so delete
    //    can restore it) — but only when it isn't already our own conversation.
    let mut wirings = Vec::new();
    for p in &placements {
        let ifo_name = p
            .ifo_path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("?");
        if !p.conversation.trim().is_empty() && !p.conversation.eq_ignore_ascii_case(&con_name) {
            changes.push(format!(
                "WARN npc {npc_id} in {ifo_name} already had conversation \"{}\" — replacing",
                p.conversation
            ));
        }
        let original = if p.conversation.eq_ignore_ascii_case(&con_name) {
            String::new() // re-wire: don't clobber the true original in the manifest
        } else {
            p.conversation.clone()
        };
        wirings.push(crate::manifest::GiverWiring {
            npc_id,
            ifo_path: p.ifo_path.to_string_lossy().into_owned(),
            original_conversation: original,
            append: false,
        });
        crate::ifo::wire_npc_in_ifo(&p.ifo_path, npc_id, &con_name, dry_run)?;
        changes.push(format!(
            "WIRE {} (npc {npc_id}) -> {con_name}",
            p.ifo_path.display()
        ));
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
    if let Some(b) = backup_once(&ltb_path)? {
        backups.push(b);
    }
    fs::write(&ltb_path, ltb.to_bytes())
        .with_context(|| format!("writing {}", ltb_path.display()))?;

    // Record the wiring in the quest's manifest (if it has one) so delete can
    // restore the NPC and edit can re-offer it.
    crate::manifest::add_givers(root, qid, &wirings).ok();

    Ok(WriteReport {
        dry_run: false,
        changes,
        backups,
    })
}

/// Make `npc_id` give quest `qid` by **appending** a dialog option to its
/// *existing* conversation(s) instead of replacing them: namespaced `QE<qid>_*`
/// menu items at the end of the root menu + the matching Lua in a `QEX1`
/// appendix chunk (requires the appendix-aware client). No IFO or LIST_EVENT
/// change — the NPC keeps its own dialog wiring. Idempotent; `.bak` per file.
pub fn append_quest_to_npc_dialog(
    root: &Path,
    npc_id: i32,
    qid: i32,
    complete_trig: &str,
    text: Option<&GiverText>,
    dry_run: bool,
) -> Result<WriteReport> {
    let stb_dir = resolve_stb_dir(root)?;
    let event_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("EVENT");

    let placements = crate::ifo::find_npc(root, npc_id)?;
    if placements.is_empty() {
        bail!("npc {npc_id} isn't placed in any zone .IFO (can't make it a quest-giver)");
    }
    // Every distinct conversation this NPC's placements use (usually one).
    let mut con_names: Vec<String> = placements
        .iter()
        .map(|p| p.conversation.trim().to_string())
        .filter(|c| !c.is_empty())
        .collect();
    con_names.sort_by_key(|c| c.to_ascii_lowercase());
    con_names.dedup_by(|a, b| a.eq_ignore_ascii_case(b));
    if con_names.is_empty() {
        bail!(
            "npc {npc_id} has no existing conversation — use the dedicated quest-giver \
             (replace) mode instead"
        );
    }

    // Dialog text: same string set as the dedicated giver (bye unused; the
    // greeting doubles as the start message shown before Accept / Decline).
    let ltb_path = file_ci(&event_dir, "ulngtb_con.ltb")?;
    let mut ltb = crate::ltb::LtbTable::read_file(&ltb_path)?;
    let strings = giver_strings(&mut ltb, qid, text);

    let mut changes = Vec::new();
    let mut outputs: Vec<(PathBuf, Vec<u8>)> = Vec::new();
    for name in &con_names {
        let path = file_ci(&event_dir, name).with_context(|| {
            format!("npc {npc_id}'s conversation \"{name}\" not found in EVENT dir")
        })?;
        let mut con = crate::convo::ConFile::read_file(&path)?;
        let re_append = crate::convo::quest_option_qids(&con).contains(&qid);
        crate::convo::append_quest_option(&mut con, qid, complete_trig, &strings)?;
        let bytes = con.rebuild();
        crate::convo::ConFile::parse(&bytes).context("rebuilt .CON failed to self-parse")?;
        changes.push(format!(
            "{} quest {qid} option in {} (accept→QF_doQuestTrigger(\"{qid}-1\"), \
             turn-in→QF_doQuestTrigger(\"{complete_trig}\"))",
            if re_append { "REFRESH" } else { "APPEND" },
            path.display()
        ));
        outputs.push((path, bytes));
    }
    changes.push(format!("UPSERT dialog strings into {}", ltb_path.display()));
    changes.push(
        "NOTE: appended options need the appendix-aware client (QEX1) — deploy client + data together"
            .to_string(),
    );

    if dry_run {
        return Ok(WriteReport {
            dry_run: true,
            changes,
            backups: Vec::new(),
        });
    }

    let mut backups = Vec::new();
    for (path, bytes) in &outputs {
        if let Some(b) = backup_once(path)? {
            backups.push(b);
        }
        fs::write(path, bytes).with_context(|| format!("writing {}", path.display()))?;
    }
    if let Some(b) = backup_once(&ltb_path)? {
        backups.push(b);
    }
    fs::write(&ltb_path, ltb.to_bytes())
        .with_context(|| format!("writing {}", ltb_path.display()))?;

    // Manifest: record the giver so edit re-offers it; `append: true` tells
    // delete to skip IFO restore (the .CON cleanup is scan-based).
    let wirings: Vec<crate::manifest::GiverWiring> = placements
        .iter()
        .map(|p| crate::manifest::GiverWiring {
            npc_id,
            ifo_path: p.ifo_path.to_string_lossy().into_owned(),
            original_conversation: p.conversation.clone(),
            append: true,
        })
        .collect();
    crate::manifest::add_givers(root, qid, &wirings).ok();

    Ok(WriteReport {
        dry_run: false,
        changes,
        backups,
    })
}

/// The set of persistent character quest-switch numbers already referenced by any
/// `COND_014` / `REWD_015` across every `.QSD` (retail + ours).
pub fn used_switches(root: &Path) -> Result<BTreeSet<i32>> {
    let stb_dir = resolve_stb_dir(root)?;
    let questdata_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("QUESTDATA");
    let mut used = BTreeSet::new();
    for entry in fs::read_dir(&questdata_dir)
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
        let Ok(qsd) = crate::qsd::QsdFile::read_file(&path) else {
            continue;
        };
        for pat in &qsd.patterns {
            for t in &pat.triggers {
                for e in t.conditions.iter().chain(t.rewards.iter()) {
                    if (e.etype == 14 || e.etype == 15) && e.payload.len() >= 2 {
                        used.insert(i16::from_le_bytes([e.payload[0], e.payload[1]]) as i32);
                    }
                }
            }
        }
    }
    Ok(used)
}

/// The first free character quest-switch (gap-fill within the 512 cap). Switches
/// are only ever read/written via `COND_014` / `REWD_015`, so any number not
/// referenced by a `.QSD` is genuinely free.
pub fn next_free_switch(root: &Path) -> Result<i32> {
    let used = used_switches(root)?;
    (0..512)
        .find(|s| !used.contains(s))
        .ok_or_else(|| anyhow!("no free character quest-switch (all 512 in use)"))
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

/// Does `name` name a trigger of quest `sn` — i.e. `<sn>-<digits>`? Trigger names
/// are globally `<questSN>-<n>`, so this uniquely identifies the owning quest.
fn trigger_belongs_to_quest(sn: i32, name: &str) -> bool {
    match name.trim().split_once('-') {
        Some((head, tail)) => {
            head.trim().parse::<i32>().ok() == Some(sn)
                && !tail.is_empty()
                && tail.bytes().all(|b| b.is_ascii_digit())
        }
        None => false,
    }
}

/// Remove every chained kill trigger of quest `sn` from the host QSDs it was
/// spliced into (all `.QSD` under `questdata_dir` except the quest's own
/// `own_qsd_name`, which is deleted wholesale). Each removal hands the trigger's
/// `check_next` back to its predecessor — the exact inverse of the splice in
/// `apply_quest` — so the host chain is restored byte-for-byte, even when several
/// objectives chained into the same host. Returns the edited (path, QSD) pairs.
fn unchain_quest_kill_triggers(
    questdata_dir: &Path,
    own_qsd_name: &str,
    sn: i32,
    changes: &mut Vec<String>,
) -> Result<Vec<(PathBuf, crate::qsd::QsdFile)>> {
    let mut edits = Vec::new();
    let rd = match fs::read_dir(questdata_dir) {
        Ok(rd) => rd,
        Err(_) => return Ok(edits),
    };
    for entry in rd {
        let path = entry?.path();
        let is_qsd = path
            .extension()
            .and_then(|e| e.to_str())
            .is_some_and(|e| e.eq_ignore_ascii_case("qsd"));
        let is_own = path
            .file_name()
            .and_then(|s| s.to_str())
            .is_some_and(|f| f.eq_ignore_ascii_case(own_qsd_name));
        if !is_qsd || is_own {
            continue;
        }
        let Ok(mut qsd) = crate::qsd::QsdFile::read_file(&path) else {
            continue;
        };
        let mut changed = false;
        // Re-scan after each removal (indices shift); each pass removes one of
        // our triggers until none remain.
        loop {
            let mut found = None;
            'scan: for (pi, pat) in qsd.patterns.iter().enumerate() {
                for (ti, t) in pat.triggers.iter().enumerate() {
                    let tn = t.name.strip_suffix(b"\0").unwrap_or(&t.name);
                    if std::str::from_utf8(tn)
                        .map(|n| trigger_belongs_to_quest(sn, n))
                        .unwrap_or(false)
                    {
                        found = Some((pi, ti));
                        break 'scan;
                    }
                }
            }
            let Some((pi, ti)) = found else { break };
            if ti > 0 {
                let cn = qsd.patterns[pi].triggers[ti].check_next;
                qsd.patterns[pi].triggers[ti - 1].check_next = cn;
            }
            let removed = qsd.patterns[pi].triggers.remove(ti);
            let nm =
                String::from_utf8_lossy(removed.name.strip_suffix(b"\0").unwrap_or(&removed.name))
                    .into_owned();
            changes.push(format!(
                "UNCHAIN \"{nm}\" from {}",
                path.file_name().and_then(|s| s.to_str()).unwrap_or("?")
            ));
            changed = true;
        }
        if changed {
            edits.push((path, qsd));
        }
    }
    Ok(edits)
}

/// Locate a trigger by name within an already-loaded QSD, returning
/// (pattern index, trigger index). Names are stored NUL-terminated; the col-41 /
/// chain form is not — compare against the stripped form.
fn find_trigger_in(qsd: &crate::qsd::QsdFile, name: &[u8]) -> Option<(usize, usize)> {
    for (pi, pat) in qsd.patterns.iter().enumerate() {
        for (ti, t) in pat.triggers.iter().enumerate() {
            let tn = t.name.strip_suffix(b"\0").unwrap_or(&t.name);
            if tn == name {
                return Some((pi, ti));
            }
        }
    }
    None
}

fn file_ci(dir: &Path, name: &str) -> Result<PathBuf> {
    let exact = dir.join(name);
    if exact.exists() {
        return Ok(exact);
    }
    if let Ok(rd) = fs::read_dir(dir) {
        for entry in rd.flatten() {
            if entry
                .file_name()
                .to_string_lossy()
                .eq_ignore_ascii_case(name)
            {
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
