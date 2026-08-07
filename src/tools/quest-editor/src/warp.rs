//! Orphan map-teleport triggers — find them, and define them as warps.
//!
//! ## What an orphan trigger is
//!
//! A map event object (`.IFO` lump 12) carries a **QSD trigger name**. When the
//! player uses the object the client calls `QF_doQuestTrigger(name)`
//! (`objectactionprocessor.cpp`), checks the trigger's conditions locally, and
//! asks the server to run it (`Send_cli_QUEST_REQ(TYPE_QUEST_REQ_DO_TRIGGER)`).
//! The server looks the name up in the quest data loaded from
//! `LIST_QUESTDATA.STB`. The event object's own record does **not** carry the
//! destination — the server reads and discards the two trigger strings
//! (`zonefile.cpp` `LUMP_TERRAIN_EVENT`) and keys the object by its location.
//!
//! So if no loaded `.QSD` defines that trigger name, the object is inert: the
//! player uses it and nothing happens. In our data 21 such names are referenced
//! by maps and defined nowhere — mostly zone-to-zone transitions
//! (`fowtopri`, `metoluna`, `pritosea`, …).
//!
//! ## What this module writes
//!
//! One new `.QSD` holding a trigger per orphan: no conditions, one `REWD_007`
//! reward — "warp to zone Z at (X, Y)" (`F_QSTREWD007` → `classUSER::Reward_WARP`
//! → `Send_gsv_RELAY_REQ(RELAY_TYPE_RECALL, …)`; coordinates are world units,
//! i.e. centimetres, the same scale the `.IFO` positions use). The file is
//! registered by appending one row to `LIST_QUESTDATA.STB`.
//!
//! Existing `.QSD` files are never touched. Re-running merges into the file this
//! command created (same-named triggers are replaced), so the destination map is
//! the single source of truth and edits are re-appliable.
//!
//! ## Destinations
//!
//! There is nothing in the data that says where a given trigger *should* go —
//! retail shipped the QSDs we don't have. So destinations come from a mapping
//! file the user fills in; `--template` emits one pre-filled with every orphan's
//! own location (so "send A to where B stands" is a copy/paste) and with an
//! automatic guess for exact inverse-named pairs (`XtoY` ↔ `YtoX`).

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

use anyhow::{anyhow, bail, Context, Result};

use crate::data::{resolve_maps_dir, resolve_stb_dir};
use crate::ifo;
use crate::qsd::{Entity, Pattern, QsdFile, Trigger};
use crate::write::{backup_once, cell, empty_row, file_ci, load_stb, set_cell, write_stb, QDATA_COL_PATH};

/// Trigger names that mean "nothing wired here" rather than a real target.
const PLACEHOLDER_NAMES: &[&str] = &["", "empty", "null", "none"];

/// LIST_ZONE column holding the `.zon` path (display index; col 0 is the root).
const ZONE_COL_FILE: usize = 2;
/// LIST_ZONE column holding the zone's name.
const ZONE_COL_NAME: usize = 1;

/// The `.QSD` this command owns. Kept stable so re-runs merge instead of piling
/// up files, and greppable in `LIST_QUESTDATA.STB`.
const WARP_QSD_FILENAME: &str = "WARP_TRIGGERS.QSD";
/// Label written into `LIST_QUESTDATA.STB` col 0 for our row.
const WARP_QSD_LABEL: &str = "QX-WARP";

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

/// A trigger name referenced by at least one map event object.
#[derive(Debug, Clone)]
pub struct TriggerSite {
    pub name: String,
    /// Zone number (LIST_ZONE row) the object stands in, if resolvable.
    pub zone_no: Option<i32>,
    pub zone_name: String,
    /// World position in game units (cm).
    pub world_x: i32,
    pub world_y: i32,
    /// How many event objects reference this name.
    pub placements: usize,
    pub ifo_path: PathBuf,
}

/// Where an orphan trigger should send the player.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Destination {
    pub zone_no: i32,
    pub x: i32,
    pub y: i32,
    /// `REWD_007.btPartyOpt` — 1 warps the whole party.
    pub party: bool,
}

pub struct Report {
    pub dry_run: bool,
    pub qsd_path: PathBuf,
    pub changes: Vec<String>,
    pub backups: Vec<PathBuf>,
}

impl Report {
    pub fn print(&self) {
        println!(
            "{}",
            if self.dry_run {
                "--- DRY RUN (nothing written; pass --write to apply) ---"
            } else {
                "--- APPLIED ---"
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

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

fn is_placeholder(name: &str) -> bool {
    let n = name.trim().to_ascii_lowercase();
    PLACEHOLDER_NAMES.iter().any(|p| *p == n)
}

/// Map each zone directory (lowercased, e.g. `junon\jg03`) to its LIST_ZONE row.
fn zone_dirs(stb_dir: &Path) -> Result<BTreeMap<String, (i32, String)>> {
    let zone = load_stb(&file_ci(stb_dir, "LIST_ZONE.STB")?)?;
    let mut out = BTreeMap::new();
    for (row_idx, row) in zone.data.iter().enumerate() {
        let path = cell(row, ZONE_COL_FILE).trim().replace("\\\\", "\\");
        if path.is_empty() {
            continue;
        }
        // "3DDATA\MAPS\JUNON\JG03\jg03.zon" -> "junon\jg03"
        let norm = path.replace('/', "\\").to_ascii_lowercase();
        let Some(rest) = norm.split("maps\\").nth(1) else {
            continue;
        };
        let Some(dir) = rest.rsplit_once('\\').map(|(d, _)| d) else {
            continue;
        };
        out.entry(dir.to_string())
            .or_insert((row_idx as i32, cell(row, ZONE_COL_NAME).trim().to_string()));
    }
    Ok(out)
}

/// The zone-dir key (`junon\jg03`) for an `.IFO` path, if it sits under a maps tree.
fn zone_key_for(ifo_path: &Path) -> Option<String> {
    let norm = ifo_path.to_string_lossy().replace('/', "\\").to_ascii_lowercase();
    let rest = norm.split("maps\\").nth(1)?;
    let dir = rest.rsplit_once('\\').map(|(d, _)| d)?;
    Some(dir.to_string())
}

/// Every trigger name defined by a `.QSD` the game actually loads (i.e. one
/// registered in `LIST_QUESTDATA.STB`).
pub fn defined_triggers(root: &Path) -> Result<BTreeSet<String>> {
    let stb_dir = resolve_stb_dir(root)?;
    let data_root = stb_dir
        .parent()
        .and_then(|p| p.parent())
        .ok_or_else(|| anyhow!("STB dir has no data root"))?;
    let qdata = load_stb(&file_ci(&stb_dir, "LIST_QUESTDATA.STB")?)?;

    let mut out = BTreeSet::new();
    for row in &qdata.data {
        let rel = cell(row, QDATA_COL_PATH).trim().replace("\\\\", "\\");
        if rel.is_empty() {
            continue;
        }
        let path = data_root.join(rel.replace('\\', std::path::MAIN_SEPARATOR_STR));
        let Ok(qsd) = QsdFile::read_file(&path) else {
            continue; // a registered path that isn't on disk defines nothing
        };
        for pattern in &qsd.patterns {
            for trigger in &pattern.triggers {
                out.insert(trigger_name(trigger).to_ascii_lowercase());
            }
        }
    }
    Ok(out)
}

fn trigger_name(t: &Trigger) -> String {
    let bytes: Vec<u8> = t.name.iter().copied().take_while(|b| *b != 0).collect();
    String::from_utf8_lossy(&bytes).into_owned()
}

/// Every trigger name referenced by a map event object but defined by no loaded
/// `.QSD`, with the location of (the first of) its event objects.
pub fn find_orphans(root: &Path) -> Result<Vec<TriggerSite>> {
    let stb_dir = resolve_stb_dir(root)?;
    let maps_dir = resolve_maps_dir(root)?;
    let dirs = zone_dirs(&stb_dir)?;
    let defined = defined_triggers(root)?;

    let mut sites: BTreeMap<String, TriggerSite> = BTreeMap::new();
    for found in ifo::scan_event_triggers(&maps_dir)? {
        let name = found.trigger.qsd_trigger.trim().to_string();
        if is_placeholder(&name) || defined.contains(&name.to_ascii_lowercase()) {
            continue;
        }
        let key = name.to_ascii_lowercase();
        if let Some(existing) = sites.get_mut(&key) {
            existing.placements += 1;
            continue;
        }
        let zone = zone_key_for(&found.ifo_path).and_then(|k| dirs.get(&k).cloned());
        sites.insert(
            key,
            TriggerSite {
                name,
                zone_no: zone.as_ref().map(|(n, _)| *n),
                zone_name: zone.map(|(_, n)| n).unwrap_or_default(),
                world_x: found.trigger.world_x.round() as i32,
                world_y: found.trigger.world_y.round() as i32,
                placements: 1,
                ifo_path: found.ifo_path,
            },
        );
    }
    Ok(sites.into_values().collect())
}

// ---------------------------------------------------------------------------
// Destination mapping file
// ---------------------------------------------------------------------------

/// Split `XtoY` into (`X`, `Y`) so `XtoY` can be paired with `YtoX`.
fn split_inverse(name: &str) -> Option<(String, String)> {
    let lower = name.to_ascii_lowercase();
    let idx = lower.find("to")?;
    if idx == 0 || idx + 2 >= lower.len() {
        return None;
    }
    Some((lower[..idx].to_string(), lower[idx + 2..].to_string()))
}

/// Emit an editable destination map, pre-filled where we can guess.
pub fn template(orphans: &[TriggerSite]) -> String {
    let by_name: BTreeMap<String, &TriggerSite> = orphans
        .iter()
        .map(|s| (s.name.to_ascii_lowercase(), s))
        .collect();

    let mut out = String::new();
    out.push_str("# Destinations for orphan map teleport triggers.\n");
    out.push_str("# One line per trigger:  <trigger>  <dest_zone_no>  <dest_x>  <dest_y>  [party]\n");
    out.push_str("# Coordinates are world units (cm) — the same scale shown below for each\n");
    out.push_str("# trigger's own event object, so \"send A to where B stands\" is a copy/paste.\n");
    out.push_str("# Add 'party' as a 5th field to warp the whole party. Lines starting with # are\n");
    out.push_str("# ignored, so a trigger you leave commented out is simply skipped.\n\n");

    for site in orphans {
        out.push_str(&format!(
            "# {} — {} placement(s) in zone {} ({}) at {} {}\n",
            site.name,
            site.placements,
            site.zone_no
                .map(|z| z.to_string())
                .unwrap_or_else(|| "?".into()),
            if site.zone_name.is_empty() {
                "unknown zone"
            } else {
                &site.zone_name
            },
            site.world_x,
            site.world_y,
        ));
        // If an exact inverse-named partner exists, its object is where this
        // trigger should land the player.
        let guess = split_inverse(&site.name)
            .map(|(a, b)| format!("{b}to{a}"))
            .and_then(|inv| by_name.get(&inv).copied())
            .filter(|partner| partner.zone_no.is_some());
        match guess {
            Some(partner) => out.push_str(&format!(
                "{}\t{}\t{}\t{}\t# -> where {} stands\n\n",
                site.name,
                partner.zone_no.unwrap(),
                partner.world_x,
                partner.world_y,
                partner.name,
            )),
            None => out.push_str(&format!(
                "# {}\t<dest_zone>\t<dest_x>\t<dest_y>\n\n",
                site.name
            )),
        }
    }
    out
}

/// Parse a destination map written from [`template`].
pub fn parse_map(text: &str) -> Result<BTreeMap<String, Destination>> {
    let mut out = BTreeMap::new();
    for (lineno, raw) in text.lines().enumerate() {
        let line = raw.split('#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        let f: Vec<&str> = line.split_whitespace().collect();
        if f.len() < 4 {
            bail!("line {}: expected '<trigger> <zone> <x> <y> [party]'", lineno + 1);
        }
        let parse = |s: &str, what: &str| -> Result<i32> {
            s.parse::<i32>()
                .with_context(|| format!("line {}: bad {what} '{s}'", lineno + 1))
        };
        let dest = Destination {
            zone_no: parse(f[1], "zone")?,
            x: parse(f[2], "x")?,
            y: parse(f[3], "y")?,
            party: f.get(4).is_some_and(|v| v.eq_ignore_ascii_case("party")),
        };
        if out.insert(f[0].to_ascii_lowercase(), dest).is_some() {
            bail!("line {}: duplicate entry for '{}'", lineno + 1, f[0]);
        }
    }
    Ok(out)
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

/// `REWD_007` — warp to `zone` at (`x`, `y`).
/// `STR_REWD_007 { u32 uiSize; i32 iType; i32 iZoneSN; i32 iX; i32 iY; u8 btPartyOpt; }`
/// — payload after the 8-byte entity header is 4+4+4+1 = 13, padded to 16.
fn rewd_warp(dest: Destination) -> Entity {
    let mut p = Vec::with_capacity(16);
    p.extend_from_slice(&dest.zone_no.to_le_bytes());
    p.extend_from_slice(&dest.x.to_le_bytes());
    p.extend_from_slice(&dest.y.to_le_bytes());
    p.push(u8::from(dest.party));
    p.extend_from_slice(&[0, 0, 0]);
    debug_assert_eq!(p.len(), 16);
    Entity {
        etype: 7,
        payload: p,
    }
}

fn warp_trigger(name: &str, dest: Destination) -> Trigger {
    let mut name_bytes = name.as_bytes().to_vec();
    name_bytes.push(0); // trigger names are stored NUL-terminated
    Trigger {
        check_next: 0,
        name: name_bytes,
        conditions: vec![], // always available: using the object is the gate
        rewards: vec![rewd_warp(dest)],
    }
}

fn empty_warp_qsd() -> QsdFile {
    QsdFile {
        size_field: 12, // constant across all retail QSDs
        description: b"map teleport triggers".to_vec(),
        patterns: vec![Pattern {
            name: b"warp triggers".to_vec(),
            triggers: vec![],
        }],
        trailing: vec![],
    }
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

/// Write (or update) the warp `.QSD` and register it in `LIST_QUESTDATA.STB`.
///
/// Only triggers present in `dests` are written. A destination naming a trigger
/// that is not an orphan, or a zone with no `.zon`, is an error — a typo there
/// would otherwise silently produce a trigger that hashes to nothing useful.
pub fn apply(
    root: &Path,
    orphans: &[TriggerSite],
    dests: &BTreeMap<String, Destination>,
    dry_run: bool,
) -> Result<Report> {
    let stb_dir = resolve_stb_dir(root)?;
    let questdata_dir = stb_dir
        .parent()
        .ok_or_else(|| anyhow!("STB dir has no parent"))?
        .join("QUESTDATA");
    let qsd_path = questdata_dir.join(WARP_QSD_FILENAME);
    let qsd_rel = format!("3DDATA\\QUESTDATA\\{WARP_QSD_FILENAME}");

    // --- validate ---
    if dests.is_empty() {
        bail!("destination map is empty — nothing to write");
    }
    let orphan_names: BTreeSet<String> = orphans
        .iter()
        .map(|s| s.name.to_ascii_lowercase())
        .collect();
    let zone = load_stb(&file_ci(&stb_dir, "LIST_ZONE.STB")?)?;
    let mut ours: BTreeSet<String> = BTreeSet::new();
    if qsd_path.exists() {
        for pattern in &QsdFile::read_file(&qsd_path)?.patterns {
            for t in &pattern.triggers {
                ours.insert(trigger_name(t).to_ascii_lowercase());
            }
        }
    }
    for (name, dest) in dests {
        if !orphan_names.contains(name) && !ours.contains(name) {
            bail!(
                "'{name}' is not an orphan trigger (no map event object references it, and this \
                 command did not define it) — check the spelling against --template"
            );
        }
        let row = zone
            .data
            .get(dest.zone_no as usize)
            .ok_or_else(|| anyhow!("'{name}': zone {} is not a LIST_ZONE row", dest.zone_no))?;
        if cell(row, ZONE_COL_FILE).trim().is_empty() {
            bail!("'{name}': zone {} has no .zon file", dest.zone_no);
        }
    }

    // --- build (merge into our own file if it already exists) ---
    let mut qsd = if qsd_path.exists() {
        QsdFile::read_file(&qsd_path)?
    } else {
        empty_warp_qsd()
    };
    let existed = qsd_path.exists();
    let mut changes = Vec::new();
    let mut added = 0usize;
    let mut updated = 0usize;
    let trigger_count;
    {
        let pattern = qsd
            .patterns
            .first_mut()
            .ok_or_else(|| anyhow!("{} has no pattern to write into", qsd_path.display()))?;
        for (name, dest) in dests {
            // Use the trigger's original casing from the map where we have it.
            let display_name = orphans
                .iter()
                .find(|s| s.name.eq_ignore_ascii_case(name))
                .map(|s| s.name.clone())
                .unwrap_or_else(|| name.clone());
            let built = warp_trigger(&display_name, *dest);
            match pattern
                .triggers
                .iter_mut()
                .find(|t| trigger_name(t).eq_ignore_ascii_case(name))
            {
                // Keep the name bytes already in the file: the game hashes
                // trigger names case-insensitively (`StrToHashKey` uppercases),
                // so rewriting the casing is churn, not a change.
                Some(existing) => {
                    if existing.rewards != built.rewards
                        || existing.conditions != built.conditions
                        || existing.check_next != built.check_next
                    {
                        existing.conditions = built.conditions;
                        existing.rewards = built.rewards;
                        existing.check_next = built.check_next;
                        updated += 1;
                    }
                }
                None => {
                    pattern.triggers.push(built);
                    added += 1;
                }
            }
            changes.push(format!(
                "trigger {display_name:<14} -> zone {} at ({}, {}){}",
                dest.zone_no,
                dest.x,
                dest.y,
                if dest.party { " [party]" } else { "" }
            ));
        }
        trigger_count = pattern.triggers.len();
    }

    let bytes = qsd.to_bytes();
    // Round-trip guard: what we are about to write must parse back identically.
    let reparsed = QsdFile::parse(&bytes).context("generated QSD does not parse back")?;
    if reparsed != qsd {
        bail!("generated QSD does not round-trip — refusing to write");
    }

    changes.push(format!(
        "{} {} ({} bytes, {trigger_count} trigger(s): {added} added, {updated} updated)",
        if existed { "UPDATE" } else { "CREATE" },
        qsd_path.display(),
        bytes.len(),
    ));

    // --- register in LIST_QUESTDATA (once) ---
    let qdata_path = file_ci(&stb_dir, "LIST_QUESTDATA.STB")?;
    let mut qdata = load_stb(&qdata_path)?;
    let registered = qdata
        .data
        .iter()
        .any(|r| cell(r, QDATA_COL_PATH).trim().eq_ignore_ascii_case(&qsd_rel));
    if !registered {
        let cols = qdata.data.first().map(Vec::len).unwrap_or(2);
        let mut row = empty_row(cols);
        set_cell(&mut row, 0, WARP_QSD_LABEL);
        set_cell(&mut row, QDATA_COL_PATH, &qsd_rel);
        qdata.data.push(row);
        changes.push(format!("APPEND LIST_QUESTDATA row -> \"{qsd_rel}\""));
    } else {
        changes.push(format!("LIST_QUESTDATA already registers \"{qsd_rel}\""));
    }

    let mut backups = Vec::new();
    if !dry_run {
        std::fs::create_dir_all(&questdata_dir)
            .with_context(|| format!("creating {}", questdata_dir.display()))?;
        if let Some(b) = backup_once(&qsd_path)? {
            backups.push(b);
        }
        std::fs::write(&qsd_path, &bytes)
            .with_context(|| format!("writing {}", qsd_path.display()))?;
        if !registered {
            if let Some(b) = backup_once(&qdata_path)? {
                backups.push(b);
            }
            write_stb(&mut qdata, &qdata_path)?;
        }
    }

    Ok(Report {
        dry_run,
        qsd_path,
        changes,
        backups,
    })
}
