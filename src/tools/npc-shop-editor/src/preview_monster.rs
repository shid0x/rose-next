//! 3D preview of a LIST_NPC row, built the way the client builds a monster
//! (client source in brackets):
//!
//! - `LIST_NPC.CHR` gives the row a skeleton, a list of `PART_NPC.ZSC`
//!   objects, (motion slot, ZMO) pairs and bone effects
//!   [`CCharMODEL::Load_MOBorNPC`]. A slot listed twice keeps the last file.
//! - Parts link exactly like avatar parts [`CCharPART::Load`, `Load_ZMODEL`],
//!   with no default bone or dummy: a part that names neither is skinned.
//!   ZSC position, rotation and scale are ignored here too.
//! - LIST_NPC columns 5 and 6 are objects of LIST_WEAPON.ZSC and
//!   LIST_SUBWPN.ZSC (China Rose: the male LIST_MWEAPON / LIST_MSUBWPN, see
//!   `WeaponTables`), linked by their parts' own dummy tags
//!   [`CCharModelDATA::Load_MOBorNPC`].
//! - Column 4 scales the whole model [`CObjMOB` `m_fScale`]. The pane fits the
//!   camera to the model either way, so the scale is reported, not applied.
//!
//! Not drawn: bone effects (no particle system), and mesh-animated parts
//! (morphers) stay in their stored shape, as in the item preview.

use std::collections::BTreeSet;

use anyhow::Result;

use crate::assets::Assets;
use crate::monsters::{
    file_name, Chr, ChrModel, Monster, WeaponTables, Zsc, MOTION_NAMES, NPC_CHR, NPC_ZSC,
};
use crate::preview::{load_rig, Animation, Scene, SceneBuilder};

/// Whether `assets` holds monster models. The tester package ships the
/// monster tables (PART_NPC.ZSC among them) and a manifest instead of the
/// meshes, and gets no preview rather than a list of misses.
pub fn has_npc_models(assets: &Assets) -> bool {
    !assets.has_manifest() && assets.exists(NPC_ZSC)
}

/// `MOB_ANI_*` slot name, capitalised for the pane.
pub fn motion_label(slot: usize) -> String {
    let name = MOTION_NAMES.get(slot).copied().unwrap_or("unknown");
    let mut chars = name.chars();
    chars
        .next()
        .map(|c| c.to_ascii_uppercase())
        .into_iter()
        .chain(chars)
        .collect()
}

/// A slot by name ("attack", "cast 1" or "cast1") or number.
pub fn parse_motion(text: &str) -> Option<usize> {
    let squashed = |s: &str| s.replace(' ', "").to_ascii_lowercase();
    text.parse()
        .ok()
        .filter(|&slot| slot < MOTION_NAMES.len())
        .or_else(|| {
            MOTION_NAMES
                .iter()
                .position(|name| squashed(name) == squashed(text))
        })
}

/// Everything the monster preview reads once per data folder.
pub struct MonsterModels {
    chr: Result<Chr, String>,
    parts: Result<Zsc, String>,
    weapons: Result<Zsc, String>,
    subweapons: Result<Zsc, String>,
    /// The two tables' file names, for messages.
    hand_files: [&'static str; 2],
}

fn read<T>(assets: &Assets, path: &str, parse: fn(&[u8]) -> Result<T>) -> Result<T, String> {
    assets
        .read(path)
        .and_then(|bytes| parse(&bytes))
        .map_err(|e| format!("{path}: {e:#}"))
}

impl MonsterModels {
    pub fn load(assets: &Assets) -> Self {
        let tables = WeaponTables::detect(assets);
        let (weapon, subweapon) = (tables.weapon(false), tables.subweapon(false));
        Self {
            chr: read(assets, NPC_CHR, Chr::parse),
            parts: read(assets, NPC_ZSC, Zsc::parse),
            weapons: read(assets, weapon, Zsc::parse),
            subweapons: read(assets, subweapon, Zsc::parse),
            hand_files: [file_name(weapon), file_name(subweapon)],
        }
    }

    fn model(&self, id: i32) -> Result<(&Chr, &ChrModel), String> {
        let chr = self.chr.as_ref().map_err(Clone::clone)?;
        let model = usize::try_from(id)
            .ok()
            .and_then(|i| chr.models.get(i))
            .and_then(Option::as_ref)
            .ok_or_else(|| format!("LIST_NPC.CHR has no model for row {id}"))?;
        Ok((chr, model))
    }

    pub fn build(&self, assets: &Assets, monster: &Monster) -> Scene {
        let (chr, model) = match self.model(monster.id) {
            Ok(found) => found,
            Err(e) => {
                let mut builder = SceneBuilder::new(assets, None);
                builder.note(e);
                return builder.finish();
            }
        };
        let rig = match usize::try_from(model.skeleton)
            .ok()
            .and_then(|i| chr.skeletons.get(i))
        {
            Some(path) => load_rig(assets, path),
            None => Err(format!(
                "skeleton {} is outside LIST_NPC.CHR ({} skeletons)",
                model.skeleton,
                chr.skeletons.len()
            )),
        };
        let mut builder = SceneBuilder::new(assets, rig.as_ref().ok().cloned());
        if let Err(e) = &rig {
            builder.note(format!("Skeleton unavailable, no animation: {e}"));
        }
        const PARTS: &str = "PART_NPC.ZSC";
        if model.parts.is_empty() {
            builder.note("LIST_NPC.CHR gives the row no parts".into());
        }
        match &self.parts {
            Ok(zsc) => {
                for (i, &index) in model.parts.iter().enumerate() {
                    let label = format!("Part {}", i + 1);
                    let Some(object) = builder.object(zsc, PARTS, index, &label) else {
                        continue;
                    };
                    if object.parts.is_empty() {
                        builder.note(format!("{label}: model {index} of {PARTS} is empty"));
                    }
                    builder.add(zsc, PARTS, object, &label, (-1, -1), false, false);
                }
            }
            Err(e) => builder.note(e.clone()),
        }
        let hands = [
            (
                monster.right_weapon,
                &self.weapons,
                self.hand_files[0],
                "Right-hand weapon",
            ),
            (
                monster.left_weapon,
                &self.subweapons,
                self.hand_files[1],
                "Left-hand weapon",
            ),
        ];
        for (index, zsc, file, label) in hands {
            if index <= 0 {
                continue;
            }
            match zsc {
                Ok(zsc) => {
                    if let Some(object) = builder.object(zsc, file, index, label) {
                        builder.add(zsc, file, object, label, (-1, -1), true, false);
                    }
                }
                Err(e) => builder.note(format!("{label}: {e}")),
            }
        }
        builder.finish()
    }

    /// The motion slots row `id` names a file for, in slot order.
    pub fn motion_slots(&self, id: i32) -> Vec<usize> {
        let Ok((_, model)) = self.model(id) else {
            return Vec::new();
        };
        let slots: BTreeSet<usize> = model
            .motions
            .iter()
            .filter_map(|&(slot, _)| usize::try_from(slot).ok())
            .collect();
        slots.into_iter().collect()
    }

    /// The motion row `id` plays in `slot`: `None` for the bind pose.
    pub fn motion(
        &self,
        assets: &Assets,
        id: i32,
        slot: Option<usize>,
    ) -> Result<Option<Animation>, String> {
        let Some(slot) = slot else {
            return Ok(None);
        };
        let label = motion_label(slot);
        let (chr, model) = self.model(id).map_err(|e| format!("{label}: {e}"))?;
        let index = model
            .motions
            .iter()
            .rev()
            .find(|&&(s, _)| usize::try_from(s).ok() == Some(slot))
            .map(|&(_, index)| index)
            .ok_or_else(|| format!("{label}: the row has no motion in this slot"))?;
        let path = usize::try_from(index)
            .ok()
            .and_then(|i| chr.motions.get(i))
            .ok_or_else(|| {
                format!(
                    "{label}: motion {index} is outside LIST_NPC.CHR ({} motions)",
                    chr.motions.len()
                )
            })?;
        Animation::load(assets, path)
            .map(Some)
            .map_err(|e| format!("{label}: {path}: {e:#}"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::monsters::{ZscMaterial, ZscObject, ZscPart};

    fn models() -> MonsterModels {
        let chr = Chr {
            skeletons: vec!["3DDATA/NPC/X/X.ZMD".into()],
            motions: vec!["IDLE_A.ZMO".into(), "IDLE_B.ZMO".into(), "WALK.ZMO".into()],
            effects: Vec::new(),
            models: vec![
                None,
                Some(ChrModel {
                    skeleton: 0,
                    parts: vec![0, 5],
                    // Idle twice (the last wins), walk, a skipped -1 slot, a bad index.
                    motions: vec![(0, 0), (1, 2), (0, 1), (-1, 0), (2, 9)],
                    effects: Vec::new(),
                }),
            ],
        };
        let zsc = Zsc {
            meshes: vec!["3DDATA/NPC/X/BODY.ZMS".into()],
            materials: vec!["3DDATA/NPC/X/BODY.DDS".into()],
            material_flags: vec![ZscMaterial::default()],
            objects: vec![ZscObject {
                parts: vec![ZscPart {
                    mesh: 0,
                    material: 0,
                    animations: Vec::new(),
                    bone: -1,
                    dummy: -1,
                }],
            }],
        };
        MonsterModels {
            chr: Ok(chr),
            parts: Ok(zsc),
            weapons: Err("LIST_WEAPON.ZSC: gone".into()),
            subweapons: Err("LIST_SUBWPN.ZSC: gone".into()),
            hand_files: ["LIST_WEAPON.ZSC", "LIST_SUBWPN.ZSC"],
        }
    }

    fn empty_assets() -> (tempfile::TempDir, Assets) {
        let temp = tempfile::tempdir().unwrap();
        let stb = temp.path().join("3DDATA/STB");
        std::fs::create_dir_all(&stb).unwrap();
        std::fs::write(stb.join("LIST_WEAPON.STB"), b"").unwrap();
        let assets = Assets::open(temp.path()).unwrap();
        (temp, assets)
    }

    fn monster(id: i32, right_weapon: i32) -> Monster {
        Monster {
            id,
            name: String::new(),
            table_name: String::new(),
            has_game_name: false,
            level: 1,
            hp: 1,
            max_hp: 1,
            attack: 0,
            hit: 0,
            defense: 0,
            resistance: 0,
            avoid: 0,
            attack_speed: 0,
            walk_speed: 0,
            run_speed: 0,
            attack_range: 0,
            exp: 0,
            magic_damage: false,
            npc_type: 0,
            ai: 0,
            scale: 100,
            right_weapon,
            left_weapon: 0,
            issues: Vec::new(),
            spawn_blocker: None,
            search_text: String::new(),
        }
    }

    #[test]
    fn motion_slots_and_files_follow_the_client() {
        let models = models();
        let (_temp, assets) = empty_assets();
        assert_eq!(models.motion_slots(1), [0, 1, 2]);
        assert!(models.motion_slots(0).is_empty());
        assert!(models.motion(&assets, 1, None).unwrap().is_none());
        // The file is missing, but the error names the one the client would load.
        let idle = models.motion(&assets, 1, Some(0)).err().unwrap();
        assert!(idle.contains("IDLE_B.ZMO"), "{idle}");
        let attack = models.motion(&assets, 1, Some(2)).err().unwrap();
        assert!(attack.contains("outside LIST_NPC.CHR"), "{attack}");
        let hit = models.motion(&assets, 1, Some(3)).err().unwrap();
        assert!(hit.contains("no motion"), "{hit}");
    }

    #[test]
    fn missing_files_become_notes() {
        let models = models();
        let (_temp, assets) = empty_assets();
        let scene = models.build(&assets, &monster(1, 3));
        assert!(scene.meshes.is_empty());
        let notes = scene.notes.join("\n");
        for expected in [
            "X.ZMD",
            "BODY.ZMS",
            "model 5 is outside",
            "Right-hand weapon",
        ] {
            assert!(notes.contains(expected), "{expected} not in\n{notes}");
        }
        let scene = models.build(&assets, &monster(0, 0));
        assert_eq!(scene.notes, ["LIST_NPC.CHR has no model for row 0"]);
    }

    #[test]
    fn motions_parse_by_name_or_slot() {
        assert_eq!(parse_motion("attack"), Some(2));
        assert_eq!(parse_motion("Cast1"), Some(6));
        assert_eq!(parse_motion("cast 1"), Some(6));
        assert_eq!(parse_motion("4"), Some(4));
        assert_eq!(parse_motion("99"), None);
        assert_eq!(motion_label(7), "Skill 1");
    }
}
