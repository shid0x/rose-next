//! GM catalog. Column constants follow common/include/rose/io/stb.h.
use std::collections::HashMap;

use anyhow::{bail, Context, Result};

use crate::assets::Assets;
use crate::data::{Item, ItemCategory, STORE_MAX_ITEM_NO};
use crate::text::{self, Codec};

pub struct Catalog {
    pub items: Vec<CatalogItem>,
    pub warnings: Vec<String>,
}

pub struct CatalogItem {
    pub item: Item,
    pub description: String,
    pub class: i32,
    pub level: Option<i32>,
    pub attack: Option<i32>,
    pub defense: Option<i32>,
    pub resistance: Option<i32>,
    pub quality: i32,
    pub search_text: String,
}

impl CatalogItem {
    pub fn spawnable(&self) -> bool {
        (1..=STORE_MAX_ITEM_NO).contains(&self.item.id) && self.item.icon_no > 0
    }

    pub fn stackable(&self) -> bool {
        matches!(
            self.item.category,
            ItemCategory::UseItem
                | ItemCategory::Gem
                | ItemCategory::Natural
                | ItemCategory::QuestItem
        )
    }

    pub fn command(&self, quantity: i32) -> Option<String> {
        if !self.spawnable() {
            return None;
        }
        let base = format!("/item {} {}", self.item.category as u8, self.item.id);
        // The third argument is an appraisal stat on equipment, NOT a quantity.
        Some(if self.stackable() && quantity > 1 {
            format!("{base} {}", quantity.clamp(1, 100))
        } else {
            base
        })
    }
}

impl Catalog {
    pub fn load(assets: &Assets) -> Result<Self> {
        Self::load_with(assets, Codec::Auto)
    }

    /// `codec` is how the tables' text is read: `Auto` decides per file, so a
    /// Jrose folder (Shift-JIS, legacy string tables) and our own open alike.
    pub fn load_with(assets: &Assets, codec: Codec) -> Result<Self> {
        let mut items = Vec::new();
        let mut warnings = Vec::new();
        let prefixes = match english_names_with(assets, "3DDATA/STB/STR_ITEMPREFIX.STL", codec) {
            Ok(prefixes) => Some(prefixes),
            Err(e) => {
                warnings.push(format!(
                    "Item prefixes unavailable; using table names for prefixed equipment: {e:#}"
                ));
                None
            }
        };
        for &category in ItemCategory::ALL {
            let path = format!("3DDATA/STB/{}", category.stb_name());
            let table = assets
                .read(&path)
                .and_then(|bytes| text::read_stb(&bytes, codec));
            let table = match table {
                Ok(table) => table,
                Err(e) => {
                    warnings.push(format!("{path}: {e:#}"));
                    continue;
                }
            };
            let stl_path = path.replace(".STB", "_S.STL");
            let names = match english_names_with(assets, &stl_path, codec) {
                Ok(names) => names,
                Err(e) => {
                    warnings.push(format!(
                        "Using table names for {}: {e:#}",
                        category.display()
                    ));
                    HashMap::new()
                }
            };
            for (id, row) in table.iter().enumerate().skip(1) {
                if let Some(item) = collect_item(category, id, row, &names, prefixes.as_ref()) {
                    items.push(item);
                }
            }
        }
        if items.is_empty() {
            bail!("No items could be loaded. {}", warnings.join("\n"));
        }
        Ok(Self { items, warnings })
    }
}

pub(crate) type Names = text::Names;

pub(crate) fn english_names_with(assets: &Assets, path: &str, codec: Codec) -> Result<Names> {
    text::read_stl_names(&assets.read(path)?, codec)
}

fn collect_item(
    category: ItemCategory,
    id: usize,
    row: &[String],
    names: &Names,
    prefixes: Option<&Names>,
) -> Option<CatalogItem> {
    // Roselib retains root column 0; the engine skips it. Row position, not
    // the editable root label, is the ID that Cheat_item indexes on the server.
    let number = |col: usize| {
        row.get(col + 1)
            .and_then(|s| s.trim().parse::<i32>().ok())
            .unwrap_or(0)
    };
    let raw_name = row.get(1).cloned().unwrap_or_default();
    let translated = row.last().and_then(|key| names.get(key));
    let (mut name, description) = translated
        .cloned()
        .unwrap_or((raw_name.clone(), String::new()));
    // Equipment variants share the base item's STL key. The penultimate
    // on-disk column carries the STR_ITEMPREFIX key; column 30 is the socket/
    // rare flag, not the name prefix. Only these five categories use prefixes
    // (see CItem::GetItemRareType / CStringManager::GetItemName).
    if translated.is_some()
        && matches!(
            category,
            ItemCategory::Cap
                | ItemCategory::Body
                | ItemCategory::Arms
                | ItemCategory::Foot
                | ItemCategory::Weapon
        )
    {
        let prefix_id = row
            .iter()
            .rev()
            .nth(1)
            .and_then(|value| value.trim().parse::<i32>().ok())
            .unwrap_or(0);
        if prefix_id > 0 {
            if let Some(prefixes) = prefixes {
                if let Some((prefix, _)) = prefixes.get(&prefix_id.to_string()) {
                    name = format!("{prefix} {name}");
                }
            } else if !raw_name.trim().is_empty() {
                // Older portable packages omit STR_ITEMPREFIX. Their raw
                // names already include the prefix: keep variants distinct.
                name = raw_name.clone();
            }
        }
    }
    let icon_no = number(9);
    if name.trim().is_empty() && icon_no == 0 {
        return None;
    }
    if name.trim().is_empty() {
        name = format!("Unnamed {} #{}", category.display(), id);
    }
    let equipment = (category as u8) <= 9;
    // AT_LEVEL = 31. Requirement slots mean different things in each schema.
    let requirement_cols: &[usize] = if equipment {
        &[19, 21]
    } else if category == ItemCategory::UseItem {
        &[17]
    } else if category == ItemCategory::Vehicle {
        &[21]
    } else {
        &[]
    };
    let level = if requirement_cols.is_empty() {
        None
    } else {
        Some(
            requirement_cols
                .iter()
                .filter(|&&col| number(col) == 31)
                .map(|&col| number(col + 1))
                .max()
                .unwrap_or(0),
        )
    };
    let attack = match category {
        ItemCategory::Weapon => Some(number(35)),
        ItemCategory::Vehicle => Some(number(36)),
        _ => None,
    };
    let class = number(4);
    let search_text = format!(
        "{name} {raw_name} {description} {} {} {}:{} {class}",
        category.display(),
        id,
        category as u8,
        id
    )
    .to_lowercase();
    Some(CatalogItem {
        item: Item {
            category,
            id: id as i32,
            name,
            icon_no,
        },
        description,
        class,
        level,
        attack,
        defense: equipment.then(|| number(31)),
        resistance: equipment.then(|| number(32)),
        quality: number(8),
        search_text,
    })
}

#[derive(Default, Clone, PartialEq)]
pub struct RangeFilter {
    pub min: String,
    pub max: String,
}

impl RangeFilter {
    pub fn bounds(&self) -> Result<(Option<i32>, Option<i32>)> {
        let parse = |s: &str| -> Result<Option<i32>> {
            if s.trim().is_empty() {
                Ok(None)
            } else {
                Ok(Some(s.trim().parse().context("Enter whole numbers")?))
            }
        };
        let bounds = (parse(&self.min)?, parse(&self.max)?);
        if matches!(bounds, (Some(min), Some(max)) if min > max) {
            bail!("Minimum exceeds maximum");
        }
        Ok(bounds)
    }

    pub fn matches(&self, value: Option<i32>) -> bool {
        let Ok((min, max)) = self.bounds() else {
            return false;
        };
        if min.is_none() && max.is_none() {
            return true;
        }
        value
            .map(|v| min.map_or(true, |min| v >= min) && max.map_or(true, |max| v <= max))
            .unwrap_or(false)
    }
}

#[derive(Clone, PartialEq)]
pub struct Filter {
    pub search: String,
    pub category: Option<ItemCategory>,
    pub class: Option<i32>,
    pub level: RangeFilter,
    pub attack: RangeFilter,
    pub defense: RangeFilter,
    pub resistance: RangeFilter,
    pub quality: RangeFilter,
    pub spawnable_only: bool,
}

impl Default for Filter {
    fn default() -> Self {
        Self {
            search: String::new(),
            category: None,
            class: None,
            level: RangeFilter::default(),
            attack: RangeFilter::default(),
            defense: RangeFilter::default(),
            resistance: RangeFilter::default(),
            quality: RangeFilter::default(),
            spawnable_only: true,
        }
    }
}

impl Filter {
    pub fn matches(&self, item: &CatalogItem) -> bool {
        (!self.spawnable_only || item.spawnable())
            && self.category.map_or(true, |c| c == item.item.category)
            && self.class.map_or(true, |c| c == item.class)
            && self
                .search
                .to_lowercase()
                .split_whitespace()
                .all(|word| item.search_text.contains(word))
            && self.level.matches(item.level)
            && self.attack.matches(item.attack)
            && self.defense.matches(item.defense)
            && self.resistance.matches(item.resistance)
            && self.quality.matches(Some(item.quality))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn row(values: &[(usize, &str)]) -> Vec<String> {
        let mut row = vec![String::new(); 72];
        row[0] = "9999".into(); // Root labels are not authoritative IDs.
        row[1] = "Internal name".into();
        row[10] = "123".into();
        for &(game_col, value) in values {
            row[game_col + 1] = value.into();
        }
        row[71] = "ITEM_KEY".into();
        row
    }

    #[test]
    fn equipment_uses_both_requirement_slots_and_explicit_type_id() {
        let names = HashMap::from([(
            "ITEM_KEY".into(),
            ("Sword of Testing".into(), "A translated description".into()),
        )]);
        let row = row(&[
            (19, "10"),
            (20, "250"),
            (21, "31"),
            (22, "240"),
            (31, "17"),
            (32, "9"),
            (35, "410"),
            (36, "12"),
        ]);
        let item = collect_item(ItemCategory::Weapon, 1453, &row, &names, None).unwrap();
        assert_eq!(item.item.name, "Sword of Testing");
        assert_eq!(item.level, Some(240));
        assert_eq!(item.attack, Some(410));
        assert_eq!(item.defense, Some(17));
        assert_eq!(item.resistance, Some(9));
        assert_eq!(item.command(99).as_deref(), Some("/item 8 1453"));
    }

    #[test]
    fn vehicle_fuel_and_consumable_effects_are_not_equipment_stats() {
        let row = row(&[
            (17, "31"),
            (18, "75"),
            (19, "31"),
            (20, "999"),
            (21, "31"),
            (22, "100"),
            (31, "5000"),
            (32, "20"),
            (35, "300"),
            (36, "150"),
        ]);
        let vehicle = collect_item(ItemCategory::Vehicle, 3, &row, &HashMap::new(), None).unwrap();
        assert_eq!(vehicle.level, Some(100));
        assert_eq!(vehicle.attack, Some(150));
        assert_eq!(vehicle.defense, None);
        assert_eq!(vehicle.resistance, None);
        assert_eq!(vehicle.command(100).as_deref(), Some("/item 14 3"));
        let potion =
            collect_item(ItemCategory::UseItem, 1060, &row, &HashMap::new(), None).unwrap();
        assert_eq!(potion.level, Some(75));
        assert_eq!(potion.attack, None);
        assert_eq!(potion.command(200).as_deref(), Some("/item 10 1060 100"));
    }

    #[test]
    fn filters_combine_words_and_inclusive_ranges_and_reject_invalid_input() {
        let item = collect_item(
            ItemCategory::Weapon,
            1453,
            &row(&[(19, "31"), (20, "240"), (35, "400")]),
            &HashMap::new(),
            None,
        )
        .unwrap();
        let mut filter = Filter::default();
        filter.search = "INTERNAL 8:1453".into();
        filter.level = RangeFilter {
            min: "240".into(),
            max: "240".into(),
        };
        filter.attack.min = "400".into();
        assert!(filter.matches(&item));
        filter.attack.max = "399".into();
        assert!(!filter.matches(&item));
        assert!(filter.attack.bounds().is_err());
        filter.attack.max = "oops".into();
        assert!(!filter.matches(&item));
        assert!(!filter.level.matches(None));
        assert!(RangeFilter::default().matches(None));
    }

    #[test]
    fn invalid_rows_have_no_command_and_unrestricted_items_have_level_zero() {
        let mut item =
            collect_item(ItemCategory::Body, 2048, &row(&[]), &HashMap::new(), None).unwrap();
        assert_eq!(item.level, Some(0));
        assert!(item.command(1).is_none());
        item.item.id = 0;
        assert!(item.command(1).is_none());
        item.item.id = 5;
        item.item.icon_no = 0;
        assert!(item.command(1).is_none());
        assert!(collect_item(
            ItemCategory::Body,
            1,
            &vec![String::new(); 40],
            &HashMap::new(),
            None,
        )
        .is_none());
    }

    #[test]
    fn shared_base_names_include_equipment_prefixes_in_display_and_search() {
        let names = HashMap::from([(
            "ITEM_KEY".into(),
            ("Trunket Armor".into(), "Armor description".into()),
        )]);
        let prefixes = HashMap::from([("3".into(), ("Golden".into(), String::new()))]);
        let base_row = row(&[(19, "31"), (20, "50"), (30, "1")]);
        let base =
            collect_item(ItemCategory::Body, 33, &base_row, &names, Some(&prefixes)).unwrap();
        assert_eq!(base.item.name, "Trunket Armor");
        let mut variant_row = base_row.clone();
        variant_row[70] = "3".into();
        variant_row[1] = "Golden Trunket Armor".into();
        let variant = collect_item(
            ItemCategory::Body,
            316,
            &variant_row,
            &names,
            Some(&prefixes),
        )
        .unwrap();
        assert_eq!(variant.item.name, "Golden Trunket Armor");
        assert_eq!(variant.description, "Armor description");
        assert_eq!(variant.level, Some(50));
        assert_eq!(variant.command(1).as_deref(), Some("/item 3 316"));
        let filter = Filter {
            search: "golden trunket".into(),
            ..Default::default()
        };
        assert!(filter.matches(&variant));
        assert!(!filter.matches(&base));
        // Neither a missing translation nor an old package's missing prefix
        // table should prepend a second prefix to an already complete raw name.
        for (names, prefixes) in [(&names, None), (&HashMap::new(), Some(&prefixes))] {
            let item =
                collect_item(ItemCategory::Body, 316, &variant_row, names, prefixes).unwrap();
            assert_eq!(item.item.name, "Golden Trunket Armor");
        }
        for category in [
            ItemCategory::Cap,
            ItemCategory::Arms,
            ItemCategory::Foot,
            ItemCategory::Weapon,
        ] {
            assert_eq!(
                collect_item(category, 316, &variant_row, &names, Some(&prefixes))
                    .unwrap()
                    .item
                    .name,
                "Golden Trunket Armor"
            );
        }
        for category in [
            ItemCategory::Face,
            ItemCategory::Back,
            ItemCategory::Jewel,
            ItemCategory::SubWpn,
            ItemCategory::UseItem,
            ItemCategory::Gem,
            ItemCategory::Natural,
            ItemCategory::QuestItem,
            ItemCategory::Vehicle,
        ] {
            assert_eq!(
                collect_item(category, 316, &variant_row, &names, Some(&prefixes))
                    .unwrap()
                    .item
                    .name,
                "Trunket Armor"
            );
        }
        variant_row[70] = "17".into(); // Empty/unmapped prefix means no prefix.
        assert_eq!(
            collect_item(
                ItemCategory::Body,
                316,
                &variant_row,
                &names,
                Some(&prefixes)
            )
            .unwrap()
            .item
            .name,
            "Trunket Armor"
        );
    }

    #[test]
    #[ignore = "requires the workspace's extracted data and Exes archives"]
    fn workspace_catalog_and_icons_from_loose_and_packed_data() {
        use crate::icons::IconStore;
        use std::{path::Path, sync::Arc};
        let repo = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
        let mut roots = vec![repo.join("data"), repo.join("Exes")];
        if let Some(extra) = std::env::var_os("ROSE_GM_CATALOG_TEST_ROOT") {
            roots.push(extra.into());
        }
        for root in roots {
            let folder = root.display();
            let assets = Arc::new(Assets::open(&root).unwrap());
            let catalog = Catalog::load(&assets).unwrap();
            for (id, expected) in [(33, "Trunket Armor"), (316, "Golden Trunket Armor")] {
                let item = catalog
                    .items
                    .iter()
                    .find(|item| item.item.category == ItemCategory::Body && item.item.id == id)
                    .unwrap();
                assert_eq!(item.item.name, expected, "{folder}: body {id}");
                println!("{folder}: body {id} = {}", item.item.name);
            }
            assert!(catalog.items.len() > 1000);
            assert!(catalog
                .items
                .iter()
                .any(|item| item.item.category == ItemCategory::Weapon && item.item.id > 999));
            assert!(
                catalog.warnings.is_empty(),
                "{folder}: {:?}",
                catalog.warnings
            );
            let ctx = egui::Context::default();
            let mut icons = IconStore::from_assets(assets).unwrap();
            let mut seen = std::collections::HashSet::new();
            let mut missing = Vec::new();
            for item in &catalog.items {
                if item.item.icon_no > 0
                    && seen.insert(item.item.icon_no)
                    && icons.icon_texture(&ctx, item.item.icon_no).is_none()
                {
                    missing.push((item.item.category, item.item.id, item.item.icon_no));
                }
            }
            println!(
                "{folder}: {} items, {} unique icons; missing: {missing:?}",
                catalog.items.len(),
                seen.len()
            );
            assert!(missing.is_empty());
        }
    }
}
