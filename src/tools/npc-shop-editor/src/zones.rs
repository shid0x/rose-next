//! LIST_ZONE.STB + IFO reader.
//!
//! Each row in LIST_ZONE.STB points at a `.ZON` file. The zone's map tiles
//! live in the same directory as the `.ZON` (files named `<nX>_<nY>.IFO`).
//! Each IFO has a list of lumps; `LUMP_TERRAIN_MOB = 2` holds both NPC and
//! monster spawns, distinguished later by `NPC_TYPE(iObjID) == 999`.
//!
//! The binary layout for each spawn entry in LUMP_TERRAIN_MOB is documented
//! in `src/sho_gameserver/src/zonefile.cpp:111-282`.

use std::collections::{BTreeMap, HashSet};
use std::fs;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};

use anyhow::{anyhow, Context, Result};
use byteorder::{LittleEndian, ReadBytesExt};
use roselib::files::STB;
use roselib::io::RoseFile;

const LUMP_TERRAIN_MOB: i32 = 2;

#[derive(Debug, Clone)]
pub struct Zone {
    pub id: i32,
    pub name: String,
    /// Every NPC/monster object id spawned inside this zone's map tiles.
    /// We don't pre-filter to shopkeepers here so that the caller can reuse
    /// the same NPC filtering logic as the flat list.
    pub spawn_ids: HashSet<i32>,
}

/// Load every zone listed in `LIST_ZONE.STB`, resolve its `.ZON` file relative
/// to `root`, and parse every sibling `.IFO` for NPC/monster spawns.
pub fn load_zones(root: &Path, stb_dir: &Path) -> Result<Vec<Zone>> {
    let zone_stb = load_stb(stb_dir, "LIST_ZONE.STB").context("loading LIST_ZONE.STB")?;

    let mut zones = Vec::new();
    for (i, row) in zone_stb.data.iter().enumerate() {
        // roselib col 0 = root label; C++ col N = roselib col N+1.
        let id = row
            .get(0)
            .and_then(|s| s.parse::<i32>().ok())
            .unwrap_or(i as i32);
        // C++ col 0 = zone name. C++ col 1 = .ZON file path.
        let name = row.get(1).cloned().unwrap_or_default();
        let zon_rel = row.get(2).cloned().unwrap_or_default();
        if name.is_empty() || zon_rel.is_empty() {
            continue;
        }

        let zon_path = resolve_path_ci(root, &zon_rel);
        let zone_dir = match zon_path.as_ref().and_then(|p| p.parent().map(|q| q.to_path_buf())) {
            Some(d) => d,
            None => continue,
        };

        let spawn_ids = match scan_zone_dir(&zone_dir) {
            Ok(ids) => ids,
            Err(e) => {
                log::warn!("zone {} ({}): {}", id, name, e);
                HashSet::new()
            }
        };

        zones.push(Zone {
            id,
            name,
            spawn_ids,
        });
    }

    Ok(zones)
}

fn scan_zone_dir(dir: &Path) -> Result<HashSet<i32>> {
    let mut ids: HashSet<i32> = HashSet::new();
    let entries = fs::read_dir(dir).with_context(|| format!("reading {}", dir.display()))?;
    for entry in entries {
        let entry = entry?;
        let path = entry.path();
        if !path.is_file() {
            continue;
        }
        let is_ifo = path
            .extension()
            .and_then(|e| e.to_str())
            .map(|e| e.eq_ignore_ascii_case("ifo"))
            .unwrap_or(false);
        if !is_ifo {
            continue;
        }
        match parse_ifo_mob_lump(&path) {
            Ok(list) => ids.extend(list),
            Err(e) => {
                log::warn!("parsing {}: {}", path.display(), e);
            }
        }
    }
    Ok(ids)
}

fn parse_ifo_mob_lump(path: &Path) -> Result<Vec<i32>> {
    let bytes = fs::read(path)?;
    let mut cur = Cursor::new(&bytes);

    let lump_count = cur.read_i32::<LittleEndian>()?;
    // Read lump directory, remember only LUMP_TERRAIN_MOB offsets.
    let mut mob_offsets = Vec::new();
    for _ in 0..lump_count {
        let ty = cur.read_i32::<LittleEndian>()?;
        let off = cur.read_i32::<LittleEndian>()? as u64;
        if ty == LUMP_TERRAIN_MOB {
            mob_offsets.push(off);
        }
    }

    let mut out = Vec::new();
    for off in mob_offsets {
        cur.set_position(off);
        let obj_cnt = cur.read_i32::<LittleEndian>()?;
        for _ in 0..obj_cnt {
            let obj = read_mob_object(&mut cur)?;
            out.push(obj);
        }
    }
    Ok(out)
}

/// Read one LUMP_TERRAIN_MOB entry and return its NPC row id.
///
/// Mirrors `ReadObjINFO` + the `LUMP_TERRAIN_MOB` branch in
/// `zonefile.cpp:111` — each entry has a variable-length common header
/// followed by MOB-specific fields (`iAI`, AI-file name).
fn read_mob_object(cur: &mut Cursor<&Vec<u8>>) -> Result<i32> {
    skip_pascal_string(cur)?; // event/object name
    let _warp_id = cur.read_i16::<LittleEndian>()?;
    let _event_id = cur.read_i16::<LittleEndian>()?;
    let _obj_type = cur.read_i32::<LittleEndian>()?;
    let obj_id = cur.read_i32::<LittleEndian>()?;
    let _map_x = cur.read_i32::<LittleEndian>()?;
    let _map_y = cur.read_i32::<LittleEndian>()?;
    // quaternion (4 floats) + position (3 floats) + scale (3 floats)
    skip_bytes(cur, 4 * 10)?;

    // MOB-specific: iAI + AI file name (pascal)
    let _ai = cur.read_i32::<LittleEndian>()?;
    skip_pascal_string(cur)?;

    Ok(obj_id)
}

fn skip_pascal_string(cur: &mut Cursor<&Vec<u8>>) -> Result<()> {
    let len = cur.read_u8()? as usize;
    let pos = cur.position() as usize;
    let data = cur.get_ref();
    if pos + len > data.len() {
        return Err(anyhow!("pascal string truncated"));
    }
    cur.set_position((pos + len) as u64);
    Ok(())
}

fn skip_bytes(cur: &mut Cursor<&Vec<u8>>, n: usize) -> Result<()> {
    let mut buf = vec![0u8; n];
    cur.read_exact(&mut buf)?;
    Ok(())
}

fn load_stb(dir: &Path, name: &str) -> Result<STB> {
    let path = find_case_insensitive(dir, name)?;
    STB::from_path(&path).map_err(|e| anyhow!("STB::from_path({}): {}", path.display(), e))
}

fn find_case_insensitive(dir: &Path, name: &str) -> Result<PathBuf> {
    let direct = dir.join(name);
    if direct.exists() {
        return Ok(direct);
    }
    for entry in fs::read_dir(dir).with_context(|| format!("reading {}", dir.display()))? {
        let entry = entry?;
        if entry.file_name().to_string_lossy().eq_ignore_ascii_case(name) {
            return Ok(entry.path());
        }
    }
    Err(anyhow!("file not found: {}/{}", dir.display(), name))
}

/// Walk a `/`- or `\`-delimited relative path under `root`, matching each
/// segment case-insensitively.
fn resolve_path_ci(root: &Path, rel: &str) -> Option<PathBuf> {
    let clean = rel.replace('\\', "/");
    let segments: Vec<&str> = clean.split('/').filter(|s| !s.is_empty()).collect();
    let mut cur = root.to_path_buf();
    for seg in segments {
        let direct = cur.join(seg);
        if direct.exists() {
            cur = direct;
            continue;
        }
        let entries = match fs::read_dir(&cur) {
            Ok(e) => e,
            Err(_) => return None,
        };
        let mut found = None;
        for entry in entries.flatten() {
            if entry.file_name().to_string_lossy().eq_ignore_ascii_case(seg) {
                found = Some(entry.path());
                break;
            }
        }
        match found {
            Some(p) => cur = p,
            None => return None,
        }
    }
    Some(cur)
}

/// Group a list of already-loaded NPCs by zone so the sidebar can render a
/// "Zone → shopkeepers" tree. NPCs that don't spawn in any loaded zone fall
/// under the `None` key so the caller can surface them in an "Other" bucket.
pub fn group_npcs_by_zone<'a>(
    zones: &'a [Zone],
    shopkeeper_ids: &[i32],
) -> BTreeMap<Option<i32>, Vec<i32>> {
    let shopkeeper_set: HashSet<i32> = shopkeeper_ids.iter().copied().collect();
    let mut out: BTreeMap<Option<i32>, Vec<i32>> = BTreeMap::new();
    let mut placed: HashSet<i32> = HashSet::new();
    for z in zones {
        let mut bucket: Vec<i32> = z
            .spawn_ids
            .iter()
            .copied()
            .filter(|id| shopkeeper_set.contains(id))
            .collect();
        if bucket.is_empty() {
            continue;
        }
        bucket.sort_unstable();
        for id in &bucket {
            placed.insert(*id);
        }
        out.insert(Some(z.id), bucket);
    }
    let mut leftover: Vec<i32> = shopkeeper_ids
        .iter()
        .copied()
        .filter(|id| !placed.contains(id))
        .collect();
    leftover.sort_unstable();
    leftover.dedup();
    if !leftover.is_empty() {
        out.insert(None, leftover);
    }
    out
}
