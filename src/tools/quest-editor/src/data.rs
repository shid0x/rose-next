//! Phase 2 — game-data readers.
//!
//! Loads the STB tables the quest generator and (later) the UI pickers need:
//! monsters + quest-giver NPCs from `LIST_NPC.STB`, existing quests from
//! `LIST_Quest.STB`, and the item tables for Fetch targets / item rewards.
//!
//! ## roselib column convention (important)
//!
//! `roselib`'s `STB.data` keeps the STB **root column** at index 0, while the
//! game's `stb.cpp` strips it. So **C++ column N == roselib column N+1**. All the
//! column constants below are the C++ numbers from
//! `src/common/include/rose/io/stb.h`; we add 1 when indexing `row`.
//!
//! This mirrors the approach proven in `npc-shop-editor/src/data.rs`.

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{anyhow, Context, Result};
use roselib::files::STB;
use roselib::io::RoseFile;

// --- LIST_NPC.STB C++ column numbers (see stb.h) ---
const NPC_COL_NAME: usize = 0;
const NPC_COL_LEVEL: usize = 7;
const NPC_COL_AI_TYPE: usize = 16;
const NPC_COL_GIVE_EXP: usize = 17;
const NPC_COL_TYPE: usize = 27;
const NPC_COL_DEAD_EVENT: usize = 41;

/// NPC_TYPE value that marks a town/quest NPC (vs a monster).
const NPC_TYPE_TOWN_NPC: i32 = 999;

/// A killable monster (a `LIST_NPC` row that is not a town NPC).
#[derive(Debug, Clone)]
pub struct Monster {
    /// Game-side NPC id (the STB root-column label).
    pub id: i32,
    pub name: String,
    pub level: i32,
    pub give_exp: i32,
    /// roselib row index — the authoritative target when writing col-41 back.
    pub roselib_row: usize,
    /// Current `LIST_NPC` col-41 dead-event trigger name. Empty == we may own it
    /// for a hunt quest; non-empty == occupied (MVP refuses to overwrite).
    pub dead_event: String,
}

impl Monster {
    /// True if this monster's dead-event slot is free for us to claim.
    pub fn dead_event_is_free(&self) -> bool {
        self.dead_event.trim().is_empty()
    }
}

/// A town/quest NPC (potential quest giver). Not strictly needed for the
/// Hunt-first MVP (accept is driven by GM commands), but cheap to collect and
/// useful for the picker / future `.CON` work.
#[derive(Debug, Clone)]
pub struct GiverNpc {
    pub id: i32,
    pub name: String,
    pub roselib_row: usize,
}

/// An existing quest row from `LIST_Quest.STB`.
///
/// IMPORTANT: a quest's id/SN is its **row index**, not a stored label. The STB
/// root column is empty; the game uses `QUEST_NAME(I) = get_cstr(I, 0)` where `I`
/// is the quest SN used directly as the row index (verified: dead-event names
/// like `5033-32` reference quest 5033 = row 5033). QSD `COND_000.iQuestSN` /
/// `REWD_000.iQuestSN` and `Quest_Append(id)` all use this same row-index SN.
#[derive(Debug, Clone)]
pub struct QuestRow {
    /// Quest SN == row index in LIST_QUEST.STB.
    pub sn: i32,
    pub name: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ItemCategory {
    Face = 1,
    Cap = 2,
    Body = 3,
    Arms = 4,
    Foot = 5,
    Back = 6,
    Jewel = 7,
    Weapon = 8,
    SubWpn = 9,
    UseItem = 10,
    Gem = 11,
    Natural = 12,
    QuestItem = 13,
    Vehicle = 14,
}

impl ItemCategory {
    pub const ALL: &'static [ItemCategory] = &[
        ItemCategory::Face,
        ItemCategory::Cap,
        ItemCategory::Body,
        ItemCategory::Arms,
        ItemCategory::Foot,
        ItemCategory::Back,
        ItemCategory::Jewel,
        ItemCategory::Weapon,
        ItemCategory::SubWpn,
        ItemCategory::UseItem,
        ItemCategory::Gem,
        ItemCategory::Natural,
        ItemCategory::QuestItem,
        ItemCategory::Vehicle,
    ];

    pub fn stb_name(self) -> &'static str {
        match self {
            ItemCategory::Face => "LIST_FACEITEM.STB",
            ItemCategory::Cap => "LIST_CAP.STB",
            ItemCategory::Body => "LIST_BODY.STB",
            ItemCategory::Arms => "LIST_ARMS.STB",
            ItemCategory::Foot => "LIST_FOOT.STB",
            ItemCategory::Back => "LIST_BACK.STB",
            ItemCategory::Jewel => "LIST_JEWEL.STB",
            ItemCategory::Weapon => "LIST_WEAPON.STB",
            ItemCategory::SubWpn => "LIST_SUBWPN.STB",
            ItemCategory::UseItem => "LIST_USEITEM.STB",
            ItemCategory::Gem => "LIST_JEMITEM.STB",
            ItemCategory::Natural => "LIST_NATURAL.STB",
            ItemCategory::QuestItem => "LIST_QUESTITEM.STB",
            ItemCategory::Vehicle => "LIST_PAT.STB",
        }
    }

    pub fn display(self) -> &'static str {
        match self {
            ItemCategory::Face => "Face",
            ItemCategory::Cap => "Helmet",
            ItemCategory::Body => "Body",
            ItemCategory::Arms => "Gauntlet",
            ItemCategory::Foot => "Boots",
            ItemCategory::Back => "Back",
            ItemCategory::Jewel => "Jewel",
            ItemCategory::Weapon => "Weapon",
            ItemCategory::SubWpn => "Sub Weapon",
            ItemCategory::UseItem => "Consumable",
            ItemCategory::Gem => "Gem",
            ItemCategory::Natural => "Material",
            ItemCategory::QuestItem => "Quest Item",
            ItemCategory::Vehicle => "Vehicle Part",
        }
    }
}

/// `full = type * 1000 + id` — the legacy packed item encoding (citem.cpp).
/// NOTE: this cannot represent item ids > 999; the generator will use the
/// type/id pair form for high-numbered items (see CLAUDE.md item-encoding note).
pub fn encode_item_no(cat: ItemCategory, id: i32) -> i32 {
    (cat as i32) * 1000 + id
}

#[derive(Debug, Clone)]
pub struct Item {
    pub category: ItemCategory,
    pub id: i32,
    pub name: String,
}

pub struct ItemDb {
    pub by_category: HashMap<ItemCategory, Vec<Item>>,
}

impl ItemDb {
    pub fn lookup(&self, cat: ItemCategory, id: i32) -> Option<&Item> {
        self.by_category
            .get(&cat)
            .and_then(|v| v.iter().find(|it| it.id == id))
    }

    pub fn all(&self) -> impl Iterator<Item = &Item> {
        self.by_category.values().flat_map(|v| v.iter())
    }
}

/// The whole loaded data set the editor reads.
pub struct DataSet {
    pub root: PathBuf,
    pub stb_dir: PathBuf,

    pub monsters: Vec<Monster>,
    pub givers: Vec<GiverNpc>,
    /// Only the *named* (real) quest rows, for listing/search.
    pub quests: Vec<QuestRow>,
    /// Total row count of LIST_QUEST.STB (named + empty placeholders). The next
    /// appended quest gets SN == this value.
    pub quest_row_count: usize,
    pub item_db: ItemDb,
}

impl DataSet {
    pub fn load(root: &Path) -> Result<Self> {
        let stb_dir = resolve_stb_dir(root)?;

        let npc_stb = load_stb(&stb_dir, "LIST_NPC.STB").context("loading LIST_NPC.STB")?;
        let (monsters, givers) = collect_npcs(&npc_stb);

        let (quests, quest_row_count) = match load_stb(&stb_dir, "LIST_QUEST.STB") {
            Ok(stb) => (collect_quests(&stb), stb.data.len()),
            Err(e) => {
                eprintln!("warning: LIST_QUEST.STB not loaded: {e:#}");
                (Vec::new(), 0)
            }
        };

        let item_db = ItemDb {
            by_category: load_all_item_tables(&stb_dir),
        };

        Ok(Self {
            root: root.to_path_buf(),
            stb_dir,
            monsters,
            givers,
            quests,
            quest_row_count,
            item_db,
        })
    }

    /// SN for the next appended quest. Appending a row to LIST_QUEST.STB places
    /// it at index == current row count, so that's the new quest's SN. Appending
    /// at the end guarantees no collision with any existing quest.
    pub fn next_free_quest_sn(&self) -> i32 {
        self.quest_row_count as i32
    }

    pub fn find_monster(&self, id: i32) -> Option<&Monster> {
        self.monsters.iter().find(|m| m.id == id)
    }

    /// Next free quest-item id (in `LIST_QUESTITEM.STB`), for a new token item.
    /// Returns the bare id; encode with `encode_item_no(QuestItem, id)` for the SN.
    pub fn next_free_quest_item_id(&self) -> i32 {
        self.item_db
            .by_category
            .get(&ItemCategory::QuestItem)
            .and_then(|v| v.iter().map(|it| it.id).max())
            .unwrap_or(0)
            + 1
    }

    /// Token quest-item SN (`type*1000+id`) for a newly allocated token.
    pub fn next_free_token_item_sn(&self) -> i32 {
        encode_item_no(ItemCategory::QuestItem, self.next_free_quest_item_id())
    }

    /// Token SN for the `offset`-th newly allocated token of a single quest
    /// (offset 0 == `next_free_token_item_sn`). Ids beyond the current max are all
    /// free, so a multi-hunt quest gets distinct tokens by bumping the offset.
    pub fn token_item_sn_at(&self, offset: i32) -> i32 {
        encode_item_no(
            ItemCategory::QuestItem,
            self.next_free_quest_item_id() + offset,
        )
    }
}

fn collect_npcs(npc_stb: &STB) -> (Vec<Monster>, Vec<GiverNpc>) {
    let mut monsters = Vec::new();
    let mut givers = Vec::new();

    for (i, row) in npc_stb.data.iter().enumerate() {
        // C++ col N -> roselib col N+1.
        let col_i32 = |cpp_col: usize| -> i32 {
            row.get(cpp_col + 1)
                .and_then(|s| s.trim().parse::<i32>().ok())
                .unwrap_or(0)
        };
        let col_str =
            |cpp_col: usize| -> String { row.get(cpp_col + 1).cloned().unwrap_or_default() };

        let id = row
            .first()
            .and_then(|s| s.trim().parse::<i32>().ok())
            .unwrap_or(i as i32);
        let name = col_str(NPC_COL_NAME);
        // Skip empty/placeholder rows.
        if name.trim().is_empty() {
            continue;
        }

        let npc_type = col_i32(NPC_COL_TYPE);
        if npc_type == NPC_TYPE_TOWN_NPC {
            givers.push(GiverNpc {
                id,
                name,
                roselib_row: i,
            });
            continue;
        }

        // Treat the rest as monsters if they look killable (have a level or
        // grant EXP or have a combat AI). This filters out non-combat decor.
        let level = col_i32(NPC_COL_LEVEL);
        let give_exp = col_i32(NPC_COL_GIVE_EXP);
        let ai = col_i32(NPC_COL_AI_TYPE);
        if level <= 0 && give_exp <= 0 && ai <= 0 {
            continue;
        }

        monsters.push(Monster {
            id,
            name,
            level,
            give_exp,
            roselib_row: i,
            dead_event: col_str(NPC_COL_DEAD_EVENT),
        });
    }

    (monsters, givers)
}

fn collect_quests(stb: &STB) -> Vec<QuestRow> {
    let mut out = Vec::new();
    for (i, row) in stb.data.iter().enumerate() {
        // SN == row index. C++ col 0 (= roselib col 1) is the quest name.
        let name = row.get(1).cloned().unwrap_or_default();
        if name.trim().is_empty() {
            continue; // empty placeholder row
        }
        out.push(QuestRow { sn: i as i32, name });
    }
    out
}

fn load_all_item_tables(stb_dir: &Path) -> HashMap<ItemCategory, Vec<Item>> {
    let mut out: HashMap<ItemCategory, Vec<Item>> = HashMap::new();
    for &cat in ItemCategory::ALL {
        let stb = match load_stb(stb_dir, cat.stb_name()) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("warning: skipping {}: {e:#}", cat.stb_name());
                continue;
            }
        };
        let mut items = Vec::new();
        for (i, row) in stb.data.iter().enumerate() {
            let id = row
                .first()
                .and_then(|s| s.trim().parse::<i32>().ok())
                .unwrap_or(i as i32);
            let name = row.get(1).cloned().unwrap_or_default();
            if name.trim().is_empty() {
                continue;
            }
            items.push(Item {
                category: cat,
                id,
                name,
            });
        }
        out.insert(cat, items);
    }
    out
}

pub fn resolve_stb_dir(root: &Path) -> Result<PathBuf> {
    let candidates = [
        root.join("3DDATA").join("STB"),
        root.join("3ddata").join("stb"),
        root.join("STB"),
        root.to_path_buf(),
    ];
    for c in candidates.iter() {
        if file_ci(c, "LIST_NPC.STB").is_ok() {
            return Ok(c.clone());
        }
    }
    Err(anyhow!(
        "could not find 3DDATA/STB/LIST_NPC.STB under '{}'",
        root.display()
    ))
}

/// Locate `3DDATA/CONTROL/RES` (holds ITEM1.TSI + the icon DDS sheets).
pub fn resolve_icon_dir(root: &Path) -> Result<PathBuf> {
    for c in [
        root.join("3DDATA").join("CONTROL").join("RES"),
        root.join("3ddata").join("control").join("res"),
    ] {
        if c.exists() {
            return Ok(c);
        }
    }
    Err(anyhow!(
        "could not find 3DDATA/CONTROL/RES under '{}'",
        root.display()
    ))
}

/// Locate `3DDATA/MAPS` (the zone `.IFO`/`.ZON` tree).
pub fn resolve_maps_dir(root: &Path) -> Result<PathBuf> {
    for c in [
        root.join("3DDATA").join("MAPS"),
        root.join("3ddata").join("maps"),
        root.join("MAPS"),
    ] {
        if c.exists() {
            return Ok(c);
        }
    }
    Err(anyhow!(
        "could not find 3DDATA/MAPS under '{}'",
        root.display()
    ))
}

fn load_stb(dir: &Path, name: &str) -> Result<STB> {
    let path = file_ci(dir, name)?;
    STB::from_path(&path).map_err(|e| anyhow!("STB::from_path({}): {}", path.display(), e))
}

/// Case-insensitive file lookup in a directory.
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
