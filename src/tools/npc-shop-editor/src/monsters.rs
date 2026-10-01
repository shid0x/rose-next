//! GM monster catalog: every LIST_NPC row a tester can `/mon`, with the reasons
//! it would misbehave in game.
//!
//! Two independent sources of breakage are checked, each mirroring real code:
//!
//! * **The server's refusal rules.** `MobRowRefusalReason` in
//!   `sho_gameserver/src/cheatcmd.cpp` whispers "Spawn refused" for a blank name,
//!   an empty model column, level 0 or a junk HP column. `CZoneTHREAD::
//!   RegenCharacter` then drops `NPC_TYPE >= 900` (town NPCs) silently.
//! * **The client's model chain.** `CCharModelDATA::Load_MOBorNPC` reads
//!   `LIST_NPC.CHR` (skeleton, body parts, motions, bone effects per row),
//!   each part is an object of `PART_NPC.ZSC` (`CModelDATA::Load`), whose
//!   parts name a mesh and a material (texture). Weapon columns 5/6 index
//!   `LIST_WEAPON.ZSC` / `LIST_SUBWPN.ZSC`. Out-of-range indices are not
//!   bounds-checked on the client, so they are errors, not warnings.
//!
//! Nothing here is removed from the list: a broken row is still a row a tester
//! can type, and knowing why it is broken is the point.
use std::collections::{BTreeSet, HashMap};
use std::io::Cursor;

use anyhow::{bail, Context, Result};
use roselib::files::STB;
use roselib::io::RoseFile;

use crate::assets::Assets;
use crate::catalog::{english_names_with, RangeFilter};
use crate::text::{self, Codec};

pub const NPC_STB: &str = "3DDATA/STB/LIST_NPC.STB";
pub const NPC_STL: &str = "3DDATA/STB/LIST_NPC_S.STL";
pub const NPC_CHR: &str = "3DDATA/NPC/LIST_NPC.CHR";
pub const NPC_ZSC: &str = "3DDATA/NPC/PART_NPC.ZSC";
pub const WEAPON_ZSC: &str = "3DDATA/WEAPON/LIST_WEAPON.ZSC";
pub const SUBWPN_ZSC: &str = "3DDATA/WEAPON/LIST_SUBWPN.ZSC";
pub const AI_STB: &str = "3DDATA/STB/FILE_AI.STB";
/// Everything `MonsterCatalog::load` reads; the model files themselves are
/// only probed, so a tester package carries these plus a manifest.
pub const PACKAGE_TABLES: [&str; 7] = [
    NPC_STB, NPC_STL, NPC_CHR, NPC_ZSC, WEAPON_ZSC, SUBWPN_ZSC, AI_STB,
];

/// `cheatcmd.cpp` refuses anything above this ("real rows top out at 10,701").
const SERVER_MAX_HP: i32 = 20000;
/// `RegenCharacter` returns without spawning, and without a message.
const TOWN_NPC_TYPE: i32 = 900;
/// `Cheat_mon` clamps the count.
pub const MAX_SPAWN_COUNT: i32 = 100;

/// Mob motion slots (`MOB_ANI_*`, `common/shared/datatype.h`).
const MOTION_NAMES: [&str; 11] = [
    "idle", "walk", "attack", "hit", "death", "run", "cast 1", "skill 1", "cast 2", "skill 2",
    "extra",
];
/// A missing file in one of these slots is visible in every fight.
const CORE_MOTIONS: usize = 5;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Severity {
    Warning,
    Error,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Issue {
    pub severity: Severity,
    pub text: String,
}

pub struct Monster {
    pub id: i32,
    /// What the game prints over the monster, or the table name when the
    /// translation is missing.
    pub name: String,
    pub table_name: String,
    pub has_game_name: bool,
    pub level: i32,
    /// LIST_NPC column 8: HP *per level*. `max_hp` is what the monster spawns with.
    pub hp: i32,
    pub max_hp: i32,
    pub attack: i32,
    pub hit: i32,
    pub defense: i32,
    pub resistance: i32,
    pub avoid: i32,
    pub attack_speed: i32,
    pub walk_speed: i32,
    pub run_speed: i32,
    pub attack_range: i32,
    pub exp: i32,
    pub magic_damage: bool,
    pub npc_type: i32,
    pub ai: i32,
    pub issues: Vec<Issue>,
    /// Why `/mon` does nothing for this row, if it doesn't.
    pub spawn_blocker: Option<&'static str>,
    pub search_text: String,
}

impl Monster {
    pub fn town_npc(&self) -> bool {
        self.npc_type >= TOWN_NPC_TYPE
    }

    pub fn worst(&self) -> Option<Severity> {
        self.issues.iter().map(|i| i.severity).max()
    }

    pub fn command(&self, count: i32) -> Option<String> {
        if self.spawn_blocker.is_some() {
            return None;
        }
        Some(format!(
            "/mon {} {}",
            self.id,
            count.clamp(1, MAX_SPAWN_COUNT)
        ))
    }
}

pub struct MonsterCatalog {
    pub monsters: Vec<Monster>,
    pub warnings: Vec<String>,
    /// False when the source holds neither the model files nor a manifest of
    /// them, so "file missing" could not be told apart from "not shipped".
    pub file_checks: bool,
    /// Every referenced model file that exists: the packager's manifest.
    pub present_files: BTreeSet<String>,
}

impl MonsterCatalog {
    pub fn load(assets: &Assets) -> Result<Self> {
        Self::load_with(assets, Codec::Auto)
    }

    /// `codec`: how the tables' text is read (see `text::Codec`).
    pub fn load_with(assets: &Assets, codec: Codec) -> Result<Self> {
        let mut warnings = Vec::new();
        let table = read_stb(assets, NPC_STB, codec)?;
        let names = english_names_with(assets, NPC_STL, codec).unwrap_or_else(|e| {
            warnings.push(format!("Monster names use table names: {e:#}"));
            HashMap::new()
        });
        let chr = optional(assets, NPC_CHR, &mut warnings, Chr::parse);
        let parts = optional(assets, NPC_ZSC, &mut warnings, Zsc::parse);
        let weapons = optional(assets, WEAPON_ZSC, &mut warnings, Zsc::parse);
        let subweapons = optional(assets, SUBWPN_ZSC, &mut warnings, Zsc::parse);
        let ai = optional(assets, AI_STB, &mut warnings, |bytes| {
            let mut stb = STB::new();
            stb.read(&mut Cursor::new(bytes))
                .map_err(|e| anyhow::anyhow!("{e}"))?;
            Ok(stb)
        });
        let models = match (&chr, &parts) {
            (Some(chr), Some(parts)) => Some((chr, parts)),
            _ => {
                warnings.push(
                    "Model checks skipped: LIST_NPC.CHR or PART_NPC.ZSC is unavailable.".into(),
                );
                None
            }
        };

        let mut rows = Vec::new();
        for (id, row) in table.iter().enumerate().skip(1) {
            let Some(mut monster) = collect_row(id, row, &names) else {
                continue;
            };
            let mut checker = RowCheck::default();
            // A row the server refuses is never drawn; its refusal is the whole story.
            if monster.worst() != Some(Severity::Error) {
                if let Some((chr, parts)) = models {
                    checker.model(chr, parts, id, &monster);
                }
                let hands = [
                    (
                        number(row, 5),
                        &weapons,
                        "LIST_WEAPON.ZSC",
                        "Right-hand weapon",
                    ),
                    (
                        number(row, 6),
                        &subweapons,
                        "LIST_SUBWPN.ZSC",
                        "Left-hand weapon",
                    ),
                ];
                for (object, zsc, file, label) in hands {
                    if let (true, Some(zsc)) = (object > 0, zsc) {
                        checker.zsc_object(zsc, object, file, Severity::Warning, label);
                    }
                }
                if let (true, Some(ai)) = (monster.ai > 0, &ai) {
                    checker.ai(ai, monster.ai);
                }
            }
            monster.issues.append(&mut checker.issues);
            rows.push((monster, checker.files));
        }

        // Existence is probed once per path, then attributed to every row.
        let wanted: BTreeSet<&str> = rows
            .iter()
            .flat_map(|(_, files)| files.iter().map(|f| f.path.as_str()))
            .collect();
        let present_files: BTreeSet<String> = wanted
            .iter()
            .filter(|path| assets.exists(path))
            .map(|path| crate::assets::normalize(path))
            .collect();
        let file_checks = assets.is_packed() || assets.has_manifest() || !present_files.is_empty();
        if !file_checks && !wanted.is_empty() {
            warnings.push(
                "Missing-file checks skipped: this data folder has no model files and no asset \
                 manifest. Open the full data folder or the game's data.idx."
                    .into(),
            );
        }
        let monsters = rows
            .into_iter()
            .map(|(mut monster, files)| {
                if file_checks {
                    let mut seen = BTreeSet::new();
                    for file in files {
                        if !present_files.contains(&crate::assets::normalize(&file.path))
                            && seen.insert(file.text.clone())
                        {
                            monster.issues.push(Issue {
                                severity: file.severity,
                                text: file.text,
                            });
                        }
                    }
                }
                monster.issues.sort_by(|a, b| b.severity.cmp(&a.severity));
                monster
            })
            .collect();
        Ok(Self {
            monsters,
            warnings,
            file_checks,
            present_files,
        })
    }
}

fn read_stb(assets: &Assets, path: &str, codec: Codec) -> Result<Vec<Vec<String>>> {
    text::read_stb(&assets.read(path)?, codec).map_err(|e| anyhow::anyhow!("{path}: {e}"))
}

fn optional<T>(
    assets: &Assets,
    path: &str,
    warnings: &mut Vec<String>,
    parse: impl FnOnce(&[u8]) -> Result<T>,
) -> Option<T> {
    match assets.read(path).and_then(|bytes| parse(&bytes)) {
        Ok(value) => Some(value),
        Err(e) => {
            warnings.push(format!("{path}: {e:#}"));
            None
        }
    }
}

/// Game column `col` (roselib keeps the root label in 0).
fn cell(row: &[String], col: usize) -> &str {
    row.get(col + 1).map_or("", |s| s.trim())
}

fn number(row: &[String], col: usize) -> i32 {
    cell(row, col).parse().unwrap_or(0)
}

fn collect_row(id: usize, row: &[String], names: &crate::catalog::Names) -> Option<Monster> {
    let table_name = cell(row, 0).to_string();
    let key = cell(row, 40);
    let game_name = (!key.is_empty())
        .then(|| names.get(key))
        .flatten()
        .map(|(name, _)| name.trim().to_string())
        .filter(|name| !name.is_empty());
    let model = cell(row, 1);
    // A row with no name anywhere and no model is not a monster, it is padding.
    if table_name.is_empty() && game_name.is_none() && model.is_empty() {
        return None;
    }
    let level = number(row, 7);
    let hp = number(row, 8);
    let npc_type = number(row, 27);
    let mut issues = Vec::new();
    let refusal = if table_name.is_empty() {
        Some("The server refuses /mon: the row has no table name.")
    } else if model.is_empty() {
        Some("The server refuses /mon: the model column is empty (retail placeholder).")
    } else if level <= 0 {
        Some("The server refuses /mon: level 0 (retail placeholder).")
    } else if hp > SERVER_MAX_HP {
        Some("The server refuses /mon: the HP column is junk.")
    } else {
        None
    };
    if let Some(reason) = refusal {
        issues.push(Issue {
            severity: Severity::Error,
            text: reason.into(),
        });
    }
    if game_name.is_none() && refusal.is_none() {
        issues.push(Issue {
            severity: Severity::Warning,
            text: if key.is_empty() {
                "No in-game name: the STL key column is empty, so the name is blank in game.".into()
            } else {
                format!("No in-game name: {key} is missing from LIST_NPC_S.STL, so the name is blank in game.")
            },
        });
    }
    let name = game_name
        .clone()
        .or_else(|| (!table_name.is_empty()).then(|| table_name.clone()))
        .unwrap_or_else(|| format!("Unnamed #{id}"));
    let search_text = format!(
        "{name} {table_name} {id} lv{level} {}",
        if npc_type >= TOWN_NPC_TYPE {
            "npc"
        } else {
            "monster"
        }
    )
    .to_lowercase();
    // Not a defect: /mon silently does nothing for a town NPC.
    let spawn_blocker = refusal.or((npc_type >= TOWN_NPC_TYPE)
        .then_some("Town NPC (type 900+): the server ignores /mon for these rows."));
    Some(Monster {
        id: id as i32,
        name,
        table_name,
        has_game_name: game_name.is_some(),
        level,
        hp,
        // Level x HP, as CObjMOB::Init and the client both compute it.
        max_hp: (i64::from(level) * i64::from(hp)).clamp(0, i64::from(i32::MAX)) as i32,
        attack: number(row, 9),
        hit: number(row, 10),
        defense: number(row, 11),
        resistance: number(row, 12),
        avoid: number(row, 13),
        attack_speed: number(row, 14),
        walk_speed: number(row, 2),
        run_speed: number(row, 3),
        attack_range: number(row, 26),
        exp: number(row, 17),
        magic_damage: number(row, 15) != 0,
        npc_type,
        ai: number(row, 16),
        issues,
        spawn_blocker,
        search_text,
    })
}

struct FileCheck {
    path: String,
    severity: Severity,
    text: String,
}

#[derive(Default)]
struct RowCheck {
    issues: Vec<Issue>,
    files: Vec<FileCheck>,
}

impl RowCheck {
    fn error(&mut self, text: String) {
        self.push(Severity::Error, text);
    }

    fn push(&mut self, severity: Severity, text: String) {
        if !self.issues.iter().any(|i| i.text == text) {
            self.issues.push(Issue { severity, text });
        }
    }

    fn file(&mut self, path: &str, severity: Severity, what: &str) {
        self.files.push(FileCheck {
            path: path.to_string(),
            severity,
            text: format!("{what} missing: {path}"),
        });
    }

    fn model(&mut self, chr: &Chr, parts: &Zsc, id: usize, monster: &Monster) {
        let Some(Some(model)) = chr.models.get(id) else {
            self.error("No model in LIST_NPC.CHR: invisible and unclickable in game.".into());
            return;
        };
        match chr
            .skeletons
            .get(model.skeleton as usize)
            .filter(|_| model.skeleton >= 0)
        {
            Some(path) => self.file(path, Severity::Error, "Skeleton"),
            None => self.error(format!(
                "Skeleton index {} is outside LIST_NPC.CHR's {} skeletons.",
                model.skeleton,
                chr.skeletons.len()
            )),
        }
        if model.parts.is_empty() {
            self.error("The model has no body parts: invisible and unclickable in game.".into());
        }
        for &object in &model.parts {
            self.zsc_object(parts, object, "PART_NPC.ZSC", Severity::Error, "");
        }
        let mut slots = [false; MOTION_NAMES.len()];
        for &(slot, motion) in &model.motions {
            if slot < 0 {
                continue;
            }
            let name = MOTION_NAMES
                .get(slot as usize)
                .copied()
                .unwrap_or("unknown");
            if let Some(seen) = slots.get_mut(slot as usize) {
                *seen = true;
            }
            match chr.motions.get(motion as usize).filter(|_| motion >= 0) {
                Some(path) => {
                    let severity = if (slot as usize) < CORE_MOTIONS {
                        Severity::Error
                    } else {
                        Severity::Warning
                    };
                    self.file(path, severity, &format!("Animation ({name})"));
                }
                None => self.error(format!(
                    "Animation ({name}) index {motion} is outside LIST_NPC.CHR's {} motions.",
                    chr.motions.len()
                )),
            }
        }
        if !slots[0] {
            self.error("No idle animation: the model cannot stand still in game.".into());
        }
        if !monster.town_npc() {
            for (slot, what) in [(1, "walk"), (2, "attack"), (4, "death")] {
                if !slots[slot] {
                    self.push(Severity::Warning, format!("No {what} animation."));
                }
            }
        }
        for &(_, effect) in &model.effects {
            match chr.effects.get(effect as usize).filter(|_| effect >= 0) {
                Some(path) => self.file(path, Severity::Warning, "Body effect"),
                None => self.error(format!(
                    "Body effect index {effect} is outside LIST_NPC.CHR's {} effects.",
                    chr.effects.len()
                )),
            }
        }
    }

    /// One ZSC object and everything its parts name. `severity` is what a
    /// defect costs: a body part is the monster, a weapon only a prop.
    /// `label` prefixes every message ("Right-hand weapon"), empty for the body.
    fn zsc_object(&mut self, zsc: &Zsc, object: i32, file: &str, severity: Severity, label: &str) {
        let what = |thing: &str| {
            if label.is_empty() {
                thing.to_string()
            } else {
                let mut chars = thing.chars();
                let first = chars.next().map(|c| c.to_ascii_lowercase());
                format!(
                    "{label}: {}{}",
                    first.into_iter().collect::<String>(),
                    chars.as_str()
                )
            }
        };
        let Some(model) = zsc.objects.get(object as usize).filter(|_| object >= 0) else {
            self.push(
                severity,
                what(&format!(
                    "Model {object} is outside {file}'s {} objects.",
                    zsc.objects.len()
                )),
            );
            return;
        };
        if model.parts.is_empty() {
            self.push(
                severity,
                what(&format!(
                    "Model {object} of {file} is empty: nothing to draw."
                )),
            );
        }
        for part in &model.parts {
            match zsc
                .meshes
                .get(part.mesh as usize)
                .filter(|_| part.mesh >= 0)
            {
                Some(path) => self.file(path, severity, &what("Mesh")),
                None => self.push(
                    Severity::Error,
                    what(&format!(
                        "Mesh index {} is outside {file}'s {} meshes.",
                        part.mesh,
                        zsc.meshes.len()
                    )),
                ),
            }
            match zsc
                .materials
                .get(part.material as usize)
                .filter(|_| part.material >= 0)
            {
                Some(path) => self.file(path, severity, &what("Texture")),
                None => self.push(
                    Severity::Error,
                    what(&format!(
                        "Texture index {} is outside {file}'s {} materials.",
                        part.material,
                        zsc.materials.len()
                    )),
                ),
            }
            for path in &part.animations {
                self.file(path, Severity::Warning, &what("Mesh animation"));
            }
        }
    }

    fn ai(&mut self, ai: &STB, index: i32) {
        let path = ai.data.get(index as usize).map_or("", |row| cell(row, 0));
        if path.is_empty() {
            self.push(
                Severity::Warning,
                format!("AI row {index} is blank in FILE_AI.STB: the monster stands still and never fights back."),
            );
        } else {
            self.files.push(FileCheck {
                path: path.to_string(),
                severity: Severity::Warning,
                text: format!(
                    "AI script missing: {path} (the monster stands still and never fights back)"
                ),
            });
        }
    }
}

/// Little-endian reader that fails instead of panicking on truncated input.
struct Reader<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, pos: 0 }
    }

    fn take(&mut self, n: usize) -> Result<&'a [u8]> {
        let end = self.pos.checked_add(n).filter(|&e| e <= self.bytes.len());
        let Some(end) = end else {
            bail!("truncated at byte {}", self.pos);
        };
        let slice = &self.bytes[self.pos..end];
        self.pos = end;
        Ok(slice)
    }

    fn u8(&mut self) -> Result<u8> {
        Ok(self.take(1)?[0])
    }

    fn i16(&mut self) -> Result<i16> {
        Ok(i16::from_le_bytes(self.take(2)?.try_into().unwrap()))
    }

    fn count(&mut self) -> Result<usize> {
        let n = self.i16()?;
        if n < 0 {
            bail!("negative count {n} at byte {}", self.pos - 2);
        }
        Ok(n as usize)
    }

    /// `CGameStr::ReadString`: NUL-terminated, quotes dropped.
    fn cstring(&mut self) -> Result<String> {
        let rest = &self.bytes[self.pos..];
        let Some(len) = rest.iter().position(|&b| b == 0) else {
            bail!("unterminated string at byte {}", self.pos);
        };
        self.pos += len + 1;
        Ok(String::from_utf8_lossy(&rest[..len])
            .replace('"', "")
            .trim()
            .to_string())
    }

    fn strings(&mut self) -> Result<Vec<String>> {
        (0..self.count()?).map(|_| self.cstring()).collect()
    }
}

/// `LIST_NPC.CHR`, as `CCharModelDATA::Load_MOBorNPC` reads it.
pub struct Chr {
    pub skeletons: Vec<String>,
    pub motions: Vec<String>,
    pub effects: Vec<String>,
    /// Indexed by LIST_NPC row; `None` where the valid flag is 0.
    pub models: Vec<Option<ChrModel>>,
}

pub struct ChrModel {
    pub skeleton: i16,
    pub parts: Vec<i32>,
    /// (motion slot, motion file index)
    pub motions: Vec<(i16, i16)>,
    /// (bone, effect file index)
    pub effects: Vec<(i16, i16)>,
}

impl Chr {
    pub fn parse(bytes: &[u8]) -> Result<Self> {
        let mut r = Reader::new(bytes);
        let skeletons = r.strings().context("skeleton list")?;
        let motions = r.strings().context("motion list")?;
        let effects = r.strings().context("effect list")?;
        let count = r.count()?;
        let mut models = Vec::with_capacity(count);
        for index in 0..count {
            let model = (|| -> Result<Option<ChrModel>> {
                if r.u8()? == 0 {
                    return Ok(None);
                }
                let skeleton = r.i16()?;
                let _name = r.cstring()?;
                let parts = (0..r.count()?)
                    .map(|_| r.i16().map(i32::from))
                    .collect::<Result<_>>()?;
                let motions = (0..r.count()?)
                    .map(|_| Ok((r.i16()?, r.i16()?)))
                    .collect::<Result<_>>()?;
                let effects = (0..r.count()?)
                    .map(|_| Ok((r.i16()?, r.i16()?)))
                    .collect::<Result<_>>()?;
                Ok(Some(ChrModel {
                    skeleton,
                    parts,
                    motions,
                    effects,
                }))
            })()
            .with_context(|| format!("model {index}"))?;
            models.push(model);
        }
        Ok(Self {
            skeletons,
            motions,
            effects,
            models,
        })
    }
}

/// The parts of a `.ZSC` that name files, read the way `CModelDATA::Load`
/// does: every part property is tag + length + payload, so unknown ones skip.
pub struct Zsc {
    pub meshes: Vec<String>,
    pub materials: Vec<String>,
    pub objects: Vec<ZscObject>,
}

pub struct ZscObject {
    pub parts: Vec<ZscPart>,
}

pub struct ZscPart {
    pub mesh: i16,
    pub material: i16,
    pub animations: Vec<String>,
}

/// `SWITCH_ANI` .. `SWITCH_ANI + MAX_MESH_ANI_TYPE` in `client/io_model.cpp`.
const ZSC_ANIMATION_TAGS: std::ops::Range<u8> = 8..29;

impl Zsc {
    pub fn parse(bytes: &[u8]) -> Result<Self> {
        let mut r = Reader::new(bytes);
        let meshes = r.strings().context("mesh list")?;
        let material_count = r.count()?;
        let mut materials = Vec::with_capacity(material_count);
        for _ in 0..material_count {
            materials.push(r.cstring()?);
            // skin, alpha, two-sided, alpha test/ref, z test/write, blend,
            // specular (9 x i16), alpha (f32), glow type (i16), glow (3 x f32)
            r.take(9 * 2 + 4 + 2 + 12)?;
        }
        let _effects = r.strings().context("effect list")?;
        let object_count = r.count()?;
        let mut objects = Vec::with_capacity(object_count);
        for index in 0..object_count {
            let object = (|| -> Result<ZscObject> {
                r.take(12)?; // cylinder radius, x, y
                let part_count = r.count()?;
                if part_count == 0 {
                    return Ok(ZscObject { parts: Vec::new() });
                }
                let mut parts = Vec::with_capacity(part_count);
                for _ in 0..part_count {
                    let mesh = r.i16()?;
                    let material = r.i16()?;
                    let mut animations = Vec::new();
                    loop {
                        let tag = r.u8()?;
                        if tag == 0 {
                            break;
                        }
                        let len = r.u8()? as usize;
                        let payload = r.take(len)?;
                        if ZSC_ANIMATION_TAGS.contains(&tag) {
                            let end = payload
                                .iter()
                                .position(|&b| b == 0)
                                .unwrap_or(payload.len());
                            let path = String::from_utf8_lossy(&payload[..end]).trim().to_string();
                            if !path.is_empty() {
                                animations.push(path);
                            }
                        }
                    }
                    parts.push(ZscPart {
                        mesh,
                        material,
                        animations,
                    });
                }
                for _ in 0..r.count()? {
                    r.take(4)?; // effect index, effect type
                    loop {
                        let tag = r.u8()?;
                        if tag == 0 {
                            break;
                        }
                        let len = r.u8()? as usize;
                        r.take(len)?;
                    }
                }
                r.take(24)?; // bounding box
                Ok(ZscObject { parts })
            })()
            .with_context(|| format!("object {index}"))?;
            objects.push(object);
        }
        Ok(Self {
            meshes,
            materials,
            objects,
        })
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StatusFilter {
    All,
    Clean,
    Problems,
    Broken,
}

impl StatusFilter {
    pub const ALL: [(Self, &'static str); 4] = [
        (Self::All, "All rows"),
        (Self::Clean, "No problems"),
        (Self::Problems, "Any problem"),
        (Self::Broken, "Broken only"),
    ];

    pub fn label(self) -> &'static str {
        Self::ALL.iter().find(|(key, _)| *key == self).unwrap().1
    }
}

#[derive(Clone, PartialEq)]
pub struct MonsterFilter {
    pub search: String,
    pub level: RangeFilter,
    pub hp: RangeFilter,
    pub status: StatusFilter,
    pub include_town_npcs: bool,
}

impl Default for MonsterFilter {
    fn default() -> Self {
        Self {
            search: String::new(),
            level: RangeFilter::default(),
            hp: RangeFilter::default(),
            status: StatusFilter::All,
            include_town_npcs: false,
        }
    }
}

impl MonsterFilter {
    pub fn matches(&self, monster: &Monster) -> bool {
        let status = match self.status {
            StatusFilter::All => true,
            StatusFilter::Clean => monster.issues.is_empty(),
            StatusFilter::Problems => !monster.issues.is_empty(),
            StatusFilter::Broken => monster.worst() == Some(Severity::Error),
        };
        status
            && (self.include_town_npcs || !monster.town_npc())
            && self.search.to_lowercase().split_whitespace().all(|word| {
                // "#123" is an exact ID; any other word is a substring.
                match word.strip_prefix('#').map(str::parse::<i32>) {
                    Some(Ok(id)) => monster.id == id,
                    _ => monster.search_text.contains(word),
                }
            })
            && self.level.matches(Some(monster.level))
            && self.hp.matches(Some(monster.max_hp))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cstr(out: &mut Vec<u8>, s: &str) {
        out.extend_from_slice(s.as_bytes());
        out.push(0);
    }

    fn i16s(out: &mut Vec<u8>, values: &[i16]) {
        for v in values {
            out.extend_from_slice(&v.to_le_bytes());
        }
    }

    fn fixture_chr() -> Vec<u8> {
        let mut b = Vec::new();
        for list in [
            &["skel.zmd"][..],
            &["idle.zmo", "walk.zmo"][..],
            &["aura.eft"][..],
        ] {
            i16s(&mut b, &[list.len() as i16]);
            for s in list {
                cstr(&mut b, s);
            }
        }
        i16s(&mut b, &[3]);
        b.push(0); // row 0: invalid
        b.push(1); // row 1
        i16s(&mut b, &[0]);
        cstr(&mut b, "name");
        i16s(&mut b, &[2, 0, 7]); // parts 0 and 7
        i16s(&mut b, &[2, 0, 0, 1, 9]); // idle -> 0, walk -> 9 (out of range)
        i16s(&mut b, &[1, 4, 0]); // bone 4 -> aura
        b.push(1); // row 2: no parts, no motions
        i16s(&mut b, &[0]);
        cstr(&mut b, "");
        i16s(&mut b, &[0, 0, 0]);
        b
    }

    fn fixture_zsc() -> Vec<u8> {
        let mut b = Vec::new();
        i16s(&mut b, &[1]);
        cstr(&mut b, "3DDATA\\NPC\\X\\BODY.ZMS");
        i16s(&mut b, &[1]);
        cstr(&mut b, "3DDATA\\NPC\\X\\..\\X\\BODY.DDS");
        b.extend_from_slice(&[0; 9 * 2 + 4 + 2 + 12]);
        i16s(&mut b, &[1]);
        cstr(&mut b, "3DDATA\\EFFECT\\SPARK.EFT");
        i16s(&mut b, &[2]);
        // object 0: one part with a bone index and a mesh animation
        b.extend_from_slice(&[0; 12]);
        i16s(&mut b, &[1, 0, 0]);
        b.extend_from_slice(&[5, 2, 3, 0]);
        b.extend_from_slice(&[8, 8]);
        b.extend_from_slice(b"TAIL.ZMO");
        b.push(0);
        i16s(&mut b, &[1, 0, 0]); // one effect point
        b.extend_from_slice(&[1, 12]);
        b.extend_from_slice(&[0; 12]);
        b.push(0);
        b.extend_from_slice(&[0; 24]);
        // object 1: empty
        b.extend_from_slice(&[0; 12]);
        i16s(&mut b, &[0]);
        b
    }

    #[test]
    fn parses_chr_and_zsc_like_the_client() {
        let chr = Chr::parse(&fixture_chr()).unwrap();
        assert_eq!(chr.motions.len(), 2);
        assert!(chr.models[0].is_none());
        let row = chr.models[1].as_ref().unwrap();
        assert_eq!(row.parts, [0, 7]);
        assert_eq!(row.motions, [(0, 0), (1, 9)]);
        assert_eq!(row.effects, [(4, 0)]);
        let zsc = Zsc::parse(&fixture_zsc()).unwrap();
        assert_eq!(zsc.objects.len(), 2);
        assert_eq!(zsc.objects[0].parts[0].animations, ["TAIL.ZMO"]);
        assert!(zsc.objects[1].parts.is_empty());
        let mut truncated = fixture_zsc();
        truncated.pop();
        assert!(Zsc::parse(&truncated).is_err());
        assert!(Chr::parse(&fixture_chr()[..20]).is_err());
    }

    fn stb_row(cells: &[(usize, &str)]) -> Vec<String> {
        let mut row = vec![String::new(); 46];
        for &(col, value) in cells {
            row[col + 1] = value.into();
        }
        row
    }

    #[test]
    fn model_chain_reports_every_broken_link_and_mirrors_server_refusals() {
        let chr = Chr::parse(&fixture_chr()).unwrap();
        let zsc = Zsc::parse(&fixture_zsc()).unwrap();
        let names = HashMap::from([("KEY".to_string(), ("Jelly Bean".to_string(), String::new()))]);
        let good = stb_row(&[(0, "jelly"), (1, "x"), (7, "5"), (8, "100"), (40, "KEY")]);
        let monster = collect_row(1, &good, &names).unwrap();
        assert_eq!(monster.name, "Jelly Bean");
        assert_eq!(monster.command(250).as_deref(), Some("/mon 1 100"));
        let mut check = RowCheck::default();
        check.model(&chr, &zsc, 1, &monster);
        let texts: Vec<&str> = check.issues.iter().map(|i| i.text.as_str()).collect();
        assert!(
            texts
                .iter()
                .any(|t| t.contains("Model 7 is outside PART_NPC.ZSC")),
            "{texts:?}"
        );
        assert!(
            texts.iter().any(|t| t.contains("Animation (walk) index 9")),
            "{texts:?}"
        );
        assert!(
            texts.iter().any(|t| t.contains("No attack animation")),
            "{texts:?}"
        );
        let files: Vec<&str> = check.files.iter().map(|f| f.path.as_str()).collect();
        assert!(files.contains(&"3DDATA\\NPC\\X\\..\\X\\BODY.DDS"));
        assert!(files.contains(&"TAIL.ZMO"));
        let mut empty = RowCheck::default();
        empty.model(&chr, &zsc, 2, &monster);
        assert!(empty
            .issues
            .iter()
            .any(|i| i.text.contains("no body parts")));
        let mut missing = RowCheck::default();
        missing.model(&chr, &zsc, 0, &monster);
        assert!(missing.issues[0].text.contains("No model in LIST_NPC.CHR"));

        for (cells, reason) in [
            (vec![(1, "x"), (7, "5"), (40, "KEY")], "no table name"),
            (vec![(0, "a"), (7, "5")], "model column is empty"),
            (vec![(0, "a"), (1, "x")], "level 0"),
            (
                vec![(0, "a"), (1, "x"), (7, "5"), (8, "7900000")],
                "HP column is junk",
            ),
        ] {
            let row = collect_row(9, &stb_row(&cells), &names).unwrap();
            assert!(row.command(1).is_none());
            assert_eq!(row.worst(), Some(Severity::Error));
            assert!(row.issues[0].text.contains(reason), "{:?}", row.issues);
        }
        let town_row = stb_row(&[(0, "a"), (1, "x"), (7, "5"), (27, "999"), (40, "KEY")]);
        let town = collect_row(9, &town_row, &names).unwrap();
        assert!(town.issues.is_empty() && town.command(1).is_none());
        assert!(collect_row(9, &stb_row(&[]), &names).is_none());
    }

    #[test]
    fn filter_supports_exact_ids_and_hides_town_npcs_by_default() {
        let names = HashMap::new();
        let monster =
            collect_row(123, &stb_row(&[(0, "Bat"), (1, "x"), (7, "12")]), &names).unwrap();
        let mut filter = MonsterFilter::default();
        filter.search = "bat #123".into();
        assert!(filter.matches(&monster));
        filter.search = "#12".into();
        assert!(!filter.matches(&monster));
        filter.search = "12".into();
        assert!(filter.matches(&monster));
        filter.search.clear();
        filter.status = StatusFilter::Broken;
        assert!(!filter.matches(&monster)); // only a missing-name warning
        filter.status = StatusFilter::Problems;
        assert!(filter.matches(&monster));
        let town_row = stb_row(&[(0, "Judy"), (1, "x"), (7, "1"), (27, "999")]);
        let town = collect_row(5, &town_row, &names).unwrap();
        filter.status = StatusFilter::All;
        assert!(!filter.matches(&town));
        filter.include_town_npcs = true;
        assert!(filter.matches(&town));
    }

    /// The tester package ships tables only; its manifest must reproduce the
    /// full tree's verdicts exactly, and a file left out of it must be reported.
    #[test]
    #[ignore = "requires the workspace's extracted data"]
    fn workspace_package_manifest_matches_full_data() {
        use std::path::Path;
        let data = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../data");
        let full = MonsterCatalog::load(&Assets::open(&data).unwrap()).unwrap();
        let package = tempfile::tempdir().unwrap();
        for table in PACKAGE_TABLES
            .iter()
            .chain(["3DDATA/STB/LIST_WEAPON.STB"].iter())
        {
            let target = package.path().join(table);
            std::fs::create_dir_all(target.parent().unwrap()).unwrap();
            std::fs::copy(data.join(table), target).unwrap();
        }
        let manifest = package.path().join(crate::assets::MANIFEST_NAME);
        let verdicts = |catalog: &MonsterCatalog| -> Vec<(i32, Vec<Issue>)> {
            catalog
                .monsters
                .iter()
                .map(|m| (m.id, m.issues.clone()))
                .collect()
        };
        std::fs::write(
            &manifest,
            crate::assets::write_manifest(full.present_files.iter().cloned()),
        )
        .unwrap();
        let packaged = MonsterCatalog::load(&Assets::open(package.path()).unwrap()).unwrap();
        assert!(packaged.warnings.is_empty(), "{:?}", packaged.warnings);
        assert!(verdicts(&packaged) == verdicts(&full));

        let dropped = full
            .present_files
            .iter()
            .find(|p| p.contains("JELLYBEAN1") && p.ends_with(".DDS"))
            .unwrap()
            .clone();
        let rest = full
            .present_files
            .iter()
            .filter(|p| **p != dropped)
            .cloned();
        std::fs::write(&manifest, crate::assets::write_manifest(rest)).unwrap();
        let packaged = MonsterCatalog::load(&Assets::open(package.path()).unwrap()).unwrap();
        let jelly = packaged.monsters.iter().find(|m| m.id == 1).unwrap();
        println!("{} -> {:#?}", jelly.name, jelly.issues);
        assert_eq!(jelly.worst(), Some(Severity::Error));
        assert!(jelly.issues[0].text.starts_with("Texture missing: "));
    }

    #[test]
    #[ignore = "requires the workspace's extracted data and Exes archives"]
    fn workspace_monster_survey() {
        use std::path::Path;
        let repo = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
        let mut roots = vec![repo.join("data"), repo.join("Exes")];
        if let Some(extra) = std::env::var_os("ROSE_GM_CATALOG_TEST_ROOT") {
            roots.push(extra.into());
        }
        for root in roots {
            let assets = Assets::open(&root).unwrap();
            let catalog = MonsterCatalog::load(&assets).unwrap();
            println!("== {} ==", root.display());
            println!("warnings: {:#?}", catalog.warnings);
            assert!(catalog.file_checks);
            let rows = &catalog.monsters;
            let town = rows.iter().filter(|m| m.town_npc()).count();
            let broken = rows
                .iter()
                .filter(|m| m.worst() == Some(Severity::Error))
                .count();
            let warned = rows
                .iter()
                .filter(|m| m.worst() == Some(Severity::Warning))
                .count();
            println!(
                "{} rows ({town} town NPCs): {broken} broken, {warned} warnings only, {} files present",
                rows.len(),
                catalog.present_files.len()
            );
            let mut kinds: std::collections::BTreeMap<String, (usize, Vec<i32>)> =
                Default::default();
            for m in rows {
                for issue in &m.issues {
                    let kind = issue.text.split([':', '(']).next().unwrap().to_string();
                    let entry = kinds
                        .entry(format!("{:?} {kind}", issue.severity))
                        .or_default();
                    entry.0 += 1;
                    if entry.1.len() < 8 {
                        entry.1.push(m.id);
                    }
                }
            }
            for (kind, (count, ids)) in kinds {
                println!("{count:5}  {kind}  e.g. {ids:?}");
            }
            for id in [253, 372, 411, 562, 1456, 1431, 2293, 2725, 230] {
                if let Some(m) = rows.iter().find(|m| m.id == id) {
                    println!("#{id} {} -> {:#?}", m.name, m.issues);
                }
            }
            assert!(rows.len() > 1000);
        }
    }
}
