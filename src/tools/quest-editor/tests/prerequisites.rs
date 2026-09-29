//! Prerequisite quests: a quest that requires two one-time quests must write a
//! "done" switch check per prerequisite into its register trigger, verify must
//! accept real prerequisites and refuse fake ones, reconstruct must read them back
//! apart from the quest's own guard, delete must name the dependents, and
//! deleting everything must leave every pre-existing `.QSD` byte-identical.
//! Runs only when the repo game data is present (skips loudly otherwise).

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use quest_editor::data::DataSet;
use quest_editor::gen::{generate, Prerequisite, QuestKind, QuestSpec};
use quest_editor::verify::{self, Level};
use quest_editor::write::{apply_quest, delete_quest, next_free_switch, reconstruct_spec};

fn repo_3ddata() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../data/3DDATA")
}

fn snapshot_qsd(dir: &Path) -> BTreeMap<String, Vec<u8>> {
    let mut out = BTreeMap::new();
    for e in std::fs::read_dir(dir).unwrap().flatten() {
        let p = e.path();
        if p.extension()
            .and_then(|x| x.to_str())
            .is_some_and(|x| x.eq_ignore_ascii_case("qsd"))
        {
            let name = p.file_name().unwrap().to_string_lossy().to_uppercase();
            out.insert(name, std::fs::read(&p).unwrap());
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

/// A one-time hunt on a free monster, numbered for the data as it is now.
fn hunt(ds: &DataSet, root: &Path, monster: i32, once: bool, requires: Vec<Prerequisite>) -> QuestSpec {
    QuestSpec {
        quest_sn: ds.next_free_quest_sn(),
        kind: QuestKind::Hunt {
            monster_id: monster,
            token_item_sn: ds.next_free_token_item_sn(),
            token_name: "Mark".into(),
            token_desc: String::new(),
            token_icon: None,
            chain_into_existing: false,
        },
        count: 3,
        reward_exp: 100,
        reward_zuly: 0,
        reward_item: None,
        one_time_switch: once.then(|| next_free_switch(root).unwrap()),
        requires,
        extra_objectives: vec![],
        title: "Prereq Test".into(),
        start_text: String::new(),
        progress_text: String::new(),
        complete_text: String::new(),
    }
}

fn errors(ds: &DataSet, spec: &QuestSpec) -> Vec<String> {
    verify::verify(ds, spec, &generate(spec))
        .into_iter()
        .filter(|i| i.level == Level::Error)
        .map(|i| i.message)
        .collect()
}

#[test]
fn prerequisites_apply_reconstruct_and_delete_cleanly() {
    let data = repo_3ddata();
    let (src_stb, src_qd) = (data.join("STB"), data.join("QUESTDATA"));
    if !src_stb.exists() || !src_qd.exists() {
        eprintln!("SKIP: game data not present under {}", data.display());
        return;
    }
    let root = std::env::temp_dir().join(format!("qe_prereq_{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    let dst = root.join("3DDATA");
    copy_dir(&src_stb, &dst.join("STB"));
    copy_dir(&src_qd, &dst.join("QUESTDATA"));
    let questdata = dst.join("QUESTDATA");
    let before = snapshot_qsd(&questdata);

    let ds = DataSet::load(&root).unwrap();
    let free: Vec<i32> = ds
        .monsters
        .iter()
        .filter(|m| m.dead_event_is_free())
        .map(|m| m.id)
        .take(4)
        .collect();
    assert!(free.len() == 4, "need four free monsters");

    // Two one-time prerequisites and one repeatable quest (not eligible).
    let mut made = Vec::new();
    for (i, once) in [(0, true), (1, true), (2, false)] {
        let ds = DataSet::load(&root).unwrap();
        let spec = hunt(&ds, &root, free[i], once, vec![]);
        apply_quest(&root, &spec, &generate(&spec), false).expect("apply prerequisite");
        made.push(spec);
    }
    let (a, b, repeatable) = (&made[0], &made[1], &made[2]);
    let (sw_a, sw_b) = (a.one_time_switch.unwrap(), b.one_time_switch.unwrap());

    let ds = DataSet::load(&root).unwrap();
    assert_eq!(ds.one_time_quest_for_switch(sw_a), Some(a.quest_sn));
    assert_eq!(ds.one_time_quest_for_switch(sw_b), Some(b.quest_sn));
    assert_eq!(ds.quest_switches.get(&repeatable.quest_sn).unwrap().own, None);

    // verify: a switch no quest sets is refused; a real prerequisite passes.
    let bogus = hunt(&ds, &root, free[3], true, vec![Prerequisite { quest_sn: repeatable.quest_sn, switch_no: 511 }]);
    assert!(
        errors(&ds, &bogus).iter().any(|m| m.contains("not a one-time quest")),
        "a prerequisite nobody completes must be an error"
    );

    let requires = vec![
        Prerequisite { quest_sn: a.quest_sn, switch_no: sw_a },
        Prerequisite { quest_sn: b.quest_sn, switch_no: sw_b },
    ];
    let finale = hunt(&ds, &root, free[3], true, requires.clone());
    assert!(errors(&ds, &finale).is_empty(), "{:?}", errors(&ds, &finale));
    apply_quest(&root, &finale, &generate(&finale), false).expect("apply finale");

    // Scanner and reconstruct tell the own guard and the prerequisites apart.
    let ds = DataSet::load(&root).unwrap();
    let w = ds.quest_switches.get(&finale.quest_sn).unwrap();
    assert_eq!(w.own, finale.one_time_switch);
    assert_eq!(w.requires, vec![sw_a, sw_b]);
    assert_eq!(ds.quests_requiring_switch(sw_a), vec![finale.quest_sn]);
    let rebuilt = reconstruct_spec(&root, finale.quest_sn).unwrap();
    assert_eq!(rebuilt.one_time_switch, finale.one_time_switch);
    assert_eq!(rebuilt.requires, requires);

    // Deleting a prerequisite names the quest that depends on it.
    let rep = delete_quest(&root, a.quest_sn, true).unwrap();
    assert!(
        rep.changes.iter().any(|c| c.contains(&format!("#{}", finale.quest_sn)) && c.contains("require")),
        "delete should warn about the dependent quest: {:#?}",
        rep.changes
    );

    // Delete everything; every pre-existing QSD is byte-identical again.
    for sn in [finale.quest_sn, repeatable.quest_sn, b.quest_sn, a.quest_sn] {
        delete_quest(&root, sn, false).expect("delete");
    }
    let after = snapshot_qsd(&questdata);
    assert_eq!(before, after, "QSDs not restored byte-exact after delete");

    let _ = std::fs::remove_dir_all(&root);
}
