//! Tier 3 acceptance test: a **multi-objective** quest (several hunts + a fetch,
//! including a chained objective) must apply cleanly and then `delete` back to a
//! state where every pre-existing `.QSD` is byte-for-byte identical again — i.e.
//! the multi-token / multi-monster wiring and the scan-based un-chaining fully
//! invert. Runs only when the repo game data is present (skips loudly otherwise).

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use quest_editor::data::{encode_item_no, DataSet, ItemCategory};
use quest_editor::gen::{generate, Objective, QuestKind, QuestSpec};
use quest_editor::write::{apply_quest, delete_quest, reconstruct_spec};

fn repo_3ddata() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../data/3DDATA")
}

/// Snapshot every `.QSD` under `dir` (by filename → bytes), skipping `.bak`.
fn snapshot_qsd(dir: &Path) -> BTreeMap<String, Vec<u8>> {
    let mut out = BTreeMap::new();
    if let Ok(rd) = std::fs::read_dir(dir) {
        for e in rd.flatten() {
            let p = e.path();
            let is_qsd = p
                .extension()
                .and_then(|x| x.to_str())
                .is_some_and(|x| x.eq_ignore_ascii_case("qsd"));
            if is_qsd {
                let name = p.file_name().unwrap().to_string_lossy().to_uppercase();
                out.insert(name, std::fs::read(&p).unwrap());
            }
        }
    }
    out
}

fn copy_dir(src: &Path, dst: &Path) {
    std::fs::create_dir_all(dst).unwrap();
    for e in std::fs::read_dir(src).unwrap().flatten() {
        let p = e.path();
        if p.is_file() {
            std::fs::copy(&p, dst.join(p.file_name().unwrap())).unwrap();
        }
    }
}

#[test]
fn multi_objective_apply_then_delete_restores_qsds_byte_exact() {
    let data = repo_3ddata();
    let src_stb = data.join("STB");
    let src_qd = data.join("QUESTDATA");
    if !src_stb.exists() || !src_qd.exists() {
        eprintln!("SKIP: game data not present under {}", data.display());
        return;
    }

    // Temp working copy: <tmp>/3DDATA/{STB,QUESTDATA}.
    let root = std::env::temp_dir().join(format!("qe_multi_{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    let dst = root.join("3DDATA");
    copy_dir(&src_stb, &dst.join("STB"));
    copy_dir(&src_qd, &dst.join("QUESTDATA"));
    let questdata = dst.join("QUESTDATA");

    let before = snapshot_qsd(&questdata);

    let ds = DataSet::load(&root).expect("load data");

    // A free monster to *claim*, plus an occupied monster to *chain* onto. Pick
    // the occupied one such that the quest actually applies (its dead-event must
    // resolve to a host trigger); try candidates until one works.
    let free: Vec<i32> = ds
        .monsters
        .iter()
        .filter(|m| m.dead_event_is_free())
        .map(|m| m.id)
        .collect();
    assert!(free.len() >= 1, "need at least one free monster");
    let claim_monster = free[0];

    // A fetch item that exists (id <= 999).
    let fetch_item = ds
        .item_db
        .by_category
        .get(&ItemCategory::UseItem)
        .and_then(|v| v.iter().find(|it| it.id <= 999))
        .map(|it| encode_item_no(ItemCategory::UseItem, it.id))
        .expect("a use-item to fetch");

    let occupied: Vec<i32> = ds
        .monsters
        .iter()
        .filter(|m| !m.dead_event_is_free())
        .map(|m| m.id)
        .collect();
    assert!(!occupied.is_empty(), "need an occupied monster to chain onto");

    let sn = ds.next_free_quest_sn();
    let mut applied = false;
    for &chain_monster in &occupied {
        // Primary: chained hunt. Extras: claimed hunt + consumed fetch.
        let spec = QuestSpec {
            quest_sn: sn,
            kind: QuestKind::Hunt {
                monster_id: chain_monster,
                token_item_sn: ds.token_item_sn_at(0),
                token_name: "Chain Mark".into(),
                token_desc: "chained".into(),
                token_icon: None,
                chain_into_existing: true,
            },
            count: 4,
            reward_exp: 500,
            reward_zuly: 100,
            reward_item: None,
            one_time_switch: None,
            extra_objectives: vec![
                Objective::Hunt {
                    monster_id: claim_monster,
                    count: 6,
                    token_item_sn: ds.token_item_sn_at(1),
                    token_name: "Claim Mark".into(),
                    token_desc: "claimed".into(),
                    token_icon: None,
                    chain_into_existing: false,
                },
                Objective::Fetch {
                    item_sn: fetch_item,
                    item_name: "Box".into(),
                    count: 3,
                    consume: true,
                },
            ],
            title: "Multi Test".into(),
            start_text: String::new(),
            progress_text: String::new(),
            complete_text: String::new(),
        };
        let gen = generate(&spec);
        if apply_quest(&root, &spec, &gen, false).is_ok() {
            applied = true;
            break;
        }
    }
    assert!(applied, "no occupied monster could be chained onto");

    // --- post-apply structural checks ---
    let qsd_path = questdata.join(format!("QX-{sn}.QSD"));
    assert!(qsd_path.exists(), "new QSD written");

    // reconstruct (no manifest path) should see all three objectives back.
    let rebuilt = reconstruct_spec(&root, sn).expect("reconstruct");
    assert_eq!(
        rebuilt.extra_objectives.len(),
        2,
        "reconstruct found both extra objectives"
    );
    // Two tokens belong to the quest.
    let ds2 = DataSet::load(&root).unwrap();
    // The claimed monster now points at the extra hunt's kill trigger.
    let claim = ds2.monsters.iter().find(|m| m.id == claim_monster).unwrap();
    assert!(!claim.dead_event_is_free(), "claimed monster wired");

    // --- delete, then everything QSD-side must be byte-exact again ---
    delete_quest(&root, sn, false).expect("delete");
    assert!(!qsd_path.exists(), "QSD removed on delete");

    let after = snapshot_qsd(&questdata);
    // Same set of pre-existing QSDs, each byte-identical (the chained host fully
    // restored). The new QX file is gone from both (removed); nothing else added.
    assert_eq!(
        before.keys().collect::<Vec<_>>(),
        after.keys().collect::<Vec<_>>(),
        "QSD file set unchanged after delete"
    );
    for (name, bytes) in &before {
        assert_eq!(
            after.get(name).unwrap(),
            bytes,
            "QSD {name} not restored byte-exact after delete"
        );
    }

    // The claimed monster's col-41 is cleared again.
    let ds3 = DataSet::load(&root).unwrap();
    let claim3 = ds3.monsters.iter().find(|m| m.id == claim_monster).unwrap();
    assert!(claim3.dead_event_is_free(), "claimed monster col-41 cleared");

    let _ = std::fs::remove_dir_all(&root);
}
