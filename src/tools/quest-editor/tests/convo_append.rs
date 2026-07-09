//! Acceptance tests for the `.CON` append-mode feature (QEX1 appendix chunk):
//!
//! 1. Every retail `.CON` must survive a *semantic* model rebuild — `parse ->
//!    rebuild -> parse` yields an identical model (the canonical re-layout is
//!    what append mode writes to disk).
//! 2. Appending a quest option and removing it again must restore the exact
//!    original model, on every retail file.
//! 3. The appendix block must decode exactly the way the client (`cevent.cpp`)
//!    reads it, including the file-size-dependent XOR keys.
//! 4. Two quests can coexist in one conversation; removing one keeps the other
//!    wired (child-menu remapping).

use std::path::PathBuf;

use quest_editor::convo::{
    append_quest_option, build_con, menu_item, quest_option_qids, remove_quest_option, ConFile,
    ConMenu, ConMsg, GiverStrings, LuaKind, SC_MSG_CLOSE, SC_MSG_NPCSAY, SC_MSG_PLAYERSELECT,
};

fn event_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../../data/3DDATA/EVENT")
        .canonicalize()
        .unwrap_or_else(|_| {
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../data/3DDATA/EVENT")
        })
}

fn collect_con(dir: &std::path::Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    if let Ok(rd) = std::fs::read_dir(dir) {
        for entry in rd.flatten() {
            let path = entry.path();
            if path
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("con"))
            {
                out.push(path);
            }
        }
    }
    out.sort();
    out
}

/// Everything the client reads from a `.CON`, flattened for comparison.
fn model(c: &ConFile) -> impl PartialEq + std::fmt::Debug {
    (
        c.event_mask,
        c.event_funcs.clone(),
        c.messages
            .iter()
            .map(|m| {
                (
                    m.sn,
                    m.mtype,
                    m.value,
                    m.check_func.clone(),
                    m.click_func.clone(),
                    m.str_id,
                )
            })
            .collect::<Vec<_>>(),
        c.menus
            .iter()
            .map(|m| {
                m.items
                    .iter()
                    .map(|i| {
                        (
                            i.mtype,
                            i.child_menu,
                            i.check_func.clone(),
                            i.click_func.clone(),
                            i.str_id,
                        )
                    })
                    .collect::<Vec<_>>()
            })
            .collect::<Vec<_>>(),
        c.lua.clone(),
        c.appendix.clone(),
    )
}

fn strings(base: i32) -> GiverStrings {
    GiverStrings {
        greeting: base,
        accept_option: base + 1,
        complete_option: base + 2,
        progress_option: base + 3,
        bye_option: base + 4,
        after_accept: base + 5,
        after_complete: base + 6,
        in_progress: base + 7,
        response_close: base + 8,
        hook_option: base + 9,
        decline_option: base + 10,
    }
}

/// A stand-in for a retail conversation: greeting -> options, "bytecode" blob.
fn synthetic_retail_con() -> Vec<u8> {
    let messages = vec![ConMsg {
        sn: 0,
        mtype: SC_MSG_PLAYERSELECT,
        value: 0,
        check_func: "checkEvent".into(),
        click_func: "endEvent".into(),
        str_id: 0,
    }];
    let menus = vec![
        ConMenu {
            items: vec![menu_item(SC_MSG_NPCSAY, 1, "", "", 10)],
        },
        ConMenu {
            items: vec![
                menu_item(SC_MSG_PLAYERSELECT, -1, "chkShop", "clkShop", 11),
                menu_item(SC_MSG_CLOSE, -1, "", "", 12),
            ],
        },
    ];
    // Not real bytecode — only the signature matters to LuaKind and the codec
    // treats the blob as opaque bytes either way. Even length on purpose (the
    // XOR key then depends on the file size, the trickier path).
    let lua = b"\x1bLua\x40fakebytecode..\0";
    build_con(&[(0, "checkEvent".to_string())], &messages, &menus, lua)
}

/// Replicate `CEvent::Load` + `Decode` byte-for-byte to prove the client will
/// read what we wrote: header offsets, main Lua XOR, appendix magic + XOR.
fn client_view(bytes: &[u8]) -> (Vec<u8>, Vec<u8>) {
    let rd32 = |o: usize| i32::from_le_bytes(bytes[o..o + 4].try_into().unwrap());
    let file_size = bytes.len() as i32;
    let key = |v1: i32, v2: i32| -> u8 {
        if v1 & 1 != 0 {
            v1 as u8
        } else {
            v2 as u8
        }
    };
    let script_off = rd32(520) as usize;
    let lua_len = rd32(script_off) as usize;
    let mut lua = bytes[script_off + 4..script_off + 4 + lua_len].to_vec();
    let k = key(lua_len as i32, file_size);
    lua.iter_mut().for_each(|b| *b ^= k);

    let mut appendix = Vec::new();
    let a_off = script_off + 4 + lua_len;
    if bytes.len() >= a_off + 8 && &bytes[a_off..a_off + 4] == b"QEX1" {
        let alen = rd32(a_off + 4) as usize;
        appendix = bytes[a_off + 8..a_off + 8 + alen].to_vec();
        let k = key(alen as i32, file_size);
        appendix.iter_mut().for_each(|b| *b ^= k);
    }
    (lua, appendix)
}

#[test]
fn all_retail_con_semantic_rebuild_and_append_remove() {
    let dir = event_dir();
    let files = collect_con(&dir);
    if files.is_empty() {
        eprintln!(
            "SKIP: no .CON files under {} — game data not present",
            dir.display()
        );
        return;
    }

    let mut failures = Vec::new();
    for path in &files {
        let orig = match ConFile::read_file(path) {
            Ok(c) => c,
            Err(e) => {
                failures.push(format!("PARSE {}: {e:#}", path.display()));
                continue;
            }
        };

        // 1) canonical rebuild is semantically identical
        let rebuilt = match ConFile::parse(&orig.rebuild()) {
            Ok(c) => c,
            Err(e) => {
                failures.push(format!("REPARSE {}: {e:#}", path.display()));
                continue;
            }
        };
        if model(&rebuilt) != model(&orig) {
            failures.push(format!("MODEL DRIFT {}", path.display()));
            continue;
        }

        // 2) append + remove restores the model exactly. Live data files may
        //    already carry the user's own appended quests — expect a superset.
        let mut work = rebuilt;
        if let Err(e) = append_quest_option(&mut work, 59503, "59503-3", &strings(100)) {
            failures.push(format!("APPEND {}: {e:#}", path.display()));
            continue;
        }
        let appended = ConFile::parse(&work.rebuild()).expect("appended file re-parses");
        let mut expect = quest_option_qids(&orig);
        expect.push(59503);
        expect.sort();
        assert_eq!(quest_option_qids(&appended), expect, "{}", path.display());
        // the client would decode the same lua + appendix we think we wrote
        let (lua, appendix) = client_view(&appended.raw);
        assert_eq!(lua, orig.lua, "client lua view drifted: {}", path.display());
        assert_eq!(
            appendix,
            appended.appendix,
            "client appendix view: {}",
            path.display()
        );
        assert!(!appendix.is_empty(), "{}", path.display());

        let mut stripped = appended;
        remove_quest_option(&mut stripped, 59503);
        let restored = ConFile::parse(&stripped.rebuild()).expect("stripped file re-parses");
        if model(&restored) != model(&orig) {
            failures.push(format!("APPEND/REMOVE DRIFT {}", path.display()));
        }
    }

    assert!(
        failures.is_empty(),
        "{}/{} CON files failed:\n{}",
        failures.len(),
        files.len(),
        failures.join("\n")
    );
    eprintln!(
        "OK: {} CON files rebuild + append/remove cleanly",
        files.len()
    );
}

#[test]
fn append_wires_menu_zero_and_appendix() {
    let bytes = synthetic_retail_con();
    let orig = ConFile::parse(&bytes).unwrap();
    assert_eq!(orig.lua_kind(), LuaKind::Bytecode);
    let orig_model = model(&orig);

    let mut con = ConFile::parse(&bytes).unwrap();
    append_quest_option(&mut con, 5503, "5503-3", &strings(100)).unwrap();
    let out = con.rebuild();
    let p = ConFile::parse(&out).unwrap();

    // retail nodes untouched, 3 option items appended to menu 0
    assert_eq!(p.menus.len(), 2 + 6);
    assert_eq!(p.menus[0].items.len(), 1 + 3);
    assert_eq!(p.menus[0].items[0].str_id, 10); // retail greeting intact
                                                // the hook line: gated by CHK_accept but with NO click action — clicking
                                                // it must open the start-message prompt, not accept the quest
    let hook = &p.menus[0].items[1];
    assert_eq!(hook.mtype, SC_MSG_PLAYERSELECT);
    assert_eq!(hook.check_func, "QE5503_CHK_accept");
    assert_eq!(hook.click_func, "");
    assert_eq!(hook.str_id, 109);
    // hook -> start message -> Accept / Decline choices
    let start = &p.menus[hook.child_menu as usize];
    assert_eq!(start.items[0].mtype, SC_MSG_NPCSAY);
    assert_eq!(start.items[0].str_id, 100);
    let choices = &p.menus[start.items[0].child_menu as usize];
    assert_eq!(choices.items.len(), 2);
    let accept = &choices.items[0];
    assert_eq!(accept.mtype, SC_MSG_PLAYERSELECT);
    assert_eq!(accept.check_func, "QE5503_CHK_accept");
    assert_eq!(accept.click_func, "QE5503_ACT_accept");
    assert_eq!(accept.str_id, 101);
    assert_eq!(choices.items[1].mtype, SC_MSG_CLOSE);
    assert_eq!(choices.items[1].str_id, 110);
    // accept -> after-accept response -> close button
    let resp = &p.menus[accept.child_menu as usize];
    assert_eq!(resp.items[0].mtype, SC_MSG_NPCSAY);
    assert_eq!(resp.items[0].str_id, 105);
    let close = &p.menus[resp.items[0].child_menu as usize];
    assert_eq!(close.items[0].mtype, SC_MSG_CLOSE);
    assert_eq!(close.items[0].child_menu, -1);
    assert_eq!(close.items[0].str_id, 108);

    // retail bytecode blob untouched; appendix holds the namespaced source
    assert_eq!(p.lua, orig.lua);
    let src = String::from_utf8(p.appendix.clone()).unwrap();
    assert!(src.contains("-- QE:BEGIN 5503"));
    assert!(src.contains("function QE5503_CHK_accept(E)"));
    assert!(src.contains("QF_doQuestTrigger(\"5503-3\")"));
    assert!(src.contains("-- QE:END 5503"));

    // exactly what the client will decode
    let (lua, appendix) = client_view(&out);
    assert_eq!(lua, orig.lua);
    assert_eq!(appendix, p.appendix);

    // re-append is idempotent
    let mut again = ConFile::parse(&out).unwrap();
    append_quest_option(&mut again, 5503, "5503-3", &strings(100)).unwrap();
    let p2 = ConFile::parse(&again.rebuild()).unwrap();
    assert_eq!(model(&p2), model(&p));

    // remove restores the original model bit-for-bit (and drops the appendix)
    let mut stripped = ConFile::parse(&out).unwrap();
    assert!(remove_quest_option(&mut stripped, 5503));
    let restored = ConFile::parse(&stripped.rebuild()).unwrap();
    assert_eq!(model(&restored), orig_model);
    assert!(restored.appendix.is_empty());
    // no appendix block on disk at all
    assert_eq!(restored.raw.len(), bytes.len());
}

#[test]
fn two_quests_coexist_and_remove_one() {
    let bytes = synthetic_retail_con();
    let mut con = ConFile::parse(&bytes).unwrap();
    append_quest_option(&mut con, 5503, "5503-3", &strings(100)).unwrap();
    append_quest_option(&mut con, 5504, "5504-2", &strings(200)).unwrap();
    let both = ConFile::parse(&con.rebuild()).unwrap();
    assert_eq!(quest_option_qids(&both), vec![5503, 5504]);

    let mut one = both;
    assert!(remove_quest_option(&mut one, 5503));
    let p = ConFile::parse(&one.rebuild()).unwrap();
    assert_eq!(quest_option_qids(&p), vec![5504]);

    // 5504's subtree survived the menu-index remapping
    let hook = p.menus[0]
        .items
        .iter()
        .find(|i| i.check_func == "QE5504_CHK_accept")
        .expect("5504 hook option still wired");
    assert_eq!(hook.str_id, 209);
    let start = &p.menus[hook.child_menu as usize];
    assert_eq!(start.items[0].str_id, 200); // start message of 5504
    let choices = &p.menus[start.items[0].child_menu as usize];
    assert_eq!(choices.items[0].click_func, "QE5504_ACT_accept");
    assert_eq!(choices.items[0].str_id, 201);
    assert_eq!(choices.items[1].str_id, 210); // decline of 5504
    let resp = &p.menus[choices.items[0].child_menu as usize];
    assert_eq!(resp.items[0].str_id, 205); // after-accept of 5504
    let close = &p.menus[resp.items[0].child_menu as usize];
    assert_eq!(close.items[0].mtype, SC_MSG_CLOSE);
    assert_eq!(close.items[0].str_id, 208);

    let src = String::from_utf8(p.appendix.clone()).unwrap();
    assert!(!src.contains("QE5503_"));
    assert!(src.contains("function QE5504_CHK_accept(E)"));

    // removing the last quest drops the appendix entirely
    let mut none = p;
    assert!(remove_quest_option(&mut none, 5504));
    let clean = ConFile::parse(&none.rebuild()).unwrap();
    assert!(quest_option_qids(&clean).is_empty());
    assert!(clean.appendix.is_empty());
    assert_eq!(model(&clean), model(&ConFile::parse(&bytes).unwrap()));
}
