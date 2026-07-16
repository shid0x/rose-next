//! QSD trigger classification + dialog trigger harvest.
//!
//! Rust mirror of the client's NPC overhead quest-icon logic
//! (`src/client/event/cevent.cpp`: `HarvestLuaStrings` / `ClassifyQuestTrigger`)
//! — **keep the two in sync**. Used by:
//!
//! - the wizard's giver-NPC picker and the `con-triggers` CLI, to show which
//!   quests an NPC's existing dialog already offers;
//! - `verify.rs`, to guarantee generated quests classify as accept / turn-in —
//!   otherwise the client's overhead "!" / "?" icons silently never show.
//!
//! ## How it works
//!
//! Trigger names a dialog can fire are readable string constants inside the
//! `.CON` Lua blob even when it is compiled Lua 4 bytecode (binary length/type
//! bytes separate the constants, so printable runs come out whole). Each
//! harvested name that resolves to a QSD trigger is classified by the reward
//! composition of the trigger's `check_next` fail-over chain:
//!
//! - `REWD_000 op 1` (add quest) → **accept** ("!")
//! - `COND_000` (has quest) present AND (`REWD_000 op 2` swap / `op 3`
//!   reset-start, or `op 0` delete *plus* a grant — item give, ability, calc
//!   reward, hp/mp, skill) → **turn-in** ("?")
//! - delete-only chains are quest-*abandon* dialog options → unclassified
//! - `op 4` is select-context only and never affects classification
//!
//! Measured over retail data: 737 dialog-referenced triggers → 152 accepts,
//! 281 turn-ins, 7 abandon-only filtered.

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{anyhow, Result};

use crate::convo::ConFile;
use crate::qsd::{QsdFile, Trigger};

/// What a trigger does to the quest log, per the rules above.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Classification {
    /// Offers a new quest (`REWD_000 op 1`).
    pub accept: bool,
    /// Completes or advances a registered quest.
    pub turn_in: bool,
    /// Quest SN granted by the accept (`REWD_000 op 1`), if any.
    pub add_quest_sn: Option<i32>,
    /// Quest SN required by `COND_000`, if any (the quest a turn-in works on).
    pub required_quest_sn: Option<i32>,
}

impl Classification {
    pub fn is_quest_option(&self) -> bool {
        self.accept || self.turn_in
    }
}

fn payload_i32(p: &[u8]) -> Option<i32> {
    p.get(..4).map(|b| i32::from_le_bytes(b.try_into().unwrap()))
}

/// Classify one entry trigger together with its `check_next` fail-over chain —
/// the same walk `CQuestDATA::CheckQUEST` performs.
fn classify_chain(chain: &[&Trigger]) -> Classification {
    let mut c = Classification::default();
    let (mut grant, mut delete, mut advance) = (false, false, false);

    for t in chain {
        for e in &t.conditions {
            if e.type_id() == 0 {
                c.required_quest_sn = c.required_quest_sn.or_else(|| payload_i32(&e.payload));
            }
        }
        for e in &t.rewards {
            match e.type_id() {
                0 => match e.payload.get(4) {
                    Some(0) => delete = true,
                    Some(1) => {
                        c.accept = true;
                        c.add_quest_sn = c.add_quest_sn.or_else(|| payload_i32(&e.payload));
                    }
                    Some(2) | Some(3) => advance = true,
                    _ => {} // op 4 = select-context only
                },
                1 => {
                    if e.payload.get(4) == Some(&1) {
                        grant = true; // item give (op 0 = remove)
                    }
                }
                3 | 5 | 6 | 14 => grant = true, // ability / calc reward / hp-mp / skill
                _ => {}
            }
        }
    }

    let has_quest_cond = c.required_quest_sn.is_some();
    c.turn_in = has_quest_cond && (advance || (delete && grant));
    c
}

fn trigger_display_name(t: &Trigger) -> String {
    let end = t.name.iter().position(|&b| b == 0).unwrap_or(t.name.len());
    String::from_utf8_lossy(&t.name[..end]).into_owned()
}

/// Classification of every trigger (by name) across one or more QSD files.
#[derive(Debug, Default)]
pub struct TriggerIndex {
    map: HashMap<String, Classification>,
}

impl TriggerIndex {
    /// Index a single parsed QSD (e.g. a freshly generated one).
    pub fn from_qsd(qsd: &QsdFile) -> TriggerIndex {
        let mut idx = TriggerIndex::default();
        idx.add_qsd(qsd);
        idx
    }

    /// Index every `.QSD` under the data root's `3DDATA/QUESTDATA`. Files that
    /// fail to parse are skipped with a warning (mirrors the game, which also
    /// loads what it can).
    pub fn load(root: &Path) -> Result<TriggerIndex> {
        let dir = resolve_questdata_dir(root)?;
        let mut idx = TriggerIndex::default();
        for entry in fs::read_dir(&dir)?.flatten() {
            let path = entry.path();
            let is_qsd = path
                .extension()
                .map(|e| e.eq_ignore_ascii_case("qsd"))
                .unwrap_or(false);
            if !is_qsd {
                continue;
            }
            match fs::read(&path).map_err(anyhow::Error::from).and_then(|b| QsdFile::parse(&b)) {
                Ok(qsd) => idx.add_qsd(&qsd),
                Err(e) => eprintln!("warning: skipping {}: {e:#}", path.display()),
            }
        }
        Ok(idx)
    }

    fn add_qsd(&mut self, qsd: &QsdFile) {
        for pat in &qsd.patterns {
            for (i, t) in pat.triggers.iter().enumerate() {
                // Entry + fail-over tail: follow check_next through the pattern.
                let mut chain: Vec<&Trigger> = vec![t];
                let mut j = i;
                while pat.triggers[j].check_next != 0 && j + 1 < pat.triggers.len() {
                    j += 1;
                    chain.push(&pat.triggers[j]);
                }
                self.map
                    .insert(trigger_display_name(t), classify_chain(&chain));
            }
        }
    }

    pub fn classify(&self, name: &str) -> Option<Classification> {
        self.map.get(name).copied()
    }

    pub fn len(&self) -> usize {
        self.map.len()
    }

    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }
}

/// Candidate trigger-name strings in a Lua blob.
///
/// Two extractions, results concatenated:
/// - printable-ASCII runs (2..=32 chars) — recovers string constants from
///   compiled **bytecode**, where binary length/type bytes separate them (this
///   is what the client does);
/// - `"quoted"` literals (2..=32 chars) — needed for Lua **source** (editor
///   replace-mode givers and QEX1 appendixes), where the whole line is one
///   printable run and the constant would be swallowed. The client doesn't
///   need this: source-mode conversations are quest-editor output, covered by
///   its `CHK_*` check-function path.
pub fn harvest_lua_strings(blob: &[u8]) -> Vec<String> {
    let mut out = Vec::new();

    let mut start: Option<usize> = None;
    for i in 0..=blob.len() {
        let printable = i < blob.len() && (0x21..=0x7e).contains(&blob[i]);
        match (printable, start) {
            (true, None) => start = Some(i),
            (false, Some(s)) => {
                let run = i - s;
                if (2..=32).contains(&run) {
                    out.push(String::from_utf8_lossy(&blob[s..i]).into_owned());
                }
                start = None;
            }
            _ => {}
        }
    }

    let mut open: Option<usize> = None;
    for (i, &b) in blob.iter().enumerate() {
        if b != b'"' {
            continue;
        }
        match open {
            None => open = Some(i + 1),
            Some(s) => {
                let span = &blob[s..i];
                if (2..=32).contains(&span.len())
                    && span.iter().all(|&c| (0x20..=0x7e).contains(&c))
                {
                    out.push(String::from_utf8_lossy(span).into_owned());
                }
                open = None;
            }
        }
    }

    out
}

/// A quest option a dialog offers (harvested trigger that classified).
#[derive(Debug, Clone)]
pub struct QuestOffer {
    pub trigger: String,
    pub class: Classification,
}

/// The quest options a conversation's Lua (main blob + QEX1 appendix) can fire,
/// deduped, in first-appearance order.
pub fn dialog_offers(con: &ConFile, index: &TriggerIndex) -> Vec<QuestOffer> {
    let mut names = harvest_lua_strings(&con.lua);
    names.extend(harvest_lua_strings(&con.appendix));

    let mut seen = std::collections::HashSet::new();
    let mut out = Vec::new();
    for name in names {
        if !seen.insert(name.clone()) {
            continue;
        }
        if let Some(class) = index.classify(&name) {
            if class.is_quest_option() {
                out.push(QuestOffer {
                    trigger: name,
                    class,
                });
            }
        }
    }
    out
}

/// Locate `3DDATA/QUESTDATA` under a data root.
pub fn resolve_questdata_dir(root: &Path) -> Result<PathBuf> {
    for c in [
        root.join("3DDATA").join("QUESTDATA"),
        root.join("3ddata").join("questdata"),
        root.join("QUESTDATA"),
    ] {
        if c.is_dir() {
            return Ok(c);
        }
    }
    Err(anyhow!(
        "could not find 3DDATA/QUESTDATA under '{}'",
        root.display()
    ))
}

/// Locate `3DDATA/EVENT` (the `.CON` conversations) under a data root.
pub fn resolve_event_dir(root: &Path) -> Result<PathBuf> {
    for c in [
        root.join("3DDATA").join("EVENT"),
        root.join("3ddata").join("event"),
        root.join("EVENT"),
    ] {
        if c.is_dir() {
            return Ok(c);
        }
    }
    Err(anyhow!(
        "could not find 3DDATA/EVENT under '{}'",
        root.display()
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::gen::{generate, QuestKind, QuestSpec};

    fn spec(kind: QuestKind, reward_exp: i32) -> QuestSpec {
        QuestSpec {
            quest_sn: 6000,
            kind,
            count: 5,
            reward_exp,
            reward_zuly: 0,
            reward_item: None,
            one_time_switch: None,
            extra_objectives: vec![],
            title: "test".into(),
            start_text: String::new(),
            progress_text: String::new(),
            complete_text: String::new(),
        }
    }

    fn hunt_kind() -> QuestKind {
        QuestKind::Hunt {
            monster_id: 1,
            token_item_sn: 13_999,
            token_name: "tok".into(),
            token_desc: String::new(),
            token_icon: None,
            chain_into_existing: false,
        }
    }

    /// The client's NPC overhead quest icons depend on generated triggers
    /// classifying accept / turn-in. If these fail, icons silently vanish for
    /// editor quests — fix the generator (or, deliberately, this classifier AND
    /// the client's cevent.cpp mirror together).
    #[test]
    fn generated_hunt_classifies_for_icons() {
        let g = generate(&spec(hunt_kind(), 100));
        let idx = TriggerIndex::from_qsd(&g.qsd);

        let reg = idx.classify(&g.register_trigger).unwrap();
        assert!(reg.accept && !reg.turn_in);
        assert_eq!(reg.add_quest_sn, Some(6000));

        let comp = idx.classify(&g.complete_trigger).unwrap();
        assert!(comp.turn_in && !comp.accept);
        assert_eq!(comp.required_quest_sn, Some(6000));

        // The kill trigger only grants a token mid-quest — neither icon.
        let kill = idx.classify(g.kill_trigger.as_deref().unwrap()).unwrap();
        assert!(!kill.accept && !kill.turn_in);
    }

    #[test]
    fn generated_fetch_classifies_for_icons() {
        let g = generate(&spec(
            QuestKind::Fetch {
                item_sn: 12_101,
                item_name: "moon stone".into(),
                consume: true,
            },
            250,
        ));
        let idx = TriggerIndex::from_qsd(&g.qsd);
        assert!(idx.classify(&g.register_trigger).unwrap().accept);
        assert!(idx.classify(&g.complete_trigger).unwrap().turn_in);
    }

    /// A quest with no rewards at all completes via a delete-only trigger, which
    /// is indistinguishable from a quest-abandon option — verify() warns on it.
    #[test]
    fn no_reward_quest_is_not_a_turn_in() {
        let g = generate(&spec(hunt_kind(), 0));
        let idx = TriggerIndex::from_qsd(&g.qsd);
        assert!(!idx.classify(&g.complete_trigger).unwrap().turn_in);
    }

    #[test]
    fn harvest_finds_bytecode_and_source_constants() {
        // Bytecode-style: binary bytes separate the constants.
        let bytecode = b"\x04\x00\x001001-01\x00\x02\x7f5512-3\x00";
        let names = harvest_lua_strings(bytecode);
        assert!(names.contains(&"1001-01".to_string()));
        assert!(names.contains(&"5512-3".to_string()));

        // Source-style: the constant sits inside one long printable run and is
        // only recoverable from its quotes.
        let source = br#"if QF_checkQuestCondition("5512-1") < 1 then return 0 end"#;
        assert!(harvest_lua_strings(source).contains(&"5512-1".to_string()));
    }
}

/// Resolve a conversation reference (zone-IFO basename like `EM22-004` or
/// `QG5503.con`, or a full/relative path) to an existing `.CON` file.
pub fn resolve_con_path(root: &Path, name: &str) -> Result<PathBuf> {
    let direct = PathBuf::from(name);
    if direct.is_file() {
        return Ok(direct);
    }
    let dir = resolve_event_dir(root)?;
    let mut candidates = vec![name.to_string()];
    if !name.to_ascii_lowercase().ends_with(".con") {
        candidates.push(format!("{name}.CON"));
    }
    if let Ok(rd) = fs::read_dir(&dir) {
        for entry in rd.flatten() {
            let fname = entry.file_name().to_string_lossy().into_owned();
            if candidates.iter().any(|c| fname.eq_ignore_ascii_case(c)) {
                return Ok(entry.path());
            }
        }
    }
    Err(anyhow!("no .CON named '{}' in {}", name, dir.display()))
}
