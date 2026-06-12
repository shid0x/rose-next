use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{anyhow, Context, Result};
use roselib::files::STB;
use roselib::io::RoseFile;

/// Item category = which STB the item lives in.
/// The numeric value is the ROSE item type, which is also the encoding used
/// by the game: full_item_no = type * 1000 + id (see common/shared/citem.cpp).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
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

    pub fn stb_name(&self) -> &'static str {
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

    pub fn display(&self) -> &'static str {
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

/// Decode a store slot value stored in LIST_SELL.STB into (type, id).
/// Encoding: `full = type * 1000 + id` (see citem.cpp:80-81).
pub fn decode_item_no(full: i32) -> Option<(ItemCategory, i32)> {
    if full <= 1000 {
        return None;
    }
    let ty = full / 1000;
    let id = full % 1000;
    let cat = ItemCategory::ALL.iter().find(|c| **c as i32 == ty)?;
    Some((*cat, id))
}

pub fn encode_item_no(cat: ItemCategory, id: i32) -> i32 {
    (cat as i32) * 1000 + id
}

#[derive(Debug, Clone)]
pub struct Item {
    pub category: ItemCategory,
    pub id: i32,
    pub name: String,
    pub icon_no: i32,
}

#[derive(Debug, Clone)]
pub struct Npc {
    pub id: i32,
    pub name: String,
    /// Roselib row index this NPC was read from in LIST_NPC.STB. Used as the
    /// authoritative target when writing edits back — don't trust `id`, which
    /// is just the root-column label.
    pub roselib_row: usize,
    /// The four columns [21,22,23,24] of LIST_NPC.STB. 0 = no tab.
    pub shop_tab_rows: [i32; 4],
}

impl Npc {
    pub fn has_shop(&self) -> bool {
        self.shop_tab_rows.iter().any(|r| *r > 0)
    }
}

/// A single shop tab (row of LIST_SELL.STB).
#[derive(Debug, Clone)]
pub struct ShopTab {
    pub row: usize, // 1-based row index in LIST_SELL.STB
    pub name: String,
    pub items: Vec<i32>, // full encoded item numbers, fixed length 48
    pub dirty: bool,
}

pub const SHOP_TAB_SLOT_COUNT: usize = 48;

/// The whole loaded workspace.
pub struct DataSet {
    pub root: PathBuf,

    pub npc_stb: STB,
    pub sell_stb: STB,

    pub npcs: Vec<Npc>,
    pub shop_tabs: HashMap<usize, ShopTab>, // row -> tab; filled lazily on edit
    /// ref_count[row] = how many NPC tab-refs point at LIST_SELL row `row`
    pub tab_ref_counts: HashMap<usize, usize>,
    pub item_db: ItemDb,
    pub zones: Vec<crate::zones::Zone>,

    pub any_npc_dirty: bool,
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

impl DataSet {
    pub fn load(root: &Path) -> Result<Self> {
        let stb_dir = resolve_stb_dir(root)?;

        let npc_stb =
            load_stb(&stb_dir, "LIST_NPC.STB").context("loading LIST_NPC.STB")?;
        let sell_stb =
            load_stb(&stb_dir, "LIST_SELL.STB").context("loading LIST_SELL.STB")?;

        let npcs = collect_npcs(&npc_stb);
        let tab_ref_counts = count_tab_refs(&npcs);
        let item_db = ItemDb {
            by_category: load_all_item_tables(&stb_dir)?,
        };

        let zones = match crate::zones::load_zones(root, &stb_dir) {
            Ok(z) => z,
            Err(e) => {
                log::warn!("zone load failed: {}", e);
                Vec::new()
            }
        };

        Ok(Self {
            root: root.to_path_buf(),
            npc_stb,
            sell_stb,
            npcs,
            shop_tabs: HashMap::new(),
            tab_ref_counts,
            item_db,
            zones,
            any_npc_dirty: false,
        })
    }

    /// Load (and cache) a shop tab from LIST_SELL.STB.
    ///
    /// Note: LIST_NPC's shop-tab columns store the *roselib data index*
    /// directly (matching the C++ server, which uses 0-based indexing into
    /// its already-header-stripped data), so no `row - 1` adjustment.
    pub fn get_or_load_tab(&mut self, row: usize) -> Option<&mut ShopTab> {
        if row == 0 || row >= self.sell_stb.data.len() {
            return None;
        }
        if !self.shop_tabs.contains_key(&row) {
            let cells = &self.sell_stb.data[row];
            // C++ col 0 (= roselib col 1) is the raw tab name
            // (e.g. "Soldier Skill"). C++ col 2..49 = item slots → roselib 3..50.
            let name = cells.get(1).cloned().unwrap_or_default();
            let mut items = Vec::with_capacity(SHOP_TAB_SLOT_COUNT);
            for slot in 0..SHOP_TAB_SLOT_COUNT {
                // C++ col 2+T → roselib col 3+T
                let col = 2 + 1 + slot;
                let v = cells
                    .get(col)
                    .and_then(|s| s.parse::<i32>().ok())
                    .unwrap_or(0);
                items.push(v);
            }
            self.shop_tabs.insert(
                row,
                ShopTab {
                    row,
                    name,
                    items,
                    dirty: false,
                },
            );
        }
        self.shop_tabs.get_mut(&row)
    }

    pub fn ref_count(&self, row: usize) -> usize {
        *self.tab_ref_counts.get(&row).unwrap_or(&0)
    }

    /// Begin a mutation on `(npc_idx, slot)`. If the referenced tab is shared
    /// with other NPCs, create a copy (append new row to LIST_SELL.STB) and
    /// retarget this NPC to the new row.
    ///
    /// Returns the effective tab row that should now be edited.
    pub fn begin_tab_edit(&mut self, npc_idx: usize, tab_slot: usize) -> Result<usize> {
        let npc = self
            .npcs
            .get_mut(npc_idx)
            .ok_or_else(|| anyhow!("invalid npc index"))?;
        let row = npc.shop_tab_rows[tab_slot];
        if row <= 0 {
            return Err(anyhow!("npc has no shop tab at slot {}", tab_slot));
        }
        let row = row as usize;
        let refs = *self.tab_ref_counts.get(&row).unwrap_or(&1);

        if refs <= 1 {
            return Ok(row);
        }

        // Copy-on-write: append a new row cloning the source.
        let source = self.sell_stb.data[row].clone();
        self.sell_stb.data.push(source);
        let new_row = self.sell_stb.data.len() - 1;

        // Retarget npc
        npc.shop_tab_rows[tab_slot] = new_row as i32;
        self.any_npc_dirty = true;

        // Update ref counts
        *self.tab_ref_counts.entry(row).or_insert(1) -= 1;
        self.tab_ref_counts.insert(new_row, 1);

        // Migrate cached tab (if any) to the new row so pending edits follow
        if let Some(mut tab) = self.shop_tabs.remove(&row) {
            tab.row = new_row;
            tab.dirty = true;
            self.shop_tabs.insert(new_row, tab);
        } else {
            // Force-load the new tab so callers see the fresh copy.
            let _ = self.get_or_load_tab(new_row);
        }

        Ok(new_row)
    }

    /// Apply any pending in-memory tab edits back into `sell_stb.data`, then
    /// write both STBs to disk. Backs up the originals to `.bak` first.
    pub fn save(&mut self) -> Result<()> {
        let stb_dir = resolve_stb_dir(&self.root)?;
        let sell_path = stb_dir.join("LIST_SELL.STB");
        let npc_path = stb_dir.join("LIST_NPC.STB");

        // Apply tab edits back into the STB cells.
        for tab in self.shop_tabs.values() {
            if !tab.dirty {
                continue;
            }
            let data_row = tab.row;
            while self.sell_stb.data.len() <= data_row {
                // Shouldn't happen, but keep bounds safe.
                self.sell_stb.data.push(Vec::new());
            }
            let row_cells = &mut self.sell_stb.data[data_row];
            // +1 offset everywhere: roselib includes the root column, C++ skips it.
            while row_cells.len() < 3 + SHOP_TAB_SLOT_COUNT {
                row_cells.push(String::from("0"));
            }
            // Tab name is stored inline in roselib col 1 (= C++ root col 0 via
            // STORE_NAME). Writing it back keeps edits from being silently lost.
            if row_cells.len() > 1 {
                row_cells[1] = tab.name.clone();
            }
            for (slot, v) in tab.items.iter().enumerate() {
                row_cells[3 + slot] = v.to_string();
            }
        }

        // Apply NPC shop-tab-row edits back into LIST_NPC.STB.
        if self.any_npc_dirty {
            for npc in &self.npcs {
                let data_row = npc.roselib_row;
                if data_row >= self.npc_stb.data.len() {
                    continue;
                }
                let cells = &mut self.npc_stb.data[data_row];
                // C++ cols 21..24 → roselib cols 22..25 (root column offset).
                for (i, col) in [22, 23, 24, 25].iter().enumerate() {
                    if cells.len() > *col {
                        cells[*col] = npc.shop_tab_rows[i].to_string();
                    }
                }
            }
        }

        backup_once(&sell_path)?;
        backup_once(&npc_path)?;

        self.sell_stb
            .write_to_path(&sell_path)
            .map_err(|e| anyhow!("writing LIST_SELL.STB: {}", e))?;
        self.npc_stb
            .write_to_path(&npc_path)
            .map_err(|e| anyhow!("writing LIST_NPC.STB: {}", e))?;

        // Mark all tabs clean post-save.
        for tab in self.shop_tabs.values_mut() {
            tab.dirty = false;
        }
        self.any_npc_dirty = false;
        Ok(())
    }
}

pub fn resolve_stb_dir(root: &Path) -> Result<PathBuf> {
    // Accept both "root/3DDATA/STB" and "root" (if the user picked 3DDATA itself).
    let candidates = [
        root.join("3DDATA").join("STB"),
        root.join("3ddata").join("stb"),
        root.join("STB"),
        root.to_path_buf(),
    ];
    for c in candidates.iter() {
        if c.join("LIST_NPC.STB").exists() || c.join("list_npc.stb").exists() {
            return Ok(c.clone());
        }
    }
    Err(anyhow!(
        "could not find 3DDATA/STB/LIST_NPC.STB under '{}'",
        root.display()
    ))
}

pub fn resolve_icon_dir(root: &Path) -> Result<PathBuf> {
    let candidates = [
        root.join("3DDATA").join("CONTROL").join("RES"),
        root.join("3ddata").join("control").join("res"),
    ];
    for c in candidates.iter() {
        if c.exists() {
            return Ok(c.clone());
        }
    }
    Err(anyhow!(
        "could not find 3DDATA/CONTROL/RES under '{}'",
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
    let lower = name.to_ascii_lowercase();
    let upper = name.to_ascii_uppercase();
    for candidate in [dir.join(&lower), dir.join(&upper)] {
        if candidate.exists() {
            return Ok(candidate);
        }
    }
    for entry in fs::read_dir(dir).context("reading directory")? {
        let entry = entry?;
        let fname = entry.file_name();
        let fname_str = fname.to_string_lossy();
        if fname_str.eq_ignore_ascii_case(name) {
            return Ok(entry.path());
        }
    }
    Err(anyhow!("file not found: {}/{}", dir.display(), name))
}

fn collect_npcs(npc_stb: &STB) -> Vec<Npc> {
    let mut out = Vec::new();
    for (i, row) in npc_stb.data.iter().enumerate() {
        // roselib keeps the STB's root column at index 0, while the game's
        // `stb.cpp` skips it — so C++ column N maps to roselib column N+1.
        let col_i32 = |cpp_col: usize| -> i32 {
            row.get(cpp_col + 1)
                .and_then(|s| s.parse::<i32>().ok())
                .unwrap_or(0)
        };
        // NPC_TYPE (C++ col 27) — only type 999 is a real NPC.
        if col_i32(27) != 999 {
            continue;
        }
        // The root label doubles as the game-side NPC ID.
        let id = row
            .get(0)
            .and_then(|s| s.parse::<i32>().ok())
            .unwrap_or(i as i32);
        // C++ col 0 (= roselib col 1) is the raw display name for this build
        // (e.g. "[Weapon Seller] Raffle"). No STL lookup required.
        let name = row
            .get(1)
            .cloned()
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| format!("NPC {}", id));
        let shop_tab_rows = [col_i32(21), col_i32(22), col_i32(23), col_i32(24)];
        out.push(Npc {
            id,
            name,
            roselib_row: i,
            shop_tab_rows,
        });
    }
    out
}

fn count_tab_refs(npcs: &[Npc]) -> HashMap<usize, usize> {
    let mut counts: HashMap<usize, usize> = HashMap::new();
    for npc in npcs {
        for r in npc.shop_tab_rows.iter() {
            if *r > 0 {
                *counts.entry(*r as usize).or_insert(0) += 1;
            }
        }
    }
    counts
}

fn load_all_item_tables(stb_dir: &Path) -> Result<HashMap<ItemCategory, Vec<Item>>> {
    let mut out: HashMap<ItemCategory, Vec<Item>> = HashMap::new();
    for cat in ItemCategory::ALL {
        let stb = match load_stb(stb_dir, cat.stb_name()) {
            Ok(s) => s,
            Err(e) => {
                log::warn!("skipping {}: {}", cat.stb_name(), e);
                continue;
            }
        };
        let mut items = Vec::new();
        for (i, row) in stb.data.iter().enumerate() {
            // Root label doubles as the item ID (matches the game-side 0-based
            // index into the per-category STB).
            let id = row
                .get(0)
                .and_then(|s| s.parse::<i32>().ok())
                .unwrap_or(i as i32);
            // C++ col 0 (= roselib col 1) is the raw display name in this
            // build (e.g. "Wooden Sword"). No STL lookup needed.
            let name = row.get(1).cloned().unwrap_or_default();
            // ITEM_ICON_NO is C++ col 9 → roselib col 10.
            let icon_no = row
                .get(10)
                .and_then(|s| s.parse::<i32>().ok())
                .unwrap_or(0);
            if name.is_empty() && icon_no == 0 {
                continue;
            }
            items.push(Item {
                category: *cat,
                id,
                name,
                icon_no,
            });
        }
        out.insert(*cat, items);
    }
    Ok(out)
}

fn backup_once(path: &Path) -> Result<()> {
    let bak = path.with_extension({
        let mut s = path
            .extension()
            .map(|e| e.to_string_lossy().to_string())
            .unwrap_or_default();
        s.push_str(".bak");
        s
    });
    if !bak.exists() && path.exists() {
        fs::copy(path, &bak)
            .with_context(|| format!("backing up {} -> {}", path.display(), bak.display()))?;
    }
    Ok(())
}
