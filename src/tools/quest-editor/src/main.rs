//! quest-editor CLI.
//!
//! Phase 1 is a developer/diagnostic front-end over the QSD codec. The egui
//! wizard UI arrives in a later phase (see PROGRESS.md).
//!
//! Usage:
//!   quest-editor verify <dir>   round-trip every .QSD under <dir>, report drift
//!   quest-editor stats  <dir>   parse every .QSD, print structure + type histogram
//!   quest-editor dump   <file>  pretty-print one .QSD's structure

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use anyhow::{bail, Context, Result};
use quest_editor::convo::{ConFile, LuaKind};
use quest_editor::data::DataSet;
use quest_editor::gen::{generate, GeneratedQuest, QuestKind, QuestSpec};
use quest_editor::qsd::QsdFile;
use quest_editor::verify::{self, Level};
use quest_editor::write::{apply_quest, delete_quest};

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let result = match args.first().map(String::as_str) {
        Some("verify") => cmd_verify(args.get(1)),
        Some("stats") => cmd_stats(args.get(1)),
        Some("dump") => cmd_dump(args.get(1)),
        Some("data") => cmd_data(args.get(1)),
        Some("stbcols") => cmd_stbcols(&args[1..]),
        Some("stlcheck") => cmd_stlcheck(&args[1..]),
        Some("gen") => cmd_gen(&args[1..]),
        Some("create") => cmd_create(&args[1..]),
        Some("create-fetch") => cmd_create_fetch(&args[1..]),
        Some("list") => cmd_list(args.get(1)),
        Some("delete") => cmd_delete(&args[1..]),
        Some("con-verify") => cmd_con_verify(args.get(1)),
        Some("con-dump") => cmd_con_dump(args.get(1)),
        Some("con-build") => cmd_con_build(&args[1..]),
        Some("ifo-find") => cmd_ifo_find(&args[1..]),
        Some("ifo-set") => cmd_ifo_set(&args[1..]),
        Some("ifo-scan") => cmd_ifo_scan(args.get(1)),
        Some("con-wire") => cmd_con_wire(&args[1..]),
        Some("npc-find") => cmd_npc_find(&args[1..]),
        Some("ltb-check") => cmd_ltb_check(&args[1..]),
        Some("icons-check") => cmd_icons_check(args.get(1)),
        Some("switch-check") => cmd_switch_check(args.get(1)),
        _ => {
            eprintln!("usage:");
            eprintln!("  quest-editor verify <dir>   round-trip every .QSD, report drift");
            eprintln!("  quest-editor stats  <dir>   parse every .QSD, print type histogram");
            eprintln!("  quest-editor dump   <file>  pretty-print one .QSD");
            eprintln!("  quest-editor data   <root>  load game tables, print a summary");
            eprintln!(
                "  quest-editor gen <root> <monster_id> <count> [exp] [zuly]\n\
                 \x20                            preview a generated Hunt quest (writes nothing)"
            );
            eprintln!(
                "  quest-editor create <root> <monster_id> <count> [exp] [zuly] [--write]\n\
                 \x20                            generate + apply a Hunt quest (dry-run unless --write)"
            );
            eprintln!(
                "  quest-editor create-fetch <root> <item_sn> <count> [exp] [zuly] [--write]\n\
                 \x20                            generate + apply a Fetch quest (item_sn = type*1000+id)"
            );
            eprintln!("  quest-editor list   <root>            list editor-created quests (from manifests)");
            eprintln!(
                "  quest-editor delete <root> <quest_sn> [--write]\n\
                 \x20                            remove an editor-created quest (dry-run unless --write)"
            );
            return ExitCode::FAILURE;
        }
    };

    match result {
        Ok(ok) => {
            if ok {
                ExitCode::SUCCESS
            } else {
                ExitCode::FAILURE
            }
        }
        Err(e) => {
            eprintln!("error: {e:#}");
            ExitCode::FAILURE
        }
    }
}

/// Recursively collect every `*.qsd` (case-insensitive) under `dir`.
fn collect_qsd(dir: &Path) -> Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        for entry in std::fs::read_dir(&d).with_context(|| format!("reading dir {}", d.display()))? {
            let path = entry?.path();
            if path.is_dir() {
                stack.push(path);
            } else if path
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("qsd"))
            {
                out.push(path);
            }
        }
    }
    out.sort();
    Ok(out)
}

fn cmd_verify(dir: Option<&String>) -> Result<bool> {
    let Some(dir) = dir else {
        bail!("verify needs a directory argument");
    };
    let files = collect_qsd(Path::new(dir))?;
    println!("round-tripping {} .QSD files under {dir}", files.len());

    let mut parse_fail = 0usize;
    let mut drift = 0usize;
    for path in &files {
        let raw = std::fs::read(path)?;
        match QsdFile::parse(&raw) {
            Ok(parsed) => {
                if parsed.to_bytes() != raw {
                    drift += 1;
                    println!("  DRIFT  {}", path.display());
                }
            }
            Err(e) => {
                parse_fail += 1;
                println!("  PARSE  {}: {e:#}", path.display());
            }
        }
    }

    let ok = parse_fail == 0 && drift == 0;
    println!(
        "\n{}  ({} parsed, {} parse failures, {} byte-drift)",
        if ok { "ALL OK" } else { "FAILURES" },
        files.len() - parse_fail,
        parse_fail,
        drift
    );
    Ok(ok)
}

fn cmd_stats(dir: Option<&String>) -> Result<bool> {
    let Some(dir) = dir else {
        bail!("stats needs a directory argument");
    };
    let files = collect_qsd(Path::new(dir))?;

    let mut total_patterns = 0usize;
    let mut total_triggers = 0usize;
    let mut cond_hist: BTreeMap<i32, usize> = BTreeMap::new();
    let mut rewd_hist: BTreeMap<i32, usize> = BTreeMap::new();
    let mut size_fields: BTreeMap<u32, usize> = BTreeMap::new();
    let mut parse_fail = 0usize;

    for path in &files {
        match QsdFile::read_file(path) {
            Ok(file) => {
                *size_fields.entry(file.size_field).or_default() += 1;
                total_patterns += file.patterns.len();
                for p in &file.patterns {
                    total_triggers += p.triggers.len();
                    for t in &p.triggers {
                        for c in &t.conditions {
                            *cond_hist.entry(c.type_id()).or_default() += 1;
                        }
                        for r in &t.rewards {
                            *rewd_hist.entry(r.type_id()).or_default() += 1;
                        }
                    }
                }
            }
            Err(e) => {
                parse_fail += 1;
                println!("  PARSE FAIL {}: {e:#}", path.display());
            }
        }
    }

    println!("files:        {}", files.len());
    println!("parse failed: {parse_fail}");
    println!("patterns:     {total_patterns}");
    println!("triggers:     {total_triggers}");
    println!("\nleading size_field values (value: count):");
    for (v, n) in &size_fields {
        println!("  {v}: {n}");
    }
    println!("\ncondition type histogram (type: count):");
    for (ty, n) in &cond_hist {
        println!("  COND {ty:>3}: {n}");
    }
    println!("\nreward type histogram (type: count):");
    for (ty, n) in &rewd_hist {
        println!("  REWD {ty:>3}: {n}");
    }

    Ok(parse_fail == 0)
}

fn cmd_dump(file: Option<&String>) -> Result<bool> {
    let Some(file) = file else {
        bail!("dump needs a file argument");
    };
    let qsd = QsdFile::read_file(Path::new(file))?;
    println!("size_field: {}", qsd.size_field);
    println!("description: {}", show_bytes(&qsd.description));
    println!("patterns: {}", qsd.patterns.len());
    for (pi, p) in qsd.patterns.iter().enumerate() {
        println!("  pattern[{pi}] name={} triggers={}", show_bytes(&p.name), p.triggers.len());
        for t in &p.triggers {
            println!(
                "    trigger '{}' check_next={} conds={} rewds={}",
                show_bytes(&t.name),
                t.check_next,
                t.conditions.len(),
                t.rewards.len()
            );
            for c in &t.conditions {
                println!(
                    "      COND {:>3}  [{}]",
                    c.type_id(),
                    hex_bytes(&c.payload)
                );
            }
            for r in &t.rewards {
                println!(
                    "      REWD {:>3}  [{}]",
                    r.type_id(),
                    hex_bytes(&r.payload)
                );
            }
        }
    }
    if !qsd.trailing.is_empty() {
        println!("trailing: {} bytes", qsd.trailing.len());
    }
    Ok(true)
}

fn cmd_data(root: Option<&String>) -> Result<bool> {
    let Some(root) = root else {
        bail!("data needs a root directory argument (the data/ or VFS root)");
    };
    let ds = DataSet::load(Path::new(root))?;

    println!("loaded from: {}", ds.stb_dir.display());
    println!("monsters:     {}", ds.monsters.len());
    println!("giver NPCs:   {}", ds.givers.len());
    println!(
        "quests:       {} named  ({} total rows)",
        ds.quests.len(),
        ds.quest_row_count
    );
    println!("next free SN: {}", ds.next_free_quest_sn());

    let item_total: usize = ds.item_db.all().count();
    println!("items:        {item_total}");

    let occupied = ds
        .monsters
        .iter()
        .filter(|m| !m.dead_event_is_free())
        .count();
    println!(
        "monsters with an occupied col-41 dead-event: {occupied} \
         (these are NOT eligible hunt targets under the MVP ownership rule)"
    );

    println!("\nsample monsters (id, lv, exp, dead-event):");
    for m in ds.monsters.iter().take(12) {
        println!(
            "  {:>5}  lv{:<3} exp{:<5} {:<22} dead_event={}",
            m.id,
            m.level,
            m.give_exp,
            truncate(&m.name, 22),
            if m.dead_event_is_free() {
                "<free>".to_string()
            } else {
                format!("\"{}\"", m.dead_event)
            }
        );
    }

    println!("\nsample existing quests (sn = row index, name):");
    for q in ds.quests.iter().take(8) {
        println!("  {:>5}  {}", q.sn, truncate(&q.name, 40));
    }

    println!("\nsample occupied dead-events (the shared-trigger risk):");
    for m in ds.monsters.iter().filter(|m| !m.dead_event_is_free()).take(10) {
        println!("  {:>5}  {:<22} -> \"{}\"", m.id, truncate(&m.name, 22), m.dead_event);
    }

    Ok(true)
}

/// Debug: print an STB's headers and a few rows (roselib column indices).
fn cmd_stbcols(args: &[String]) -> Result<bool> {
    use roselib::files::STB;
    use roselib::io::RoseFile;
    if args.is_empty() {
        bail!("usage: stbcols <file.stb> [num_rows]");
    }
    let n: usize = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(3);
    let start: usize = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(0);
    let stb = STB::from_path(Path::new(&args[0])).map_err(|e| anyhow::anyhow!("{e}"))?;
    println!("rows={} cols={}", stb.data.len(), stb.cols());
    if start == 0 {
        println!("headers (roselib col idx):");
        for (i, h) in stb.headers.iter().enumerate() {
            println!("  [{i}] {h:?}");
        }
    }
    for (r, row) in stb.data.iter().enumerate().skip(start).take(n) {
        println!("row {r}: {row:?}");
    }
    Ok(true)
}

/// Parsed `<root> <monster_id> <count> [exp] [zuly]` shared by gen/create.
struct HuntArgs {
    root: PathBuf,
    monster_id: i32,
    count: i32,
    exp: i32,
    zuly: i32,
}

fn parse_hunt_args(args: &[String]) -> Result<HuntArgs> {
    if args.len() < 3 {
        bail!("expected <root> <monster_id> <count> [exp] [zuly]");
    }
    Ok(HuntArgs {
        root: PathBuf::from(&args[0]),
        monster_id: args[1].parse().context("monster_id")?,
        count: args[2].parse().context("count")?,
        exp: args.get(3).and_then(|s| s.parse().ok()).unwrap_or(0),
        zuly: args.get(4).and_then(|s| s.parse().ok()).unwrap_or(0),
    })
}

/// Load data, enforce the ownership rule, and build the spec + generated quest.
/// Returns the monster name too (for display).
fn build_hunt(ha: &HuntArgs) -> Result<(DataSet, QuestSpec, GeneratedQuest, String)> {
    let ds = DataSet::load(&ha.root)?;
    let monster = ds
        .find_monster(ha.monster_id)
        .ok_or_else(|| anyhow::anyhow!("no monster with id {}", ha.monster_id))?;

    let name = monster.name.clone();
    // If the monster already has a dead-event trigger, chain onto it.
    let chain = !monster.dead_event_is_free();
    if chain {
        println!("(note: {name} already has a quest hook \"{}\" — chaining onto it)", monster.dead_event);
    }
    let spec = QuestSpec {
        quest_sn: ds.next_free_quest_sn(),
        kind: QuestKind::Hunt {
            monster_id: monster.id,
            token_item_sn: ds.next_free_token_item_sn(),
            token_name: format!("{name} Mark"),
            token_desc: format!("Proof of a defeated {name}."),
            token_icon: None,
            chain_into_existing: chain,
        },
        count: ha.count,
        reward_exp: ha.exp,
        reward_zuly: ha.zuly,
        reward_item: None,
        one_time_switch: None,
        extra_objectives: vec![],
        title: format!("Hunt: {name}"),
        start_text: format!("Defeat {} {name}.", ha.count),
        progress_text: format!("Keep hunting {name}."),
        complete_text: "Quest complete!".into(),
    };
    let gen = generate(&spec);
    Ok((ds, spec, gen, name))
}

/// Print verify issues; returns false if there were any errors.
fn report_issues(ds: &DataSet, spec: &QuestSpec, gen: &GeneratedQuest) -> bool {
    let issues = verify::verify(ds, spec, gen);
    for i in &issues {
        let tag = match i.level {
            Level::Error => "ERROR",
            Level::Warning => "warn",
        };
        println!("  [{tag}] {}", i.message);
    }
    !verify::has_errors(&issues)
}

fn cmd_create(args: &[String]) -> Result<bool> {
    let write = args.iter().any(|a| a == "--write");
    let once = args.iter().any(|a| a == "--once");
    let positional: Vec<String> = args.iter().filter(|a| !a.starts_with("--")).cloned().collect();
    let ha = parse_hunt_args(&positional)?;
    let (ds, mut spec, mut gen, name) = build_hunt(&ha)?;
    if once {
        spec.one_time_switch = Some(quest_editor::write::next_free_switch(&ha.root)?);
        gen = generate(&spec);
        println!("(one-time: character switch {})", spec.one_time_switch.unwrap());
    }

    println!("quest {} \"Hunt: {name}\" -> monster {}", spec.quest_sn, ha.monster_id);
    if !report_issues(&ds, &spec, &gen) {
        bail!("validation failed — nothing written");
    }
    let report = apply_quest(&ha.root, &spec, &gen, !write)?;
    report.print();
    if report.dry_run {
        println!("\n(re-run with --write to apply)");
    } else {
        println!(
            "\ntest in-game: register quest {} via GM cheat, kill {}x {name}, then /QUEST {}",
            spec.quest_sn, ha.count, gen.complete_trigger
        );
    }
    Ok(true)
}

fn cmd_list(root: Option<&String>) -> Result<bool> {
    let root = PathBuf::from(root.context("usage: list <root>")?);
    let quests = quest_editor::write::list_editor_quests(&root)?;
    if quests.is_empty() {
        println!("(no editor-created quests found)");
        return Ok(true);
    }
    println!("{} editor-created quest(s):", quests.len());
    for q in &quests {
        let detail = match &q.spec {
            Some(s) => match &s.kind {
                QuestKind::Hunt { monster_id, .. } => format!("Hunt monster {monster_id} x{}", s.count),
                QuestKind::Fetch { item_sn, .. } => format!("Fetch item {item_sn} x{}", s.count),
            },
            None => "(no manifest — delete only)".to_string(),
        };
        println!("  #{:<5} {:<28} {detail}", q.quest_sn, q.title);
    }
    Ok(true)
}

fn cmd_delete(args: &[String]) -> Result<bool> {
    let write = args.iter().any(|a| a == "--write");
    let pos: Vec<String> = args.iter().filter(|a| !a.starts_with("--")).cloned().collect();
    if pos.len() < 2 {
        bail!("usage: delete <root> <quest_sn> [--write]");
    }
    let root = PathBuf::from(&pos[0]);
    let quest_sn: i32 = pos[1].parse().context("quest_sn")?;
    let report = delete_quest(&root, quest_sn, !write)?;
    report.print();
    if report.dry_run {
        println!("\n(re-run with --write to apply)");
    }
    Ok(true)
}

fn collect_con(dir: &Path) -> Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        for entry in std::fs::read_dir(&d).with_context(|| format!("reading {}", d.display()))? {
            let p = entry?.path();
            if p.is_dir() {
                stack.push(p);
            } else if p
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("con"))
            {
                out.push(p);
            }
        }
    }
    out.sort();
    Ok(out)
}

fn cmd_con_verify(root: Option<&String>) -> Result<bool> {
    let root = PathBuf::from(root.context("usage: con-verify <dir>")?);
    let files = collect_con(&root)?;
    if files.is_empty() {
        bail!("no .CON files under {}", root.display());
    }
    let (mut ok, mut fail, mut drift) = (0u32, 0u32, 0u32);
    let (mut src, mut byte, mut empty) = (0u32, 0u32, 0u32);
    let (mut msgs, mut menus) = (0usize, 0usize);
    let mut failures = Vec::new();
    for f in &files {
        let bytes = match std::fs::read(f) {
            Ok(b) => b,
            Err(e) => {
                fail += 1;
                failures.push(format!("{}: {e}", f.display()));
                continue;
            }
        };
        match ConFile::parse(&bytes) {
            Ok(c) => {
                ok += 1;
                match c.lua_kind() {
                    LuaKind::Source => src += 1,
                    LuaKind::Bytecode => byte += 1,
                    LuaKind::Empty => empty += 1,
                }
                msgs += c.messages.len();
                menus += c.menus.len();
                if c.to_bytes() != bytes {
                    drift += 1;
                    if failures.len() < 15 {
                        let name = f.file_name().and_then(|s| s.to_str()).unwrap_or("?");
                        failures.push(format!("{name}: byte drift on round-trip"));
                    }
                }
            }
            Err(e) => {
                fail += 1;
                if failures.len() < 15 {
                    let name = f.file_name().and_then(|s| s.to_str()).unwrap_or("?");
                    failures.push(format!("{name}: {e:#}"));
                }
            }
        }
    }
    println!(
        "{} .CON files: {ok} parsed, {fail} failed, {drift} byte-drift on round-trip",
        files.len()
    );
    println!("  lua: {src} source, {byte} bytecode, {empty} empty");
    println!("  totals: {msgs} messages, {menus} menus");
    for fl in &failures {
        println!("  FAIL {fl}");
    }
    Ok(fail == 0 && drift == 0)
}

fn cmd_switch_check(root: Option<&String>) -> Result<bool> {
    let root = PathBuf::from(root.context("usage: switch-check <root>")?);
    let used = quest_editor::write::used_switches(&root)?;
    let in_cap = used.iter().filter(|&&s| s < 512).count();
    let next = quest_editor::write::next_free_switch(&root).ok();
    println!(
        "character quest-switches: {} used (0..511), {} free; next free = {}",
        in_cap,
        512 - in_cap,
        next.map_or("NONE".into(), |n| n.to_string())
    );
    Ok(true)
}

fn cmd_icons_check(root: Option<&String>) -> Result<bool> {
    let root = PathBuf::from(root.context("usage: icons-check <root>")?);
    let store = quest_editor::icons::IconStore::load(&root)?;
    println!("loaded ITEM1.TSI: {} icons", store.icon_count());
    Ok(store.icon_count() > 0)
}

fn cmd_ltb_check(args: &[String]) -> Result<bool> {
    use quest_editor::ltb::LtbTable;
    if args.is_empty() {
        bail!("usage: ltb-check <file.ltb> [row]");
    }
    let bytes = std::fs::read(&args[0]).with_context(|| format!("reading {}", args[0]))?;
    let t = LtbTable::parse(&bytes)?;
    println!("rows={} cols={} ({} bytes)", t.rows.len(), t.col_cnt, bytes.len());
    // Semantic round-trip.
    let rt = LtbTable::parse(&t.to_bytes())?;
    let ok = rt.rows == t.rows && rt.col_cnt == t.col_cnt;
    println!("semantic round-trip: {}", if ok { "OK" } else { "DRIFT" });
    if let Some(r) = args.get(1).and_then(|s| s.parse::<usize>().ok()) {
        if let Some(row) = t.rows.get(r) {
            for (c, s) in row.iter().enumerate() {
                println!("  row {r} col {c}: {:?}", quest_editor::ltb::decode_utf16le(s));
            }
        } else {
            println!("  row {r} out of range");
        }
    }
    Ok(!ok as i32 == 0)
}

fn cmd_npc_find(args: &[String]) -> Result<bool> {
    if args.len() < 2 {
        bail!("usage: npc-find <root> <name-substring>");
    }
    let root = PathBuf::from(&args[0]);
    let needle = args[1].to_lowercase();
    let ds = DataSet::load(&root)?;
    let placed = quest_editor::ifo::all_placed_npc_ids(&root).unwrap_or_default();
    let mut hits = 0;
    for g in &ds.givers {
        if g.name.to_lowercase().contains(&needle) {
            let p = if placed.contains(&g.id) { "placed" } else { "NOT placed" };
            println!("  npc {:<6} [town] {:<32} ({p})", g.id, g.name);
            hits += 1;
        }
    }
    for m in &ds.monsters {
        if m.name.to_lowercase().contains(&needle) {
            let p = if placed.contains(&m.id) { "placed" } else { "NOT placed" };
            println!("  npc {:<6} [mob]  {:<32} ({p})", m.id, m.name);
            hits += 1;
        }
    }
    if hits == 0 {
        println!("(no NPC name contains \"{needle}\")");
    }
    Ok(true)
}

fn cmd_con_wire(args: &[String]) -> Result<bool> {
    let write = args.iter().any(|a| a == "--write");
    let pos: Vec<String> = args.iter().filter(|a| !a.starts_with("--")).cloned().collect();
    if pos.len() < 4 {
        bail!("usage: con-wire <root> <npc_id> <quest_sn> <complete_trigger> [--write]\n\
               \x20  e.g. con-wire ../data 1001 5503 5503-3 --write");
    }
    let root = PathBuf::from(&pos[0]);
    let npc_id: i32 = pos[1].parse().context("npc_id")?;
    let qid: i32 = pos[2].parse().context("quest_sn")?;
    let trigger = &pos[3];

    let report = quest_editor::write::wire_quest_giver(&root, npc_id, qid, trigger, None, !write)?;
    report.print();
    if report.dry_run {
        println!("\n(re-run with --write to apply, then bake the VFS + restart)");
    } else {
        println!("\nnext: bake the VFS, restart servers + client, click npc {npc_id}.");
    }
    Ok(true)
}

fn cmd_ifo_scan(root: Option<&String>) -> Result<bool> {
    let root = PathBuf::from(root.context("usage: ifo-scan <root>")?);
    let list = quest_editor::ifo::placements_with_conversation(&root)?;
    println!("{} NPC placement(s) with a conversation:", list.len());
    for (npc_id, conv) in list.iter().take(60) {
        println!("  npc {npc_id:<5} -> {conv}");
    }
    if list.len() > 60 {
        println!("  ... and {} more", list.len() - 60);
    }
    Ok(true)
}

fn cmd_ifo_find(args: &[String]) -> Result<bool> {
    if args.len() < 2 {
        bail!("usage: ifo-find <root> <npc_id>");
    }
    let root = PathBuf::from(&args[0]);
    let npc_id: i32 = args[1].parse().context("npc_id")?;
    let found = quest_editor::ifo::find_npc(&root, npc_id)?;
    if found.is_empty() {
        println!("npc {npc_id}: no placements found under {}", root.display());
        return Ok(true);
    }
    println!("npc {npc_id}: {} placement(s):", found.len());
    for f in &found {
        let conv = if f.conversation.is_empty() {
            "<none>".to_string()
        } else {
            f.conversation.clone()
        };
        println!("  conversation = {conv:<16}  {}", f.ifo_path.display());
    }
    Ok(true)
}

fn cmd_ifo_set(args: &[String]) -> Result<bool> {
    let write = args.iter().any(|a| a == "--write");
    let pos: Vec<String> = args.iter().filter(|a| !a.starts_with("--")).cloned().collect();
    if pos.len() < 3 {
        bail!("usage: ifo-set <ifo_file> <npc_id> <conversation_name> [--write]");
    }
    let ifo = PathBuf::from(&pos[0]);
    let npc_id: i32 = pos[1].parse().context("npc_id")?;
    let name = &pos[2];
    let applied = quest_editor::ifo::wire_npc_in_ifo(&ifo, npc_id, name, !write)?;
    if !applied {
        bail!("npc {npc_id} not found in {}", ifo.display());
    }
    if write {
        println!("set npc {npc_id} conversation = {name} in {} (.bak made)", ifo.display());
    } else {
        println!("DRY RUN: npc {npc_id} -> {name} in {} (re-run with --write)", ifo.display());
    }
    Ok(true)
}

fn cmd_con_build(args: &[String]) -> Result<bool> {
    if args.len() < 3 {
        bail!("usage: con-build <out.CON> <quest_sn> <complete_trigger>  (e.g. con-build QG.CON 5503 5503-3)");
    }
    let out = PathBuf::from(&args[0]);
    let qid: i32 = args[1].parse().context("quest_sn")?;
    let trigger = &args[2];

    let bytes = quest_editor::convo::build_quest_giver(qid, trigger, Default::default());
    // Self-verify: re-parse with the same parser that reads all 125 retail files.
    let p = ConFile::parse(&bytes).context("generated .CON failed to re-parse")?;
    if p.to_bytes() != bytes {
        bail!("generated .CON did not round-trip");
    }
    std::fs::write(&out, &bytes).with_context(|| format!("writing {}", out.display()))?;
    println!(
        "wrote {} ({} bytes): {} message(s), {} menu(s), lua {} bytes ({:?})",
        out.display(),
        bytes.len(),
        p.messages.len(),
        p.menus.len(),
        p.lua.len(),
        p.lua_kind()
    );
    println!("accept -> QF_appendQuest({qid});  turn-in -> QF_doQuestTrigger(\"{trigger}\")");
    Ok(true)
}

fn cmd_con_dump(file: Option<&String>) -> Result<bool> {
    let path = PathBuf::from(file.context("usage: con-dump <file>")?);
    let c = ConFile::read_file(&path)?;
    println!("=== {} ===", path.display());
    println!(
        "event_mask=0x{:04x}  conv_off={}  script_off={}  size={}",
        c.event_mask, c.conv_off, c.script_off, c.file_size
    );
    for (slot, name) in &c.event_funcs {
        println!("  event[{slot}] -> {name}");
    }
    println!("messages ({}):", c.messages.len());
    for (i, m) in c.messages.iter().enumerate() {
        println!(
            "  msg[{i}] type={} value={} check='{}' click='{}' str={}",
            m.mtype, m.value, m.check_func, m.click_func, m.str_id
        );
    }
    println!("menus ({}):", c.menus.len());
    for (i, menu) in c.menus.iter().enumerate() {
        println!("  menu[{i}] ({} items)", menu.items.len());
        for (j, it) in menu.items.iter().enumerate() {
            println!(
                "    [{j}] type={} child={} check='{}' click='{}' str={}",
                it.mtype, it.child_menu, it.check_func, it.click_func, it.str_id
            );
        }
    }
    let kind = c.lua_kind();
    println!("lua: {:?}, {} bytes", kind, c.lua.len());
    if kind == LuaKind::Source {
        let n = c.lua.len().min(700);
        println!("--- lua preview ---\n{}", String::from_utf8_lossy(&c.lua[..n]));
    } else if kind == LuaKind::Bytecode {
        let n = c.lua.len().min(48);
        let hex: String = c.lua[..n].iter().map(|b| format!("{b:02x} ")).collect();
        println!("--- lua hex (first {n}) ---\n{hex}");
    }
    Ok(true)
}

fn cmd_create_fetch(args: &[String]) -> Result<bool> {
    let write = args.iter().any(|a| a == "--write");
    let pos: Vec<String> = args.iter().filter(|a| !a.starts_with("--")).cloned().collect();
    if pos.len() < 3 {
        bail!("usage: create-fetch <root> <item_sn> <count> [exp] [zuly] [--write]");
    }
    let root = PathBuf::from(&pos[0]);
    let item_sn: i32 = pos[1].parse().context("item_sn")?;
    let count: i32 = pos[2].parse().context("count")?;
    let exp: i32 = pos.get(3).and_then(|s| s.parse().ok()).unwrap_or(0);
    let zuly: i32 = pos.get(4).and_then(|s| s.parse().ok()).unwrap_or(0);

    let ds = DataSet::load(&root)?;
    // Resolve a display name for the item (type*1000+id).
    let (cat_id, id) = (item_sn / 1000, item_sn % 1000);
    let item_name = ds
        .item_db
        .all()
        .find(|it| it.category as i32 == cat_id && it.id == id)
        .map(|it| it.name.clone())
        .unwrap_or_else(|| format!("item {item_sn}"));

    let spec = QuestSpec {
        quest_sn: ds.next_free_quest_sn(),
        kind: QuestKind::Fetch {
            item_sn,
            item_name: item_name.clone(),
            consume: true,
        },
        count,
        reward_exp: exp,
        reward_zuly: zuly,
        reward_item: None,
        one_time_switch: None,
        extra_objectives: vec![],
        title: format!("Gather: {item_name}"),
        start_text: format!("Bring {count} {item_name}."),
        progress_text: format!("Collect {count} {item_name}."),
        complete_text: "Thank you, adventurer!".into(),
    };
    let gen = generate(&spec);

    println!("quest {} \"Gather: {item_name}\" -> bring {count}x item {item_sn}", spec.quest_sn);
    if !report_issues(&ds, &spec, &gen) {
        bail!("validation failed — nothing written");
    }
    let report = apply_quest(&root, &spec, &gen, !write)?;
    report.print();
    if report.dry_run {
        println!("\n(re-run with --write to apply)");
    } else {
        println!(
            "\ntest in-game: /QUEST {} to register, get {count}x {item_name}, then /QUEST {}",
            gen.register_trigger, gen.complete_trigger
        );
    }
    Ok(true)
}

/// Debug: re-parse an STL and look up a key (verifies a written STL).
fn cmd_stlcheck(args: &[String]) -> Result<bool> {
    use roselib::files::stl::StringTableRow;
    use roselib::files::STL;
    use roselib::io::RoseFile;
    if args.is_empty() {
        bail!("usage: stlcheck <file.stl> [key]");
    }
    let stl = STL::from_path(Path::new(&args[0])).map_err(|e| anyhow::anyhow!("{e}"))?;
    let rows = stl.language_tables.first().map(|l| l.rows.len()).unwrap_or(0);
    println!(
        "format={:?} keys={} languages={} rows/lang={}",
        stl.format,
        stl.keys.len(),
        stl.language_tables.len(),
        rows
    );
    println!("keys==rows aligned: {}", stl.keys.len() == rows);
    if let Some(key) = args.get(1) {
        match stl.keys.iter().position(|k| &k.name == key) {
            Some(idx) => {
                println!("key \"{key}\" found at index {idx} (id={})", stl.keys[idx].id);
                if let Some(lt) = stl.language_tables.first() {
                    if let Some(StringTableRow::QuestRow(q)) = lt.rows.get(idx) {
                        println!(
                            "  text={:?}\n  desc={:?}\n  start={:?}\n  end={:?}",
                            q.text, q.description, q.start_message, q.end_message
                        );
                    }
                }
            }
            None => println!("key \"{key}\" NOT FOUND"),
        }
    }
    Ok(true)
}

fn cmd_gen(args: &[String]) -> Result<bool> {
    let ha = parse_hunt_args(args)?;
    let (_ds, spec, gen, name) = build_hunt(&ha)?;
    let token_sn = match &spec.kind {
        QuestKind::Hunt { token_item_sn, .. } => *token_item_sn,
        _ => 0,
    };
    let kill_trigger = gen.kill_trigger.clone().unwrap_or_default();
    let monster_name = name;
    let count = ha.count;
    let exp = ha.exp;
    let zuly = ha.zuly;
    let monster_id = ha.monster_id;

    println!("=== generated Hunt quest (PREVIEW — nothing written) ===");
    println!("quest SN:        {}", gen.quest_sn);
    println!("target monster:  {monster_id} \"{monster_name}\"");
    println!("token item SN:   {token_sn} (new quest-item)");
    println!("kill count:      {count}");
    println!("rewards:         exp={exp} zuly={zuly}");
    println!("QSD file:        {}", gen.qsd_filename);
    println!("NPC col-41:      set npc {monster_id} -> \"{kill_trigger}\"");
    println!("triggers:        register=\"{}\" kill=\"{}\" complete=\"{}\"",
        gen.register_trigger, kill_trigger, gen.complete_trigger);
    println!(
        "\ntest in-game (no dialogs needed):\n  \
         /<cheat> QUEST {sn}        register the quest\n  \
         kill {count}x {mon}\n  \
         /QUEST {complete}          complete + collect rewards",
        sn = gen.quest_sn,
        mon = monster_name,
        complete = gen.complete_trigger,
    );

    let bytes = gen.qsd.to_bytes();
    println!("\nQSD is {} bytes; structure:", bytes.len());
    // Re-parse and pretty-print via the same path as `dump`.
    let parsed = QsdFile::parse(&bytes)?;
    for t in &parsed.patterns[0].triggers {
        println!(
            "  trigger \"{}\"  conds={} rewds={}",
            String::from_utf8_lossy(t.name.strip_suffix(b"\0").unwrap_or(&t.name)),
            t.conditions.len(),
            t.rewards.len()
        );
        for c in &t.conditions {
            println!("    COND {:>3}  [{}]", c.type_id(), hex_bytes(&c.payload));
        }
        for r in &t.rewards {
            println!("    REWD {:>3}  [{}]", r.type_id(), hex_bytes(&r.payload));
        }
    }

    Ok(true)
}

fn hex_bytes(b: &[u8]) -> String {
    b.iter()
        .map(|x| format!("{x:02x}"))
        .collect::<Vec<_>>()
        .join(" ")
}

fn truncate(s: &str, n: usize) -> String {
    if s.chars().count() <= n {
        s.to_string()
    } else {
        format!("{}…", s.chars().take(n - 1).collect::<String>())
    }
}

/// Render a byte string as text, escaping non-printable bytes. Quest names are
/// ASCII; description/pattern strings may contain legacy Korean (EUC-KR) bytes.
fn show_bytes(b: &[u8]) -> String {
    let trimmed = b.strip_suffix(b"\0").unwrap_or(b);
    let mut s = String::from("\"");
    for &c in trimmed {
        if c.is_ascii_graphic() || c == b' ' {
            s.push(c as char);
        } else {
            s.push_str(&format!("\\x{c:02x}"));
        }
    }
    s.push('"');
    s
}
