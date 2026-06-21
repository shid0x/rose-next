//! Phase 3 — typed entity builders + Hunt-quest template generator.
//!
//! This layer turns high-level intent ("kill N of monster X for these rewards")
//! into an in-memory [`qsd::QsdFile`] plus the side-data the Phase-4 writer needs
//! (NPC col-41 assignment, token quest-item, LIST_Quest row, STL text). It writes
//! nothing to disk.
//!
//! ## Encoding rules
//!
//! Each builder emits a `qsd::Entity` whose `payload` is the raw C-struct body
//! **after** the 8-byte `{size,type}` header, including struct padding. The
//! layouts are taken from `STR_COND_*` / `STR_REWD_*` in
//! `src/common/shared/io_quest.h` and validated byte-for-byte against the real
//! retail quest 105 kill trigger (`QN-021.QSD`, see PROGRESS.md). We zero our
//! padding bytes; retail left uninitialized `0xCC`/`0xCD` there, but the game
//! never reads padding so the behavior is identical.
//!
//! ## The Hunt model (item-collection, matching retail)
//!
//! Each kill of the target monster grants 1 of a dedicated **token quest-item**
//! while the player holds `< N`; the quest completes when they hold `N`.
//! Finishing the quest (`REWD_000` op 0 = `Init`) clears the quest slot and its
//! token items in one step.

use crate::qsd::{Entity, Pattern, QsdFile, Trigger};

// --- comparison operators (Check_QuestOP, io_quest.cpp:44) ---
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum CmpOp {
    Eq = 0,
    Gt = 1,
    Ge = 2,
    Lt = 3,
    Le = 4,
    Ne = 10,
}

// --- REWD_000 quest operations (F_QSTREWD000, io_quest.cpp:1230) ---
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum QuestOp {
    /// `Init()` the current quest slot — clears its data + token items. Used as
    /// "finish/complete" since it frees the registration.
    Finish = 0,
    /// `Quest_Append()` — register the quest in a new slot.
    Register = 1,
    /// `SetID(.., false)` — set current slot's id without reset.
    SetId = 2,
    /// `Init()` + `SetID(.., true)` — clean (re)start in the current slot.
    ResetSet = 3,
    /// Select an already-registered quest as the "current" one for the rest of
    /// the trigger (needed before item/reward ops that read `m_pQUEST`).
    Select = 4,
}

/// `iWhere` for a quest-inventory item check: 0 = `EQUIP_IDX_NULL`, which is
/// outside the equip range `[1,12)`, so a quest-type item falls through to the
/// quest-inventory branch (Check_QuestITEM, io_quest.cpp:222). Retail used 13
/// (also out of range) for the same effect; 0 is clearer.
const ITEM_WHERE_QUEST_INVENTORY: i32 = 0;

/// Small little-endian payload writer.
struct W(Vec<u8>);
impl W {
    fn new() -> Self {
        W(Vec::new())
    }
    fn u8(mut self, v: u8) -> Self {
        self.0.push(v);
        self
    }
    fn i16(mut self, v: i16) -> Self {
        self.0.extend_from_slice(&v.to_le_bytes());
        self
    }
    fn i32(mut self, v: i32) -> Self {
        self.0.extend_from_slice(&v.to_le_bytes());
        self
    }
    fn u32(mut self, v: u32) -> Self {
        self.0.extend_from_slice(&v.to_le_bytes());
        self
    }
    /// Zero padding bytes (struct alignment).
    fn pad(mut self, n: usize) -> Self {
        self.0.resize(self.0.len() + n, 0);
        self
    }
    fn done(self) -> Vec<u8> {
        self.0
    }
}

fn cond(etype: i32, payload: Vec<u8>) -> Entity {
    Entity { etype, payload }
}
fn rewd(etype: i32, payload: Vec<u8>) -> Entity {
    Entity { etype, payload }
}

// ---------------------------------------------------------------------------
// Condition builders
// ---------------------------------------------------------------------------

/// COND_000 — the quest must be registered. `STR_COND_000 { int iQuestSN }`.
pub fn cond_quest_registered(quest_sn: i32) -> Entity {
    cond(0, W::new().i32(quest_sn).done())
}

/// COND_004 — compare the player's held count of `item_sn` against `count`.
/// `STR_COND_004 { int iDataCnt; STR_ITEM_DATA[1] }`,
/// `STR_ITEM_DATA { u32 uiItemSN; i32 iWhere; i32 iRequestCnt; u8 btOp; }`
/// (13 bytes + 3 pad = a 16-byte array element; total payload 20 bytes).
pub fn cond_item_count(item_sn: i32, count: i32, op: CmpOp) -> Entity {
    let p = W::new()
        .i32(1) // iDataCnt
        .u32(item_sn as u32) // uiItemSN
        .i32(ITEM_WHERE_QUEST_INVENTORY) // iWhere
        .i32(count) // iRequestCnt
        .u8(op as u8) // btOp
        .pad(3)
        .done();
    debug_assert_eq!(p.len(), 20);
    cond(4, p)
}

// ---------------------------------------------------------------------------
// Reward builders
// ---------------------------------------------------------------------------

/// REWD_000 — quest add/select/finish. `STR_REWD_000 { int iQuestSN; u8 btOp; }`
/// (4 + 1 + 3 pad = 8 bytes).
pub fn rewd_quest_op(quest_sn: i32, op: QuestOp) -> Entity {
    let p = W::new().i32(quest_sn).u8(op as u8).pad(3).done();
    debug_assert_eq!(p.len(), 8);
    rewd(0, p)
}

/// REWD_001 — give (`op=1`) or remove (`op=0`) `qty` of `item_sn`.
/// `STR_REWD_001 { u32 uiItemSN; u8 btOp; short nDupCNT; u8 btPartyOpt; }`
/// (4 + 1 + 1 pad + 2 + 1 + 3 pad = 12 bytes).
fn rewd_item(item_sn: i32, qty: i16, op: u8) -> Entity {
    let p = W::new()
        .u32(item_sn as u32) // uiItemSN
        .u8(op) // btOp
        .pad(1)
        .i16(qty) // nDupCNT
        .u8(0) // btPartyOpt (0 = not shared with party)
        .pad(3)
        .done();
    debug_assert_eq!(p.len(), 12);
    rewd(1, p)
}

/// Give `qty` of an item (REWD_001 op 1).
pub fn rewd_give_item(item_sn: i32, qty: i16) -> Entity {
    rewd_item(item_sn, qty, 1)
}

/// Remove `qty` of an item (REWD_001 op 0). Quest-type items are removed from
/// the quest inventory.
pub fn rewd_remove_item(item_sn: i32, qty: i16) -> Entity {
    rewd_item(item_sn, qty, 0)
}

/// REWD_005 — calculated EXP/zuly/item reward.
/// `STR_REWD_005 { u8 btTarget; u8 btEquation; int iValue; int iItemSN;
/// u8 btPartyOpt; short nItemOpt; }`
/// layout: target(1) eq(1) [pad2] iValue(4) iItemSN(4) party(1) [pad1]
/// nItemOpt(2) = 16 bytes.
fn rewd_005(target: u8, equation: u8, value: i32, item_sn: i32, item_opt: i16) -> Entity {
    let p = W::new()
        .u8(target)
        .u8(equation)
        .pad(2)
        .i32(value)
        .i32(item_sn)
        .u8(0) // btPartyOpt
        .pad(1)
        .i16(item_opt)
        .done();
    debug_assert_eq!(p.len(), 16);
    rewd(5, p)
}

/// EXP reward (target 0). Equation 0 = "base value priority" — grants at least
/// `exp` plus the game's standard charm/fame/level bonus (the equation retail
/// quests use). Requires the quest to be selected first.
pub fn rewd_exp(exp: i32) -> Entity {
    rewd_005(0, 0, exp, 0, 0)
}

/// Zuly reward (target 1). Equation 3 = "money base value" — at least `zuly`
/// plus the standard bonus, independent of the per-quest count var (unlike eq 2).
/// Requires the quest to be selected first.
pub fn rewd_zuly(zuly: i32) -> Entity {
    rewd_005(1, 3, zuly, 0, 0)
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

/// What the quest asks the player to do.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub enum QuestKind {
    /// "Kill N of monster X." Each kill drops one dedicated token quest-item;
    /// completion checks for N tokens. The token item is created by the writer.
    Hunt {
        /// Target monster's NPC id (kill trigger is wired into its col 41).
        monster_id: i32,
        /// Dedicated token quest-item SN (`type*1000+id`, type 13). One per quest.
        token_item_sn: i32,
        token_name: String,
        token_desc: String,
        /// Icon override for the token; `None` keeps the cloned template icon.
        token_icon: Option<i32>,
        /// The monster already has a dead-event trigger — chain our kill trigger
        /// onto its existing chain (inserted into the host QSD) instead of
        /// claiming col 41. See the writer.
        chain_into_existing: bool,
    },
    /// "Bring N of an existing item X." No monster, no token; completion checks
    /// the player's inventory and (optionally) consumes the items on turn-in.
    Fetch {
        /// SN (`type*1000+id`) of the item the player must bring.
        item_sn: i32,
        /// Display name of that item (preview/log only).
        item_name: String,
        /// Remove the N items from the player on turn-in.
        consume: bool,
    },
}

/// Inputs for generating a quest (Hunt or Fetch).
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct QuestSpec {
    /// Quest SN (== the new LIST_QUEST.STB row index).
    pub quest_sn: i32,
    pub kind: QuestKind,
    /// How many to kill / collect.
    pub count: i32,

    pub reward_exp: i32,
    pub reward_zuly: i32,
    /// Optional bonus item reward: (item_sn, qty).
    pub reward_item: Option<(i32, i16)>,

    // Text (consumed by the Phase-4 STL writer; not part of the QSD bytes).
    pub title: String,
    pub start_text: String,
    pub progress_text: String,
    pub complete_text: String,
}

/// The full output of generating a quest: the QSD plus the trigger names the
/// Phase-4 writer needs. Kind-specific data (monster/token) lives in
/// [`QuestSpec::kind`].
#[derive(Debug, Clone)]
pub struct GeneratedQuest {
    pub qsd: QsdFile,
    /// Suggested QSD filename (registered in LIST_QuestDATA.STB).
    pub qsd_filename: String,

    pub register_trigger: String,
    pub complete_trigger: String,
    /// Hunt only: the kill trigger's name.
    pub kill_trigger: Option<String>,
    /// Hunt + chaining only: the kill `Trigger` to insert into the monster's
    /// existing host QSD (instead of putting it in this quest's QSD and claiming
    /// col 41). `None` for the non-chaining case (the kill trigger is in `qsd`).
    pub host_kill_trigger: Option<Trigger>,
    /// Quest SN / row to append in LIST_Quest.STB.
    pub quest_sn: i32,
}

/// Trigger name following the retail `"<SN>-<NN>"` convention.
fn trigger_name(quest_sn: i32, suffix: u8) -> String {
    format!("{quest_sn}-{suffix}")
}

/// Name bytes as stored in the QSD: ASCII + a trailing NUL (retail stores the
/// length including the terminator). The col-41 / hash form has no NUL; both
/// hash identically because `StrToHashKey` stops at the terminator.
fn name_bytes(s: &str) -> Vec<u8> {
    let mut b = s.as_bytes().to_vec();
    b.push(0);
    b
}

/// The register trigger (no conditions): just add the quest.
fn register_trigger_entity(spec: &QuestSpec, name: &str) -> Trigger {
    Trigger {
        check_next: 0,
        name: name_bytes(name),
        conditions: vec![],
        rewards: vec![rewd_quest_op(spec.quest_sn, QuestOp::Register)],
    }
}

/// Shared completion-reward sequence: select → exp/zuly/item → `extra` → finish.
/// `extra` lets Fetch insert a "remove the brought items" reward before finish.
fn complete_reward_list(spec: &QuestSpec, extra: Vec<Entity>) -> Vec<Entity> {
    let mut r = vec![rewd_quest_op(spec.quest_sn, QuestOp::Select)];
    if spec.reward_exp > 0 {
        r.push(rewd_exp(spec.reward_exp));
    }
    if spec.reward_zuly > 0 {
        r.push(rewd_zuly(spec.reward_zuly));
    }
    if let Some((item_sn, qty)) = spec.reward_item {
        if qty > 0 {
            r.push(rewd_give_item(item_sn, qty));
        }
    }
    r.extend(extra);
    // Finish LAST: Init() clears the quest slot (and its quest-inventory tokens).
    r.push(rewd_quest_op(spec.quest_sn, QuestOp::Finish));
    r
}

fn qsd_for(spec: &QuestSpec, triggers: Vec<Trigger>) -> QsdFile {
    QsdFile {
        size_field: 12, // constant across all retail QSDs
        description: spec.title.as_bytes().to_vec(),
        patterns: vec![Pattern {
            name: format!("quest {}", spec.quest_sn).into_bytes(),
            triggers,
        }],
        trailing: vec![],
    }
}

/// Generate a quest (Hunt or Fetch) from its spec.
pub fn generate(spec: &QuestSpec) -> GeneratedQuest {
    match &spec.kind {
        QuestKind::Hunt {
            token_item_sn,
            chain_into_existing,
            ..
        } => generate_hunt(spec, *token_item_sn, *chain_into_existing),
        QuestKind::Fetch {
            item_sn, consume, ..
        } => generate_fetch(spec, *item_sn, *consume),
    }
}

/// Hunt: register / kill / complete. Each kill grants a token while the player
/// holds `< N`; completion checks `>= N`. The token is a quest-type item, so
/// Finish/Init clears it — no explicit removal needed.
///
/// When `chain` is false the kill trigger goes in this quest's QSD and claims the
/// monster's col 41. When true the kill trigger is returned in
/// `host_kill_trigger` for the writer to splice into the monster's existing chain
/// (and this quest's QSD holds only register + complete).
fn generate_hunt(spec: &QuestSpec, token_item_sn: i32, chain: bool) -> GeneratedQuest {
    let register_trigger = trigger_name(spec.quest_sn, 1);
    let kill_trigger = trigger_name(spec.quest_sn, 2);
    let complete_trigger = trigger_name(spec.quest_sn, 3);

    let t_kill = Trigger {
        check_next: 0,
        name: name_bytes(&kill_trigger),
        conditions: vec![
            cond_quest_registered(spec.quest_sn),
            cond_item_count(token_item_sn, spec.count, CmpOp::Lt),
        ],
        rewards: vec![
            rewd_quest_op(spec.quest_sn, QuestOp::Select),
            rewd_give_item(token_item_sn, 1),
        ],
    };
    let t_complete = Trigger {
        check_next: 0,
        name: name_bytes(&complete_trigger),
        conditions: vec![
            cond_quest_registered(spec.quest_sn),
            cond_item_count(token_item_sn, spec.count, CmpOp::Ge),
        ],
        rewards: complete_reward_list(spec, vec![]),
    };

    let register = register_trigger_entity(spec, &register_trigger);
    let (triggers, host_kill_trigger) = if chain {
        (vec![register, t_complete], Some(t_kill))
    } else {
        (vec![register, t_kill, t_complete], None)
    };

    GeneratedQuest {
        qsd: qsd_for(spec, triggers),
        qsd_filename: format!("QX-{}.QSD", spec.quest_sn),
        kill_trigger: Some(kill_trigger),
        host_kill_trigger,
        quest_sn: spec.quest_sn,
        register_trigger,
        complete_trigger,
    }
}

/// Fetch: register / complete. No monster, no token. Completion checks the
/// player holds `>= N` of an existing item and (optionally) removes them — the
/// item is normal-inventory, so Finish/Init does NOT clear it.
fn generate_fetch(spec: &QuestSpec, item_sn: i32, consume: bool) -> GeneratedQuest {
    let register_trigger = trigger_name(spec.quest_sn, 1);
    let complete_trigger = trigger_name(spec.quest_sn, 2);

    let extra = if consume {
        vec![rewd_remove_item(item_sn, spec.count as i16)]
    } else {
        vec![]
    };
    let t_complete = Trigger {
        check_next: 0,
        name: name_bytes(&complete_trigger),
        conditions: vec![
            cond_quest_registered(spec.quest_sn),
            cond_item_count(item_sn, spec.count, CmpOp::Ge),
        ],
        rewards: complete_reward_list(spec, extra),
    };

    GeneratedQuest {
        qsd: qsd_for(
            spec,
            vec![register_trigger_entity(spec, &register_trigger), t_complete],
        ),
        qsd_filename: format!("QX-{}.QSD", spec.quest_sn),
        kill_trigger: None,
        host_kill_trigger: None,
        quest_sn: spec.quest_sn,
        register_trigger,
        complete_trigger,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Reproduce the real retail quest 105 kill trigger and check our payloads
    /// match byte-for-byte (with retail's uninitialized padding masked to 0).
    /// Retail bytes from `QN-021.QSD` "105-31":
    ///   COND_000  69 00 00 00
    ///   COND_004  01 00 00 00  49 33 00 00  0d 00 00 00  0a 00 00 00  03 cc cc cc
    ///   REWD_000  69 00 00 00  04 cd cd cd
    ///   REWD_001  49 33 00 00  01 cd  01 00  00 cd cd cd
    #[test]
    fn matches_retail_quest_105_kill_trigger() {
        // COND_000 iQuestSN = 105 (0x69)
        assert_eq!(cond_quest_registered(105).payload, vec![0x69, 0, 0, 0]);

        // COND_004 item 13129 (0x3349), count 10, op < . Retail iWhere=13; we use
        // 0 (both out of the equip range → identical behavior), so compare with
        // iWhere and padding normalized.
        let c4 = cond_item_count(13129, 10, CmpOp::Lt);
        assert_eq!(c4.etype, 4);
        assert_eq!(
            c4.payload,
            vec![
                0x01, 0, 0, 0, // iDataCnt
                0x49, 0x33, 0, 0, // uiItemSN = 13129
                0x00, 0, 0, 0, // iWhere (0; retail used 13 — behaviorally identical)
                0x0a, 0, 0, 0, // iRequestCnt = 10
                0x03, // btOp = 3 (<)
                0, 0, 0, // pad
            ]
        );

        // REWD_000 select quest 105 (op 4)
        let r0 = rewd_quest_op(105, QuestOp::Select);
        assert_eq!(r0.payload, vec![0x69, 0, 0, 0, 0x04, 0, 0, 0]);

        // REWD_001 give 1 of item 13129 (op 1)
        let r1 = rewd_give_item(13129, 1);
        assert_eq!(
            r1.payload,
            vec![0x49, 0x33, 0, 0, 0x01, 0, 0x01, 0x00, 0x00, 0, 0, 0]
        );
    }

    fn hunt_spec(quest_sn: i32, monster_id: i32, token_sn: i32, count: i32) -> QuestSpec {
        QuestSpec {
            quest_sn,
            kind: QuestKind::Hunt {
                monster_id,
                token_item_sn: token_sn,
                token_name: "Mark".into(),
                token_desc: String::new(),
                token_icon: None,
                chain_into_existing: false,
            },
            count,
            reward_exp: 500,
            reward_zuly: 1000,
            reward_item: Some((10_001, 1)),
            title: "Jelly Hunt".into(),
            start_text: "Kill some jellies.".into(),
            progress_text: "Keep hunting!".into(),
            complete_text: "Well done.".into(),
        }
    }

    #[test]
    fn generated_hunt_round_trips_through_codec() {
        let gen = generate(&hunt_spec(5501, 4, 13_500, 10));

        // The QSD must survive a codec round-trip byte-exact.
        let bytes = gen.qsd.to_bytes();
        let reparsed = QsdFile::parse(&bytes).expect("parse generated qsd");
        assert_eq!(gen.qsd, reparsed);
        assert_eq!(bytes, reparsed.to_bytes());

        // Structure sanity.
        let triggers = &gen.qsd.patterns[0].triggers;
        assert_eq!(triggers.len(), 3);
        assert_eq!(triggers[0].conditions.len(), 0); // register
        assert_eq!(triggers[0].rewards.len(), 1);
        assert_eq!(triggers[1].conditions.len(), 2); // kill
        assert_eq!(triggers[1].rewards.len(), 2);
        // complete: select + exp + zuly + item + finish = 5 rewds
        assert_eq!(triggers[2].conditions.len(), 2);
        assert_eq!(triggers[2].rewards.len(), 5);

        assert_eq!(gen.kill_trigger.as_deref(), Some("5501-2"));
    }

    #[test]
    fn hunt_rewards_are_omitted_when_zero() {
        let mut spec = hunt_spec(6000, 8, 13_501, 5);
        spec.reward_exp = 0;
        spec.reward_zuly = 0;
        spec.reward_item = None;
        let gen = generate(&spec);
        // complete: just select + finish = 2 rewds
        assert_eq!(gen.qsd.patterns[0].triggers[2].rewards.len(), 2);
    }

    #[test]
    fn chaining_hunt_splits_out_the_kill_trigger() {
        let mut spec = hunt_spec(5503, 1, 13_968, 3);
        if let QuestKind::Hunt {
            chain_into_existing, ..
        } = &mut spec.kind
        {
            *chain_into_existing = true;
        }
        let gen = generate(&spec);

        // This quest's own QSD holds only register + complete (no kill trigger).
        let triggers = &gen.qsd.patterns[0].triggers;
        assert_eq!(triggers.len(), 2);
        assert_eq!(triggers[0].conditions.len(), 0); // register
        assert_eq!(triggers[1].conditions.len(), 2); // complete

        // The kill trigger comes out separately for host insertion.
        let kill = gen.host_kill_trigger.expect("host kill trigger");
        assert_eq!(kill.name, name_bytes("5503-2"));
        assert_eq!(kill.conditions.len(), 2);
        assert_eq!(kill.rewards.len(), 2);
        assert_eq!(gen.kill_trigger.as_deref(), Some("5503-2"));

        // Splicing it into a host pattern must still round-trip through the codec.
        let mut host = qsd_for(&spec, vec![triggers[0].clone()]);
        host.patterns[0].triggers.push(kill);
        let bytes = host.to_bytes();
        assert_eq!(QsdFile::parse(&bytes).unwrap().to_bytes(), bytes);
    }

    #[test]
    fn fetch_quest_round_trips_and_consumes() {
        let spec = QuestSpec {
            quest_sn: 7000,
            kind: QuestKind::Fetch {
                item_sn: 10_060, // a use-item
                item_name: "Box".into(),
                consume: true,
            },
            count: 3,
            reward_exp: 200,
            reward_zuly: 0,
            reward_item: None,
            title: "Bring boxes".into(),
            start_text: String::new(),
            progress_text: String::new(),
            complete_text: String::new(),
        };
        let gen = generate(&spec);
        assert!(gen.kill_trigger.is_none());

        let bytes = gen.qsd.to_bytes();
        assert_eq!(QsdFile::parse(&bytes).unwrap().to_bytes(), bytes);

        let triggers = &gen.qsd.patterns[0].triggers;
        assert_eq!(triggers.len(), 2); // register + complete (no kill)
        // complete: COND_000 + COND_004; rewards = select + exp + remove + finish
        assert_eq!(triggers[1].conditions.len(), 2);
        assert_eq!(triggers[1].rewards.len(), 4);
        // the remove reward (REWD_001 op 0) consumes the fetched item
        let remove = &triggers[1].rewards[2];
        assert_eq!(remove.etype, 1);
        assert_eq!(remove.payload[4], 0); // btOp = 0 (remove)
    }
}
