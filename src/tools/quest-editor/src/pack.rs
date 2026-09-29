//! Quest packs — a set of quests described in a JSON file and applied in one go.
//!
//! The wizard is the tool for making one quest by hand; a pack is for content
//! that is *built by a script* and must be reproducible, like a zone importer's
//! quest line. Each quest carries a stable `key`, recorded in its manifest, so a
//! re-run finds what it already created instead of making duplicates:
//!
//! * a quest whose key is new is **created** (numbers allocated as the wizard
//!   would: next quest SN, next token ids, a fresh switch for a one-time quest);
//! * a quest whose key exists is **kept** -- only its dialog option is refreshed,
//!   which is what an importer that rebuilds an NPC's `.CON` from source needs;
//! * with `--replace`, an existing quest is deleted and re-created from the pack
//!   on its **old completion switch**, like a wizard edit, so players who finished
//!   it stay finished and quests that require it keep working.
//!
//! `requires` names other quests by key -- earlier in the pack, or created by an
//! earlier run. Only a one-time quest (`"once": true`) can be required.
//!
//! ```json
//! { "quests": [
//!   { "key": "shibuya-yuka", "once": true,
//!     "title": "...", "start_text": "...", "progress_text": "...", "complete_text": "...",
//!     "objectives": [ { "kind": "hunt", "monster": 4060, "count": 30,
//!                       "token_name": "Business Zombie Tag", "token_icon": 12 } ],
//!     "reward_exp": 20000, "reward_zuly": 5000,
//!     "reward_item": { "item": "body:880", "qty": 1 },
//!     "requires": [],
//!     "giver": { "npc": 4044, "hook": "Is there anything I can do?" } } ] }
//! ```
//!
//! Objectives: `{"kind":"hunt","monster":N,"count":N, token_name?, token_desc?,
//! token_icon?}` or `{"kind":"fetch","item":"type:id","count":N, consume?}`; the
//! first one is the primary. An item is `"<type>:<id>"`, the type a number or a
//! name (`face cap body arms foot back jewel weapon subwpn useitem gem natural
//! quest pat`).
//!
//! Every write goes through the same `apply_quest` / `delete_quest` /
//! `append_quest_to_npc_dialog` the wizard uses, and every quest is verified
//! first; a pack stops at the first quest with an error.

use std::collections::BTreeMap;
use std::path::Path;

use anyhow::{anyhow, bail, Context, Result};
use serde::Deserialize;

use crate::data::{encode_item_no, DataSet, Item, ItemCategory, QuestSwitches};
use crate::gen::{generate, Objective, Prerequisite, QuestKind, QuestSpec};
use crate::verify::{self, Level};
use crate::write::GiverText;

#[derive(Debug, Deserialize)]
pub struct Pack {
    pub quests: Vec<PackQuest>,
}

#[derive(Debug, Deserialize)]
pub struct PackQuest {
    pub key: String,
    pub title: String,
    #[serde(default)]
    pub start_text: String,
    #[serde(default)]
    pub progress_text: String,
    #[serde(default)]
    pub complete_text: String,
    pub objectives: Vec<PackObjective>,
    #[serde(default)]
    pub reward_exp: i32,
    #[serde(default)]
    pub reward_zuly: i32,
    #[serde(default)]
    pub reward_item: Option<PackItem>,
    #[serde(default)]
    pub once: bool,
    #[serde(default)]
    pub requires: Vec<String>,
    #[serde(default)]
    pub giver: Option<PackGiver>,
}

#[derive(Debug, Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum PackObjective {
    Hunt {
        monster: i32,
        count: i32,
        #[serde(default)]
        token_name: Option<String>,
        #[serde(default)]
        token_desc: Option<String>,
        #[serde(default)]
        token_icon: Option<i32>,
    },
    Fetch {
        item: String,
        count: i32,
        #[serde(default = "yes")]
        consume: bool,
    },
}

#[derive(Debug, Deserialize)]
pub struct PackItem {
    pub item: String,
    #[serde(default = "one")]
    pub qty: i16,
}

/// The NPC that offers the quest. Only append mode: the option is added to the
/// NPC's existing dialog (a pack is for NPCs that already talk).
#[derive(Debug, Deserialize)]
pub struct PackGiver {
    pub npc: i32,
    #[serde(default)]
    pub hook: String,
    #[serde(default)]
    pub accept: String,
    #[serde(default)]
    pub decline: String,
    #[serde(default)]
    pub after_accept: String,
    #[serde(default)]
    pub turnin: String,
    #[serde(default)]
    pub progress: String,
}

fn yes() -> bool {
    true
}
fn one() -> i16 {
    1
}

/// Parse `"<type>:<id>"` into a packed item SN; the type is a number or a name.
pub fn parse_item(s: &str) -> Result<i32> {
    let (ty, id) = s
        .split_once(':')
        .ok_or_else(|| anyhow!("item \"{s}\" is not \"<type>:<id>\""))?;
    let id: i32 = id.trim().parse().with_context(|| format!("item id in \"{s}\""))?;
    let ty = ty.trim().to_ascii_lowercase();
    let cat = match ty.as_str() {
        "face" | "faceitem" => ItemCategory::Face,
        "cap" | "helmet" => ItemCategory::Cap,
        "body" | "armor" => ItemCategory::Body,
        "arms" | "gauntlet" => ItemCategory::Arms,
        "foot" | "boots" => ItemCategory::Foot,
        "back" => ItemCategory::Back,
        "jewel" => ItemCategory::Jewel,
        "weapon" => ItemCategory::Weapon,
        "subwpn" => ItemCategory::SubWpn,
        "useitem" | "use" => ItemCategory::UseItem,
        "gem" => ItemCategory::Gem,
        "natural" => ItemCategory::Natural,
        "quest" | "questitem" => ItemCategory::QuestItem,
        "pat" | "vehicle" => ItemCategory::Vehicle,
        n => {
            let n: i32 = n.parse().map_err(|_| anyhow!("unknown item type \"{ty}\" in \"{s}\""))?;
            *ItemCategory::ALL
                .iter()
                .find(|c| **c as i32 == n)
                .ok_or_else(|| anyhow!("unknown item type {n} in \"{s}\""))?
        }
    };
    Ok(encode_item_no(cat, id))
}

pub fn read_pack(path: &Path) -> Result<Pack> {
    let json = std::fs::read_to_string(path).with_context(|| format!("reading {}", path.display()))?;
    let pack: Pack = serde_json::from_str(&json).with_context(|| format!("parsing {}", path.display()))?;
    let mut seen = std::collections::HashSet::new();
    for q in &pack.quests {
        if q.key.trim().is_empty() {
            bail!("a quest in the pack has an empty key");
        }
        if !seen.insert(q.key.as_str()) {
            bail!("key \"{}\" appears twice in the pack", q.key);
        }
        if q.objectives.is_empty() {
            bail!("quest \"{}\" has no objectives", q.key);
        }
    }
    Ok(pack)
}

/// Existing quests created from a pack: key -> (quest SN, spec), from the manifests.
fn keyed_quests(root: &Path) -> BTreeMap<String, QuestSpec> {
    crate::manifest::list_manifests(root)
        .into_iter()
        .filter_map(|m| m.key.clone().map(|k| (k, m.spec)))
        .collect()
}

/// Build a spec for `q`, numbered for the data set as it stands.
fn build_spec(
    ds: &DataSet,
    q: &PackQuest,
    one_time_switch: Option<i32>,
    requires: Vec<Prerequisite>,
) -> Result<QuestSpec> {
    let mut hunt_offset = 0;
    let mut objectives = Vec::new();
    for o in &q.objectives {
        objectives.push(match o {
            PackObjective::Hunt {
                monster,
                count,
                token_name,
                token_desc,
                token_icon,
            } => {
                let m = ds
                    .find_monster(*monster)
                    .ok_or_else(|| anyhow!("quest \"{}\": no monster {monster}", q.key))?;
                let token_item_sn = ds.token_item_sn_at(hunt_offset);
                hunt_offset += 1;
                Objective::Hunt {
                    monster_id: *monster,
                    count: *count,
                    token_item_sn,
                    token_name: token_name.clone().unwrap_or_else(|| format!("{} Mark", m.name)),
                    token_desc: token_desc
                        .clone()
                        .unwrap_or_else(|| format!("Proof of a defeated {}.", m.name)),
                    token_icon: *token_icon,
                    chain_into_existing: !m.dead_event_is_free(),
                }
            }
            PackObjective::Fetch { item, count, consume } => {
                let item_sn = parse_item(item).with_context(|| format!("quest \"{}\"", q.key))?;
                let name = ds
                    .item_db
                    .all()
                    .find(|it| encode_item_no(it.category, it.id) == item_sn)
                    .map(|it| it.name.clone())
                    .unwrap_or_default();
                Objective::Fetch {
                    item_sn,
                    item_name: name,
                    count: *count,
                    consume: *consume,
                }
            }
        });
    }
    let primary = objectives.remove(0);
    let (kind, count) = match primary {
        Objective::Hunt {
            monster_id,
            count,
            token_item_sn,
            token_name,
            token_desc,
            token_icon,
            chain_into_existing,
        } => (
            QuestKind::Hunt {
                monster_id,
                token_item_sn,
                token_name,
                token_desc,
                token_icon,
                chain_into_existing,
            },
            count,
        ),
        Objective::Fetch {
            item_sn,
            item_name,
            count,
            consume,
        } => (
            QuestKind::Fetch {
                item_sn,
                item_name,
                consume,
            },
            count,
        ),
    };
    let reward_item = match &q.reward_item {
        Some(r) => Some((
            parse_item(&r.item).with_context(|| format!("quest \"{}\" reward", q.key))?,
            r.qty,
        )),
        None => None,
    };
    Ok(QuestSpec {
        quest_sn: ds.next_free_quest_sn(),
        kind,
        count,
        reward_exp: q.reward_exp,
        reward_zuly: q.reward_zuly,
        reward_item,
        one_time_switch,
        requires,
        extra_objectives: objectives,
        title: q.title.clone(),
        start_text: q.start_text.clone(),
        progress_text: q.progress_text.clone(),
        complete_text: q.complete_text.clone(),
    })
}

/// A dry run writes nothing, so the data set is advanced by hand to what an
/// apply would have left: the quest row, its token rows, its switch wiring.
fn simulate_apply(ds: &mut DataSet, spec: &QuestSpec) {
    ds.quest_row_count += 1;
    let tokens = ds.item_db.by_category.entry(ItemCategory::QuestItem).or_default();
    let mut add = |sn: i32| {
        tokens.push(Item {
            category: ItemCategory::QuestItem,
            id: sn % 1000,
            name: "(dry run)".into(),
        })
    };
    if let QuestKind::Hunt { token_item_sn, .. } = &spec.kind {
        add(*token_item_sn);
    }
    for o in &spec.extra_objectives {
        if let Objective::Hunt { token_item_sn, .. } = o {
            add(*token_item_sn);
        }
    }
    ds.quest_switches.insert(
        spec.quest_sn,
        QuestSwitches {
            own: spec.one_time_switch,
            requires: spec.requires.iter().map(|p| p.switch_no).collect(),
        },
    );
}

fn giver_text(q: &PackQuest, g: &PackGiver) -> GiverText {
    GiverText {
        greeting: q.start_text.clone(),
        in_progress: q.progress_text.clone(),
        after_complete: q.complete_text.clone(),
        hook: g.hook.clone(),
        accept: g.accept.clone(),
        decline: g.decline.clone(),
        after_accept: g.after_accept.clone(),
        turnin: g.turnin.clone(),
        progress: g.progress.clone(),
    }
}

/// Apply a pack. Returns the report lines; bails at the first quest that fails
/// verification (nothing of that quest is written).
pub fn apply_pack(root: &Path, pack: &Pack, replace: bool, dry_run: bool) -> Result<Vec<String>> {
    let mut log = Vec::new();
    let mut keyed = keyed_quests(root);
    let mut ds = DataSet::load(root)?;
    // Switches handed out during a dry run (nothing records them on disk).
    let mut reserved: Vec<i32> = Vec::new();
    // A dry run has created (in memory only) at least one quest.
    let mut simulated = false;
    // key -> (quest SN, completion switch) for quests in this pack.
    let mut done: BTreeMap<String, (i32, Option<i32>)> = BTreeMap::new();

    for q in &pack.quests {
        let existing = keyed.get(&q.key).cloned();

        // Prerequisites by key: from this run or from an earlier one.
        let mut requires = Vec::new();
        for k in &q.requires {
            let (sn, sw) = match done.get(k) {
                Some(v) => *v,
                None => match keyed.get(k) {
                    Some(s) => (s.quest_sn, s.one_time_switch),
                    None => bail!("quest \"{}\" requires \"{k}\", which is neither earlier in the pack nor already created", q.key),
                },
            };
            let sw = sw.ok_or_else(|| {
                anyhow!("quest \"{}\" requires \"{k}\", which is not one-time (\"once\": true)", q.key)
            })?;
            requires.push(Prerequisite { quest_sn: sn, switch_no: sw });
        }

        if let Some(old) = &existing {
            if !replace {
                log.push(format!(
                    "KEEP \"{}\" = quest #{} (exists; --replace re-creates it from the pack)",
                    q.key, old.quest_sn
                ));
                if let Some(g) = &q.giver {
                    let complete = generate(old).complete_trigger;
                    let rep = crate::write::append_quest_to_npc_dialog(
                        root,
                        g.npc,
                        old.quest_sn,
                        &complete,
                        Some(&giver_text(q, g)),
                        dry_run,
                    )
                    .with_context(|| format!("quest \"{}\": dialog option on npc {}", q.key, g.npc))?;
                    log.extend(rep.changes.into_iter().map(|c| format!("  {c}")));
                }
                done.insert(q.key.clone(), (old.quest_sn, old.one_time_switch));
                continue;
            }
            if dry_run {
                log.push(format!("REPLACE \"{}\": would delete quest #{} first", q.key, old.quest_sn));
            } else {
                let rep = crate::write::delete_quest(root, old.quest_sn, false)
                    .with_context(|| format!("quest \"{}\": deleting #{} to replace it", q.key, old.quest_sn))?;
                log.push(format!("REPLACE \"{}\": deleted quest #{}", q.key, old.quest_sn));
                log.extend(rep.changes.into_iter().map(|c| format!("  {c}")));
                keyed.remove(&q.key);
                ds = DataSet::load(root)?;
            }
        }

        // One-time: keep a replaced quest's switch, else allocate a fresh one.
        let one_time_switch = if !q.once {
            None
        } else if let Some(sw) = existing.as_ref().and_then(|o| o.one_time_switch) {
            Some(sw)
        } else {
            let used = crate::write::used_switches(root)?;
            Some(
                (0..512)
                    .find(|s| !used.contains(s) && !reserved.contains(s))
                    .ok_or_else(|| anyhow!("no free character quest-switch (all 512 in use)"))?,
            )
        };

        let spec = build_spec(&ds, q, one_time_switch, requires)?;
        let gen = generate(&spec);
        let issues = verify::verify(&ds, &spec, &gen);
        // A dry-run replace cannot delete, so the old QSD is still in the way.
        let blocking: Vec<_> = issues
            .iter()
            .filter(|i| i.level == Level::Error)
            .filter(|i| !(dry_run && existing.is_some() && i.message.contains("already exists")))
            .collect();
        for i in &issues {
            let tag = if i.level == Level::Error { "ERROR" } else { "warn" };
            log.push(format!("  [{tag}] \"{}\": {}", q.key, i.message));
        }
        if !blocking.is_empty() {
            bail!("quest \"{}\" failed verification — stopping (earlier quests stay applied)", q.key);
        }

        log.push(format!(
            "CREATE \"{}\" = quest #{}{}{}",
            q.key,
            spec.quest_sn,
            spec.one_time_switch.map_or(String::new(), |s| format!(", one-time (switch {s})")),
            if spec.requires.is_empty() {
                String::new()
            } else {
                format!(
                    ", requires {}",
                    spec.requires.iter().map(|p| format!("#{}", p.quest_sn)).collect::<Vec<_>>().join(" ")
                )
            }
        ));
        if dry_run && simulated {
            // apply_quest checks its numbers against the files, which a dry run
            // never advances; the numbering and verification above are simulated.
            log.push("  (file-level preview skipped: it depends on the quests above)".into());
        } else {
            let rep = crate::write::apply_quest(root, &spec, &gen, dry_run)
                .with_context(|| format!("quest \"{}\"", q.key))?;
            log.extend(rep.changes.into_iter().map(|c| format!("  {c}")));
        }

        if !dry_run {
            crate::manifest::set_key(root, spec.quest_sn, &q.key)?;
        }
        if let Some(g) = &q.giver {
            let rep = crate::write::append_quest_to_npc_dialog(
                root,
                g.npc,
                spec.quest_sn,
                &gen.complete_trigger,
                Some(&giver_text(q, g)),
                dry_run,
            )
            .with_context(|| format!("quest \"{}\": dialog option on npc {}", q.key, g.npc))?;
            log.extend(rep.changes.into_iter().map(|c| format!("  {c}")));
        }

        done.insert(q.key.clone(), (spec.quest_sn, spec.one_time_switch));
        if dry_run {
            if let Some(s) = spec.one_time_switch {
                reserved.push(s);
            }
            simulate_apply(&mut ds, &spec);
            simulated = true;
        } else {
            ds = DataSet::load(root)?;
        }
    }
    Ok(log)
}
