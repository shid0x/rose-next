//! Quest packs: a dry run numbers every quest as a real run would, a real run
//! creates them (prerequisites by key included), a re-run keeps them, `--replace`
//! re-creates a prerequisite on its old completion switch so the quest that
//! requires it stays valid, and deleting everything leaves every pre-existing
//! `.QSD` byte-identical. Runs only when the repo game data is present.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use quest_editor::data::DataSet;
use quest_editor::pack::{apply_pack, parse_item, Pack};
use quest_editor::write::delete_quest;

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
            out.insert(
                p.file_name().unwrap().to_string_lossy().to_uppercase(),
                std::fs::read(&p).unwrap(),
            );
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

fn keyed(root: &Path) -> BTreeMap<String, (i32, Option<i32>)> {
    quest_editor::manifest::list_manifests(root)
        .into_iter()
        .filter_map(|m| m.key.map(|k| (k, (m.spec.quest_sn, m.spec.one_time_switch))))
        .collect()
}

#[test]
fn item_refs_parse_by_number_and_name() {
    assert_eq!(parse_item("3:880").unwrap(), 3880);
    assert_eq!(parse_item("body:880").unwrap(), 3880);
    assert_eq!(parse_item("useitem:15").unwrap(), 10015);
    assert!(parse_item("hat:1").is_err());
    assert!(parse_item("3-880").is_err());
}

#[test]
fn pack_creates_keeps_and_replaces() {
    let data = repo_3ddata();
    let (src_stb, src_qd) = (data.join("STB"), data.join("QUESTDATA"));
    if !src_stb.exists() || !src_qd.exists() {
        eprintln!("SKIP: game data not present under {}", data.display());
        return;
    }
    let root = std::env::temp_dir().join(format!("qe_pack_{}", std::process::id()));
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
        .take(3)
        .collect();
    let base_sn = ds.next_free_quest_sn();

    let json = format!(
        r#"{{ "quests": [
          {{ "key": "t-a", "once": true, "title": "A",
             "objectives": [ {{ "kind": "hunt", "monster": {a}, "count": 30, "token_name": "Tag A" }} ],
             "reward_exp": 100 }},
          {{ "key": "t-b", "once": true, "title": "B",
             "objectives": [ {{ "kind": "hunt", "monster": {b}, "count": 15, "token_name": "Part" }} ],
             "reward_exp": 100 }},
          {{ "key": "t-c", "once": true, "title": "C", "requires": ["t-a", "t-b"],
             "objectives": [ {{ "kind": "hunt", "monster": {c}, "count": 1 }} ],
             "reward_exp": 100 }} ] }}"#,
        a = free[0],
        b = free[1],
        c = free[2]
    );
    let pack: Pack = serde_json::from_str(&json).unwrap();

    // Dry run: three distinct, consecutive quest numbers; nothing written.
    let log = apply_pack(&root, &pack, false, true).expect("dry run");
    for (i, key) in ["t-a", "t-b", "t-c"].iter().enumerate() {
        let want = format!("CREATE \"{key}\" = quest #{}", base_sn + i as i32);
        assert!(log.iter().any(|l| l.starts_with(&want)), "missing {want}: {log:#?}");
    }
    assert_eq!(snapshot_qsd(&questdata), before, "a dry run must not write");

    // Real run.
    apply_pack(&root, &pack, false, false).expect("apply");
    let k = keyed(&root);
    assert_eq!(k.len(), 3);
    let ds = DataSet::load(&root).unwrap();
    let (sn_c, _) = k["t-c"];
    let (_, sw_a) = k["t-a"];
    let (sn_b, sw_b) = k["t-b"];
    assert_eq!(
        ds.quest_switches[&sn_c].requires,
        vec![sw_a.unwrap(), sw_b.unwrap()],
        "C gated on A and B"
    );

    // Re-run: everything kept, nothing new.
    let rows = ds.quest_row_count;
    let log = apply_pack(&root, &pack, false, false).expect("re-run");
    assert_eq!(log.iter().filter(|l| l.starts_with("KEEP")).count(), 3);
    assert_eq!(DataSet::load(&root).unwrap().quest_row_count, rows);

    // Replace: B moves to a new SN on its old switch; C's gate still resolves.
    apply_pack(&root, &pack, true, false).expect("replace");
    let k2 = keyed(&root);
    assert_eq!(k2.len(), 3);
    let (sn_b2, sw_b2) = k2["t-b"];
    assert_ne!(sn_b2, sn_b, "replace re-creates the quest");
    assert_eq!(sw_b2, sw_b, "replace keeps the completion switch");
    let ds = DataSet::load(&root).unwrap();
    assert_eq!(ds.one_time_quest_for_switch(sw_b.unwrap()), Some(sn_b2));

    for (_, (sn, _)) in keyed(&root) {
        delete_quest(&root, sn, false).expect("delete");
    }
    assert_eq!(snapshot_qsd(&questdata), before, "QSDs not restored byte-exact");
    let _ = std::fs::remove_dir_all(&root);
}
