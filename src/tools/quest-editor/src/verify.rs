//! Tier 1 #3 — pre-write validation.
//!
//! Catches problems *before* the writer touches any file and surfaces them in
//! the wizard (and CLI). The writer still re-checks its own guards as the final
//! authority; this layer exists to give friendly, early feedback.

use std::collections::HashSet;

use crate::classify::TriggerIndex;
use crate::data::{DataSet, ItemCategory};
use crate::gen::{GeneratedQuest, Objective, QuestKind, QuestSpec};
use crate::qsd::QsdFile;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Level {
    Error,
    Warning,
}

#[derive(Debug, Clone)]
pub struct Issue {
    pub level: Level,
    pub message: String,
}

impl Issue {
    fn err(m: impl Into<String>) -> Self {
        Issue {
            level: Level::Error,
            message: m.into(),
        }
    }
    fn warn(m: impl Into<String>) -> Self {
        Issue {
            level: Level::Warning,
            message: m.into(),
        }
    }
}

pub fn has_errors(issues: &[Issue]) -> bool {
    issues.iter().any(|i| i.level == Level::Error)
}

const MAX_ITEM_ID: i32 = 999; // type*1000+id encoding limit

/// Decode a packed item SN into (category, id), if its type is a known category.
fn decode_item(sn: i32) -> Option<(ItemCategory, i32)> {
    let (ty, id) = (sn / 1000, sn % 1000);
    ItemCategory::ALL
        .iter()
        .copied()
        .find(|c| *c as i32 == ty)
        .map(|c| (c, id))
}

/// Validate a generated quest against the loaded data. Returns issues (errors
/// block creation; warnings are advisory).
pub fn verify(ds: &DataSet, spec: &QuestSpec, gen: &GeneratedQuest) -> Vec<Issue> {
    let mut out = Vec::new();

    if spec.count < 1 {
        out.push(Issue::err("Count must be at least 1."));
    }

    // Quest number must still be the next append slot (data not stale).
    if spec.quest_sn != ds.next_free_quest_sn() {
        out.push(Issue::err(format!(
            "Quest number {} is out of date (expected {}). Reload the data folder.",
            spec.quest_sn,
            ds.next_free_quest_sn()
        )));
    }

    // The generated quest data must round-trip (internal sanity).
    let bytes = gen.qsd.to_bytes();
    let ok = QsdFile::parse(&bytes)
        .map(|p| p.to_bytes() == bytes)
        .unwrap_or(false);
    if !ok {
        out.push(Issue::err(
            "Generated quest data failed its self-check (internal error).",
        ));
    }

    // Icon compatibility: the client's NPC overhead quest icons ("!" / "?")
    // classify triggers by reward composition (classify.rs mirrors the client's
    // cevent.cpp). The generated register/complete triggers must classify as
    // accept / turn-in, or the icons for this quest silently never appear.
    let icon_idx = TriggerIndex::from_qsd(&gen.qsd);
    match icon_idx.classify(&gen.register_trigger) {
        Some(c) if c.accept && c.add_quest_sn == Some(spec.quest_sn) => {}
        _ => out.push(Issue::err(format!(
            "Internal: register trigger {} does not classify as a quest offer — \
             the NPC \"!\" icon would never show (generator regression).",
            gen.register_trigger
        ))),
    }
    match icon_idx.classify(&gen.complete_trigger) {
        Some(c) if c.turn_in => {}
        _ => out.push(Issue::warn(format!(
            "Turn-in trigger {} does not classify as a completion, so the NPC \"?\" \
             icon will not show. A quest with no EXP/zuly/item reward looks like a \
             quest-abandon option to the client — add any reward to fix.",
            gen.complete_trigger
        ))),
    }

    // The new QSD file must not already exist.
    if let Some(parent) = ds.stb_dir.parent() {
        let p = parent.join("QUESTDATA").join(&gen.qsd_filename);
        if p.exists() {
            out.push(Issue::err(format!(
                "A quest file named {} already exists.",
                gen.qsd_filename
            )));
        }
    }

    match &spec.kind {
        QuestKind::Hunt {
            monster_id,
            token_item_sn,
            chain_into_existing,
            ..
        } => {
            // Token must fit a free quest-item slot (type 13, id <= 999).
            if token_item_sn / 1000 != 13 || token_item_sn % 1000 > MAX_ITEM_ID {
                out.push(Issue::err(
                    "No free quest-item slot for the kill token (ids must stay ≤ 999).",
                ));
            } else if *token_item_sn != ds.next_free_token_item_sn() {
                out.push(Issue::warn(
                    "Token item id is out of date — reload the data folder.",
                ));
            }
            // Monster must exist; if it already has a hook we chain onto it.
            match ds.find_monster(*monster_id) {
                None => out.push(Issue::err(format!("Monster {monster_id} not found."))),
                Some(m) => {
                    let free = m.dead_event_is_free();
                    if !free && !*chain_into_existing {
                        out.push(Issue::err(format!(
                            "{} already has a quest hook — it will be chained automatically.",
                            m.name
                        )));
                    } else if *chain_into_existing {
                        out.push(Issue::warn(format!(
                            "{} already has a quest — your kill trigger will be chained onto it \
                             (its quest file is modified, with a .bak backup).",
                            m.name
                        )));
                    }
                }
            }
        }
        QuestKind::Fetch { item_sn, .. } => match decode_item(*item_sn) {
            None => out.push(Issue::err("The chosen item is invalid.")),
            Some((cat, id)) => {
                if id > MAX_ITEM_ID {
                    out.push(Issue::err(
                        "The chosen item's id is too high (must be ≤ 999).",
                    ));
                } else if ds.item_db.lookup(cat, id).is_none() {
                    out.push(Issue::warn(
                        "The chosen item wasn't found in the item tables.",
                    ));
                }
            }
        },
    }

    // Extra objectives: validate each like the primary, plus catch cross-objective
    // collisions (a monster or token id used twice within the same quest).
    let mut seen_monsters: HashSet<i32> = HashSet::new();
    let mut seen_tokens: HashSet<i32> = HashSet::new();
    if let QuestKind::Hunt {
        monster_id,
        token_item_sn,
        ..
    } = &spec.kind
    {
        seen_monsters.insert(*monster_id);
        seen_tokens.insert(*token_item_sn);
    }
    for (i, o) in spec.extra_objectives.iter().enumerate() {
        let n = i + 2; // objective #1 is the primary
        match o {
            Objective::Hunt {
                monster_id,
                count,
                token_item_sn,
                chain_into_existing,
                ..
            } => {
                if *count < 1 {
                    out.push(Issue::err(format!(
                        "Objective {n}: count must be at least 1."
                    )));
                }
                if token_item_sn / 1000 != 13 || token_item_sn % 1000 > MAX_ITEM_ID {
                    out.push(Issue::err(format!(
                        "Objective {n}: no free quest-item slot for its kill token."
                    )));
                } else if !seen_tokens.insert(*token_item_sn) {
                    out.push(Issue::err(format!(
                        "Objective {n}: its token item id collides with another objective."
                    )));
                }
                if !seen_monsters.insert(*monster_id) {
                    out.push(Issue::err(format!(
                        "Objective {n}: monster {monster_id} is already targeted by another objective."
                    )));
                }
                match ds.find_monster(*monster_id) {
                    None => out.push(Issue::err(format!(
                        "Objective {n}: monster {monster_id} not found."
                    ))),
                    Some(m) => {
                        let free = m.dead_event_is_free();
                        if !free && !*chain_into_existing {
                            out.push(Issue::err(format!(
                                "Objective {n}: {} already has a quest hook (it will be chained).",
                                m.name
                            )));
                        } else if *chain_into_existing {
                            out.push(Issue::warn(format!(
                                "Objective {n}: {} already has a quest — chained onto it (its file is \
                                 modified, with a .bak).",
                                m.name
                            )));
                        }
                    }
                }
            }
            Objective::Fetch { item_sn, count, .. } => {
                if *count < 1 {
                    out.push(Issue::err(format!(
                        "Objective {n}: count must be at least 1."
                    )));
                }
                match decode_item(*item_sn) {
                    None => out.push(Issue::err(format!(
                        "Objective {n}: the chosen item is invalid."
                    ))),
                    Some((cat, id)) => {
                        if id > MAX_ITEM_ID {
                            out.push(Issue::err(format!(
                                "Objective {n}: the item's id is too high (must be ≤ 999)."
                            )));
                        } else if ds.item_db.lookup(cat, id).is_none() {
                            out.push(Issue::warn(format!(
                                "Objective {n}: the item wasn't found in the item tables."
                            )));
                        }
                    }
                }
            }
        }
    }

    // Optional reward item.
    if let Some((sn, qty)) = spec.reward_item {
        if qty < 1 {
            out.push(Issue::warn(
                "Reward item quantity is zero — it won't be given.",
            ));
        }
        match decode_item(sn) {
            None => out.push(Issue::err("The reward item is invalid.")),
            Some((cat, id)) => {
                if id > MAX_ITEM_ID {
                    out.push(Issue::err(
                        "The reward item's id is too high (must be ≤ 999).",
                    ));
                } else if ds.item_db.lookup(cat, id).is_none() {
                    out.push(Issue::warn(
                        "The reward item wasn't found in the item tables.",
                    ));
                }
            }
        }
    }

    out
}
