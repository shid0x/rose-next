//! Phase 2b — zone `.IFO` reader/editor for wiring an NPC to a conversation.
//!
//! Each map-tile `.IFO` holds a lump directory then lump bodies. `LUMP_TERRAIN_MOB`
//! (type 2) holds NPC/monster placements. Each placement record (mirroring
//! `zonefile.cpp` `ReadObjINFO` + the MOB branch) is:
//!
//! ```text
//!   pascal object-name
//!   i16 warp_id, i16 event_id, i32 obj_type, i32 obj_id,
//!   i32 map_x, i32 map_y, 40 bytes (quat 4f + pos 3f + scale 3f),
//!   i32 iAI, pascal conversation-name   <- the field the server matches to
//!                                          LIST_EVENT to set the NPC's m_nQuestIDX
//! ```
//!
//! Wiring an NPC = rewriting that last pascal. Because it's variable length, the
//! edit splices the bytes and shifts every lump-directory offset that points past
//! the edit by the size delta.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};

const LUMP_TERRAIN_MOB: i32 = 2;

#[derive(Debug, Clone)]
pub struct Placement {
    pub npc_id: i32,
    pub conversation: String,
    /// Byte range `[start, end)` of the conversation pascal (len byte + chars).
    pub conv_range: (usize, usize),
}

#[derive(Debug, Clone)]
pub struct FoundPlacement {
    pub ifo_path: PathBuf,
    pub npc_id: i32,
    pub conversation: String,
}

fn rd_i16(b: &[u8], o: usize) -> Result<i16> {
    b.get(o..o + 2)
        .map(|s| i16::from_le_bytes([s[0], s[1]]))
        .with_context(|| format!("i16 past EOF at {o}"))
}
fn rd_i32(b: &[u8], o: usize) -> Result<i32> {
    b.get(o..o + 4)
        .map(|s| i32::from_le_bytes([s[0], s[1], s[2], s[3]]))
        .with_context(|| format!("i32 past EOF at {o}"))
}

/// Parse one MOB record at `pos`, returning the placement and the offset just
/// past the record.
fn parse_record(b: &[u8], pos: usize) -> Result<(Placement, usize)> {
    let mut p = pos;
    // object-name pascal
    let name_len = *b.get(p).context("record name len past EOF")? as usize;
    p += 1 + name_len;
    let _warp = rd_i16(b, p)?;
    let _event = rd_i16(b, p + 2)?;
    let _ty = rd_i32(b, p + 4)?;
    let npc_id = rd_i32(b, p + 8)?;
    p += 12; // warp(2)+event(2)+type(4)+objId(4)
    p += 8; // map_x + map_y
    p += 40; // quat + pos + scale
    p += 4; // iAI
            // conversation-name pascal
    let conv_start = p;
    let conv_len = *b.get(p).context("conversation len past EOF")? as usize;
    p += 1;
    let conv = b
        .get(p..p + conv_len)
        .map(|s| String::from_utf8_lossy(s).into_owned())
        .with_context(|| format!("conversation name past EOF at {p}"))?;
    p += conv_len;
    Ok((
        Placement {
            npc_id,
            conversation: conv,
            conv_range: (conv_start, p),
        },
        p,
    ))
}

/// All NPC/monster placements in an `.IFO`'s MOB lumps.
pub fn parse_placements(b: &[u8]) -> Result<Vec<Placement>> {
    let lump_count = rd_i32(b, 0)?;
    if lump_count < 0 || lump_count > 4096 {
        bail!("implausible lump count {lump_count}");
    }
    let mut mob_offsets = Vec::new();
    for i in 0..lump_count as usize {
        let ty = rd_i32(b, 4 + i * 8)?;
        let off = rd_i32(b, 4 + i * 8 + 4)? as usize;
        if ty == LUMP_TERRAIN_MOB {
            mob_offsets.push(off);
        }
    }
    let mut out = Vec::new();
    for off in mob_offsets {
        let obj_cnt = rd_i32(b, off)?;
        let mut p = off + 4;
        for _ in 0..obj_cnt.max(0) {
            let (placement, next) = parse_record(b, p)?;
            out.push(placement);
            p = next;
        }
    }
    Ok(out)
}

/// Set the conversation name of the first record matching `npc_id`. Returns the
/// rewritten file bytes, or `None` if the NPC isn't in this IFO. Splices the
/// pascal and fixes up every lump-directory offset that points past the edit.
pub fn set_conversation(b: &[u8], npc_id: i32, new_name: &str) -> Result<Option<Vec<u8>>> {
    let placements = parse_placements(b)?;
    let Some(target) = placements.iter().find(|p| p.npc_id == npc_id) else {
        return Ok(None);
    };
    let (start, end) = target.conv_range;

    // New pascal: 1 length byte + name (names are short; cap at 255).
    let name_bytes = new_name.as_bytes();
    if name_bytes.len() > 255 {
        bail!("conversation name too long ({} bytes)", name_bytes.len());
    }
    let mut pascal = Vec::with_capacity(1 + name_bytes.len());
    pascal.push(name_bytes.len() as u8);
    pascal.extend_from_slice(name_bytes);

    let delta = pascal.len() as i64 - (end - start) as i64;

    let mut out = Vec::with_capacity((b.len() as i64 + delta) as usize);
    out.extend_from_slice(&b[..start]);
    out.extend_from_slice(&pascal);
    out.extend_from_slice(&b[end..]);

    // Fix up lump-directory offsets that point past the edit point.
    if delta != 0 {
        let lump_count = rd_i32(&out, 0)?;
        for i in 0..lump_count as usize {
            let off_pos = 4 + i * 8 + 4;
            let off = rd_i32(&out, off_pos)?;
            if (off as usize) > start {
                let patched = (off as i64 + delta) as i32;
                out[off_pos..off_pos + 4].copy_from_slice(&patched.to_le_bytes());
            }
        }
    }
    Ok(Some(out))
}

/// Recursively collect every `*.ifo` under `dir`.
pub fn collect_ifo_files(dir: &Path) -> Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        let rd = match std::fs::read_dir(&d) {
            Ok(rd) => rd,
            Err(_) => continue,
        };
        for entry in rd.flatten() {
            let p = entry.path();
            if p.is_dir() {
                stack.push(p);
            } else if p
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("ifo"))
            {
                out.push(p);
            }
        }
    }
    out.sort();
    Ok(out)
}

/// Every placement of `npc_id` across all zone `.IFO`s under `root`.
pub fn find_npc(root: &Path, npc_id: i32) -> Result<Vec<FoundPlacement>> {
    let mut out = Vec::new();
    for path in collect_ifo_files(root)? {
        let bytes = match std::fs::read(&path) {
            Ok(b) => b,
            Err(_) => continue,
        };
        let placements = match parse_placements(&bytes) {
            Ok(p) => p,
            Err(_) => continue, // skip IFOs we can't parse
        };
        for p in placements {
            if p.npc_id == npc_id {
                out.push(FoundPlacement {
                    ifo_path: path.clone(),
                    npc_id,
                    conversation: p.conversation,
                });
            }
        }
    }
    Ok(out)
}

/// (npc_id, conversation) for every placement that already has a conversation,
/// across all `.IFO`s under `root`. Deduplicated, sorted by npc id.
pub fn placements_with_conversation(root: &Path) -> Result<Vec<(i32, String)>> {
    let mut set: BTreeSet<(i32, String)> = BTreeSet::new();
    for path in collect_ifo_files(root)? {
        if let Ok(bytes) = std::fs::read(&path) {
            if let Ok(placements) = parse_placements(&bytes) {
                for p in placements {
                    if !p.conversation.trim().is_empty() {
                        set.insert((p.npc_id, p.conversation));
                    }
                }
            }
        }
    }
    Ok(set.into_iter().collect())
}

/// One-pass scan for the wizard's giver picker: every placed NPC id, plus
/// (npc_id, conversation) for placements that already have a dialog. Walks the
/// tree and reads/parses each `.IFO` exactly once (the separate
/// `all_placed_npc_ids` + `placements_with_conversation` calls each did the
/// full walk on their own, doubling the cost).
pub fn scan_npc_placements(root: &Path) -> Result<(BTreeSet<i32>, Vec<(i32, String)>)> {
    let mut ids = BTreeSet::new();
    let mut convs: BTreeSet<(i32, String)> = BTreeSet::new();
    for path in collect_ifo_files(root)? {
        if let Ok(bytes) = std::fs::read(&path) {
            if let Ok(placements) = parse_placements(&bytes) {
                for p in placements {
                    ids.insert(p.npc_id);
                    if !p.conversation.trim().is_empty() {
                        convs.insert((p.npc_id, p.conversation));
                    }
                }
            }
        }
    }
    Ok((ids, convs.into_iter().collect()))
}

/// NPC ids that have at least one placement, across all `.IFO`s under `root`.
pub fn all_placed_npc_ids(root: &Path) -> Result<BTreeSet<i32>> {
    let mut out = BTreeSet::new();
    for path in collect_ifo_files(root)? {
        if let Ok(bytes) = std::fs::read(&path) {
            if let Ok(placements) = parse_placements(&bytes) {
                for p in placements {
                    out.insert(p.npc_id);
                }
            }
        }
    }
    Ok(out)
}

/// Rewrite one IFO so `npc_id`'s placement uses `new_name`. Backs up to `.bak`.
/// Re-parses afterward to confirm the edit is clean. Returns false if the NPC
/// isn't in this IFO.
pub fn wire_npc_in_ifo(ifo: &Path, npc_id: i32, new_name: &str, dry_run: bool) -> Result<bool> {
    let bytes = std::fs::read(ifo).with_context(|| format!("reading {}", ifo.display()))?;
    let before = parse_placements(&bytes)?;
    let Some(new_bytes) = set_conversation(&bytes, npc_id, new_name)? else {
        return Ok(false);
    };

    // Self-check: re-parse and confirm only the target changed.
    let after =
        parse_placements(&new_bytes).context("rewritten IFO failed to re-parse (edit aborted)")?;
    if before.len() != after.len() {
        bail!("rewritten IFO has a different placement count (edit aborted)");
    }
    let mut changed = 0;
    for (a, b2) in before.iter().zip(after.iter()) {
        if a.npc_id != b2.npc_id {
            bail!("rewritten IFO desynced npc ids (edit aborted)");
        }
        if a.conversation != b2.conversation {
            changed += 1;
        }
    }
    let applied = after
        .iter()
        .find(|p| p.npc_id == npc_id)
        .map(|p| p.conversation.as_str());
    // `changed` is 0 when already set (idempotent) or 1 on a real change.
    if applied != Some(new_name) || changed > 1 {
        bail!("rewritten IFO did not apply the conversation cleanly (edit aborted)");
    }

    if !dry_run {
        let bak = ifo.with_extension("ifo.bak");
        if !bak.exists() {
            std::fs::copy(ifo, &bak)
                .with_context(|| format!("backing up {} -> {}", ifo.display(), bak.display()))?;
        }
        std::fs::write(ifo, &new_bytes).with_context(|| format!("writing {}", ifo.display()))?;
    }
    Ok(true)
}
