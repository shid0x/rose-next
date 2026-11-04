#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::sync::{mpsc, Arc, Mutex};

use egui::{Color32, RichText};
use egui_extras::{Column, TableBuilder};
use npc_shop_editor::assets::{self, Assets};
use npc_shop_editor::catalog::{Catalog, CatalogItem, Filter, RangeFilter};
use npc_shop_editor::data::ItemCategory;
use npc_shop_editor::icons::IconStore;
use npc_shop_editor::preview::{
    self, Action, Animation, AvatarModels, Camera, Framing, Outfit, Posed, Scene, Sex, Slot,
    MIN_ZOOM,
};
use npc_shop_editor::preview_gl::GlScene;
use npc_shop_editor::preview_monster::{self, motion_label, parse_motion, MonsterModels};
use npc_shop_editor::monsters::{
    Monster, MonsterCatalog, MonsterFilter, Severity, StatusFilter, MAX_SPAWN_COUNT, PACKAGE_TABLES,
};
use npc_shop_editor::text::Codec;

/// Broken monster rows keep their place in the list, drawn in rose.
const ROSE: Color32 = Color32::from_rgb(255, 96, 150);
const AMBER: Color32 = Color32::from_rgb(235, 185, 80);

fn main() -> eframe::Result<()> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn")).init();
    let mut args: Vec<_> = std::env::args_os().skip(1).collect();
    // --encoding auto|western|korean|japanese|chinese|utf-8: how table text is decoded.
    // Auto reads the script off each file, so this is only for overriding it.
    let mut codec = Codec::Auto;
    if let Some(i) = args.iter().position(|a| a == "--encoding") {
        let value = args.get(i + 1).and_then(|v| v.to_str()).and_then(Codec::parse);
        match value {
            Some(c) => codec = c,
            None => {
                eprintln!("usage: --encoding auto|western|korean|japanese|chinese|utf-8");
                std::process::exit(2);
            }
        }
        args.drain(i..i + 2);
    }
    if args.first().is_some_and(|a| a == "--check") {
        // Headless: load a folder the way the window does and print what the
        // "Data warnings" header would show. Exit 1 if either catalog failed.
        // `--previews` also builds every spawnable item's and every monster's 3D preview
        // (no drawing) and groups what could not be shown.
        let Some(root) = args.get(1).map(PathBuf::from) else {
            eprintln!(
                "usage: gm-item-browser --check <data folder or data.idx> [--previews] [--encoding ...]"
            );
            std::process::exit(2);
        };
        let previews = args[2..].iter().any(|a| a == "--previews");
        std::process::exit(check(&root, codec, previews));
    }
    if args.first().is_some_and(|a| a == "--preview") {
        // Headless: render an outfit's 3D preview to a PNG, the way the pane
        // draws it, and list what could not be shown. Exit 1 on any note.
        let Some(preview) = PreviewArgs::parse(&args[1..], false) else {
            eprintln!(
                "usage: gm-item-browser --preview <data> <out.png> <type:id>[,<type:id>...] \
                 [--female] [--motion idle|battle|walk|run|attack|sitting|bind] \
                 [--time seconds] [--yaw degrees] [--zoom z]"
            );
            std::process::exit(2);
        };
        std::process::exit(preview.run(codec));
    }
    if args.first().is_some_and(|a| a == "--preview-monster") {
        // Headless: the same for a LIST_NPC row.
        let Some(preview) = PreviewArgs::parse(&args[1..], true) else {
            eprintln!(
                "usage: gm-item-browser --preview-monster <data> <out.png> <id> \
                 [--motion idle|walk|attack|hit|death|run|cast1|skill1|cast2|skill2|extra|bind] \
                 [--time seconds] [--yaw degrees] [--zoom z]"
            );
            std::process::exit(2);
        };
        std::process::exit(preview.run(codec));
    }
    if args.first().is_some_and(|a| a == "--package-monsters") {
        // Used by scripts/package-gm-item-browser.ps1. The release build has no
        // console, so the packager redirects stderr and checks the exit code.
        let code = match (args.get(1), args.get(2)) {
            (Some(data), Some(out)) => match package_monsters(Path::new(data), Path::new(out)) {
                Ok(()) => 0,
                Err(e) => {
                    eprintln!("{e:#}");
                    1
                }
            },
            _ => {
                eprintln!("usage: gm-item-browser --package-monsters <data> <package-data>");
                2
            }
        };
        std::process::exit(code);
    }
    let root = args.first().map(PathBuf::from).or_else(|| {
        let beside_exe = std::env::current_exe().ok()?.parent()?.to_path_buf();
        [Some(beside_exe), std::env::current_dir().ok()]
            .into_iter()
            .flatten()
            .flat_map(|root| [root.join("data"), root])
            .find(|path| {
                path.join("data.idx").is_file() || path.join("3DDATA/STB/LIST_WEAPON.STB").is_file()
            })
    });
    eframe::run_native(
        "ROSE GM Browser",
        eframe::NativeOptions {
            viewport: egui::ViewportBuilder::default()
                .with_inner_size([1320.0, 820.0])
                .with_min_inner_size([1100.0, 650.0]),
            // The 3D item preview depth-tests; without a depth buffer back
            // faces draw through.
            depth_buffer: 24,
            ..Default::default()
        },
        Box::new(move |cc| Box::new(Browser::new(cc, root, codec))),
    )
}

/// `--check`: what the window would load from `root`, on the console.
fn check(root: &Path, codec: Codec, previews: bool) -> i32 {
    let assets = match Assets::open(root) {
        Ok(assets) => assets,
        Err(e) => {
            eprintln!("cannot open {}: {e:#}", root.display());
            return 1;
        }
    };
    let mut failed = false;
    let mut warnings = Vec::new();
    match Catalog::load_with(&assets, codec) {
        Ok(catalog) => {
            let named = catalog
                .items
                .iter()
                .filter(|i| !i.item.name.starts_with("Unnamed "))
                .count();
            println!("items: {} ({named} named)", catalog.items.len());
            for item in catalog.items.iter().filter(|i| i.item.icon_no > 0).take(5) {
                println!("  e.g. {}:{} {}", item.item.category as u8, item.item.id, item.item.name);
            }
            if previews {
                let models = AvatarModels::load(&assets, codec);
                let mut notes = NoteTally::default();
                let mut built = 0;
                for item in &catalog.items {
                    let category = item.item.category;
                    if Slot::for_category(category).is_none() || !item.spawnable() {
                        continue;
                    }
                    for sex in [Sex::Male, Sex::Female] {
                        let worn = [(category, item.item.id)];
                        let outfit = models.outfit(sex, &worn, Some(category));
                        let scene = models.build(&assets, &outfit);
                        built += 1;
                        let who = format!("{}:{} {:?}", category as u8, item.item.id, sex);
                        notes.add(&who, &scene.notes);
                    }
                }
                notes.print("item previews", built);
            }
            warnings.extend(catalog.warnings);
        }
        Err(e) => {
            failed = true;
            println!("items: FAILED: {e:#}");
        }
    }
    match MonsterCatalog::load_with(&assets, codec) {
        Ok(catalog) => {
            let named = catalog.monsters.iter().filter(|m| m.has_game_name).count();
            println!(
                "monsters: {} ({named} with a game name; file checks {})",
                catalog.monsters.len(),
                if catalog.file_checks { "on" } else { "off" }
            );
            for m in catalog.monsters.iter().filter(|m| m.has_game_name).take(5) {
                println!("  e.g. {} {}", m.id, m.name);
            }
            if previews {
                let models = MonsterModels::load(&assets);
                let mut notes = NoteTally::default();
                for monster in &catalog.monsters {
                    let scene = models.build(&assets, monster);
                    notes.add(&format!("monster {}", monster.id), &scene.notes);
                }
                notes.print("monster previews", catalog.monsters.len());
            }
            warnings.extend(catalog.warnings);
        }
        Err(e) => {
            failed = true;
            println!("monsters: FAILED: {e:#}");
        }
    }
    println!("data warnings: {}", warnings.len());
    for w in &warnings {
        println!("  {w}");
    }
    // Load the fallback fonts into a headless context and run one frame, which
    // is when egui parses them -- so a font the window would choke on fails here.
    let ctx = egui::Context::default();
    let fonts = install_fallback_fonts(&ctx);
    let _ = ctx.run(egui::RawInput::default(), |ctx| {
        egui::Area::new("probe".into()).show(ctx, |ui| {
            ui.label("鏡の盾 나무 방패");
        });
    });
    println!(
        "fallback fonts: {}",
        if fonts.is_empty() { "none (CJK names draw as boxes)".to_string() } else { fonts.join(", ") }
    );
    i32::from(failed)
}

/// `--check --previews`: preview notes grouped by their text with the numbers
/// taken out, so one cause shared by a thousand items is one line.
#[derive(Default)]
struct NoteTally {
    failed: usize,
    groups: std::collections::BTreeMap<String, (usize, Vec<String>)>,
}

impl NoteTally {
    fn add(&mut self, who: &str, notes: &[String]) {
        if !notes.is_empty() {
            self.failed += 1;
        }
        for note in notes {
            let key: String = note
                .chars()
                .map(|c| if c.is_ascii_digit() { '#' } else { c })
                .collect();
            let group = self.groups.entry(key).or_default();
            group.0 += 1;
            if group.1.len() < 3 {
                group.1.push(format!("{who}: {note}"));
            }
        }
    }

    fn print(&self, what: &str, built: usize) {
        println!("{what}: {built} built, {} with notes", self.failed);
        let mut groups: Vec<_> = self.groups.iter().collect();
        groups.sort_by(|a, b| b.1 .0.cmp(&a.1 .0));
        for (key, (count, examples)) in groups {
            println!("  {count:>6}  {key}");
            for example in examples {
                println!("            e.g. {example}");
            }
        }
    }
}

/// `--preview` / `--preview-monster`: what the 3D pane would show, as a PNG.
struct PreviewArgs {
    root: PathBuf,
    out: PathBuf,
    subject: Subject,
    sex: Sex,
    /// Parsed by the subject: an avatar action or a monster motion slot.
    motion: Option<String>,
    time: f32,
    camera: Camera,
}

enum Subject {
    /// The last one is the item looked at.
    Outfit(Vec<(ItemCategory, i32)>),
    Monster(i32),
}

impl PreviewArgs {
    fn parse(args: &[OsString], monster: bool) -> Option<Self> {
        let args: Vec<String> = args.iter().map(|a| a.to_string_lossy().into_owned()).collect();
        let mut positional = Vec::new();
        let (mut sex, mut motion, mut time) = (Sex::Male, None, 0.0);
        let mut camera = Camera::default();
        let mut values = args.iter();
        while let Some(arg) = values.next() {
            let mut number = || values.next()?.parse::<f32>().ok();
            match arg.as_str() {
                "--female" => sex = Sex::Female,
                "--motion" => motion = Some(values.next()?.clone()),
                "--time" => time = number()?,
                "--yaw" => camera.yaw = number()?.to_radians(),
                "--zoom" => camera.zoom = number()?,
                _ => positional.push(arg.clone()),
            }
        }
        let [root, out, subject] = positional.as_slice() else {
            return None;
        };
        let subject = if monster {
            Subject::Monster(subject.parse().ok()?)
        } else {
            let worn = subject
                .split(',')
                .map(|item| {
                    let (category, id) = item.split_once(':')?;
                    let category: u8 = category.parse().ok()?;
                    let category =
                        ItemCategory::ALL.iter().copied().find(|c| *c as u8 == category)?;
                    Some((category, id.parse().ok()?))
                })
                .collect::<Option<Vec<_>>>()?;
            Subject::Outfit(worn)
        };
        Some(Self {
            root: root.into(),
            out: out.into(),
            subject,
            sex,
            motion,
            time,
            camera,
        })
    }

    fn run(&self, codec: Codec) -> i32 {
        let assets = match Assets::open(&self.root) {
            Ok(assets) => assets,
            Err(e) => {
                eprintln!("cannot open {}: {e:#}", self.root.display());
                return 1;
            }
        };
        let started = std::time::Instant::now();
        let built = match &self.subject {
            Subject::Outfit(worn) => self.outfit(&assets, codec, worn),
            Subject::Monster(id) => self.monster(&assets, codec, *id),
        };
        let Some((scene, animation, notes)) = built else {
            return 2;
        };
        let built = started.elapsed();
        let started = std::time::Instant::now();
        let posed = scene.pose(animation.as_ref(), self.time);
        let posing = started.elapsed();
        let framing = scene.framing(animation.as_ref());
        println!(
            "{} meshes, {} triangles, {} textures; tables + model {built:?}, one pose {posing:?}",
            scene.meshes.len(),
            scene.triangles(),
            scene.textures.len(),
        );
        match &animation {
            Some(animation) => println!(
                "motion {} ({:.2} s), at {:.2} s",
                animation.path,
                animation.duration(),
                self.time
            ),
            None => println!("bind pose"),
        }
        println!("bounds {:?} .. {:?}", framing.min, framing.max);
        let (width, height) = (360, 480);
        let rgba = preview::render(&scene, &posed, &framing, &self.camera, width, height);
        if let Err(e) = image::save_buffer(
            &self.out,
            &rgba,
            width as u32,
            height as u32,
            image::ColorType::Rgba8,
        ) {
            eprintln!("cannot write {}: {e}", self.out.display());
            return 1;
        }
        for note in &notes {
            println!("note: {note}");
        }
        i32::from(!notes.is_empty())
    }

    fn outfit(
        &self,
        assets: &Assets,
        codec: Codec,
        worn: &[(ItemCategory, i32)],
    ) -> Option<(Scene, Option<Animation>, Vec<String>)> {
        let action = match &self.motion {
            Some(text) => Action::parse(text).or_else(|| {
                eprintln!("unknown motion {text}");
                None
            })?,
            None => Action::Idle,
        };
        let models = AvatarModels::load(assets, codec);
        let focus = worn.last().map(|&(category, _)| category);
        let outfit = models.outfit(self.sex, worn, focus);
        println!("{outfit:?}");
        let scene = models.build(assets, &outfit);
        let mut notes: Vec<String> = models.warnings.iter().chain(&scene.notes).cloned().collect();
        let animation = models.motion(assets, &outfit, action).unwrap_or_else(|e| {
            notes.push(e);
            None
        });
        Some((scene, animation, notes))
    }

    fn monster(
        &self,
        assets: &Assets,
        codec: Codec,
        id: i32,
    ) -> Option<(Scene, Option<Animation>, Vec<String>)> {
        let catalog = MonsterCatalog::load_with(assets, codec)
            .map_err(|e| eprintln!("monster tables: {e:#}"))
            .ok()?;
        let Some(monster) = catalog.monsters.iter().find(|m| m.id == id) else {
            eprintln!("LIST_NPC has no monster {id}");
            return None;
        };
        let models = MonsterModels::load(assets);
        let slots = models.motion_slots(id);
        let slot = match self.motion.as_deref() {
            Some("bind") => None,
            Some(text) => Some(parse_motion(text).or_else(|| {
                eprintln!("unknown motion {text}");
                None
            })?),
            None => slots.first().copied(),
        };
        println!(
            "{} (#{id}), scale {} %, weapons {}/{}, motion slots: {}",
            monster.name,
            monster.scale,
            monster.right_weapon,
            monster.left_weapon,
            slots
                .iter()
                .map(|&s| motion_label(s))
                .collect::<Vec<_>>()
                .join(", ")
        );
        let scene = models.build(assets, monster);
        let mut notes = scene.notes.clone();
        let animation = models.motion(assets, id, slot).unwrap_or_else(|e| {
            notes.push(e);
            None
        });
        Some((scene, animation, notes))
    }
}

/// egui ships Latin-only fonts, so a correctly decoded Japanese or Korean name
/// still draws as boxes. Append whatever CJK fonts Windows has as fallbacks
/// (egui walks a family's list until a glyph is found); `ROSE_GM_FONT` names
/// one to try first. Returns what was loaded, for the data warnings header.
fn install_fallback_fonts(ctx: &egui::Context) -> Vec<String> {
    let mut fonts = egui::FontDefinitions::default();
    let mut candidates: Vec<PathBuf> = Vec::new();
    if let Some(custom) = std::env::var_os("ROSE_GM_FONT") {
        candidates.push(custom.into());
    }
    let windir = std::env::var_os("WINDIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(r"C:\Windows"));
    for name in [
        "YuGothM.ttc", // Japanese (Windows 10+)
        "meiryo.ttc",
        "msgothic.ttc",
        "malgun.ttf", // Korean
        "msyh.ttc",   // Simplified Chinese, also covers most hanja/kanji
        "simsun.ttc",
    ] {
        candidates.push(windir.join("Fonts").join(name));
    }
    let mut loaded = Vec::new();
    for path in candidates {
        let Ok(bytes) = std::fs::read(&path) else {
            continue;
        };
        // epaint unwraps the parse inside set_fonts; validate here instead so
        // a damaged font file degrades to "no fallback" rather than a panic.
        if ab_glyph::FontRef::try_from_slice_and_index(&bytes, 0).is_err() {
            continue;
        }
        let key = format!("fallback-{}", loaded.len());
        fonts
            .font_data
            .insert(key.clone(), egui::FontData::from_owned(bytes));
        for family in [egui::FontFamily::Proportional, egui::FontFamily::Monospace] {
            fonts.families.entry(family).or_default().push(key.clone());
        }
        loaded.push(path.file_name().unwrap_or_default().to_string_lossy().into_owned());
    }
    ctx.set_fonts(fonts);
    loaded
}

/// Copy the monster tables and write the manifest of model files they
/// reference, so a package without meshes or textures checks monsters exactly
/// as the full data does.
fn package_monsters(data: &Path, out: &Path) -> anyhow::Result<()> {
    let source = Assets::open(data)?;
    let catalog = MonsterCatalog::load(&source)?;
    if !catalog.file_checks || !catalog.warnings.is_empty() {
        anyhow::bail!(
            "refusing to package incomplete monster data:\n{}",
            catalog.warnings.join("\n")
        );
    }
    for table in PACKAGE_TABLES {
        let target = out.join(table);
        std::fs::create_dir_all(target.parent().unwrap())?;
        std::fs::write(&target, source.read(table)?)?;
    }
    std::fs::write(
        out.join(assets::MANIFEST_NAME),
        assets::write_manifest(catalog.present_files.iter().cloned()),
    )?;
    let packaged = MonsterCatalog::load(&Assets::open(out)?)?;
    let verdicts = |c: &MonsterCatalog| -> Vec<_> {
        c.monsters
            .iter()
            .map(|m| (m.id, m.issues.clone()))
            .collect()
    };
    if verdicts(&packaged) != verdicts(&catalog) {
        anyhow::bail!("the packaged monster data does not reproduce the source's checks");
    }
    Ok(())
}

struct LoadedData {
    items: Result<Catalog, String>,
    monsters: Result<MonsterCatalog, String>,
    assets: Arc<Assets>,
}

type Loaded = Result<LoadedData, String>;

#[derive(Clone, Copy, PartialEq)]
enum Tab {
    Items,
    Monsters,
}

#[derive(Clone, Copy, PartialEq)]
enum Sort {
    Type,
    Id,
    Name,
    Level,
    Attack,
    Defense,
    Resistance,
    Quality,
}

impl Sort {
    const ALL: [(Self, &'static str); 8] = [
        (Self::Type, "Type"),
        (Self::Id, "ID"),
        (Self::Name, "Name"),
        (Self::Level, "Level"),
        (Self::Attack, "Attack"),
        (Self::Defense, "Defense"),
        (Self::Resistance, "Resistance"),
        (Self::Quality, "Quality"),
    ];
    fn label(self) -> &'static str {
        Self::ALL.iter().find(|(key, _)| *key == self).unwrap().1
    }
}

#[derive(Clone, Copy, PartialEq)]
enum MobSort {
    Id,
    Name,
    Level,
    Hp,
    Attack,
    Defense,
    Exp,
    Status,
}

impl MobSort {
    const ALL: [(Self, &'static str); 8] = [
        (Self::Id, "ID"),
        (Self::Name, "Name"),
        (Self::Level, "Level"),
        (Self::Hp, "Max HP"),
        (Self::Attack, "Attack"),
        (Self::Defense, "Defense"),
        (Self::Exp, "EXP"),
        (Self::Status, "Status"),
    ];
    fn label(self) -> &'static str {
        Self::ALL.iter().find(|(key, _)| *key == self).unwrap().1
    }
}

struct MonsterView {
    catalog: Option<MonsterCatalog>,
    filter: MonsterFilter,
    sort: MobSort,
    descending: bool,
    results: Vec<usize>,
    selected: Option<usize>,
    count: i32,
}

impl MonsterView {
    fn new() -> Self {
        Self {
            catalog: None,
            filter: MonsterFilter::default(),
            sort: MobSort::Id,
            descending: false,
            results: Vec::new(),
            selected: None,
            count: 1,
        }
    }

    fn refresh(&mut self) {
        let Some(catalog) = &self.catalog else {
            return;
        };
        self.results = catalog
            .monsters
            .iter()
            .enumerate()
            .filter(|(_, m)| self.filter.matches(m))
            .map(|(i, _)| i)
            .collect();
        self.results.sort_by(|&a, &b| {
            let (a, b) = (&catalog.monsters[a], &catalog.monsters[b]);
            let cmp = match self.sort {
                MobSort::Id => a.id.cmp(&b.id),
                MobSort::Name => a.name.to_lowercase().cmp(&b.name.to_lowercase()),
                MobSort::Level => a.level.cmp(&b.level),
                MobSort::Hp => a.max_hp.cmp(&b.max_hp),
                MobSort::Attack => a.attack.cmp(&b.attack),
                MobSort::Defense => a.defense.cmp(&b.defense),
                MobSort::Exp => a.exp.cmp(&b.exp),
                MobSort::Status => a.worst().cmp(&b.worst()),
            };
            (if self.descending { cmp.reverse() } else { cmp }).then_with(|| a.id.cmp(&b.id))
        });
        if self
            .selected
            .is_some_and(|selected| !self.results.contains(&selected))
        {
            self.selected = None;
        }
    }
}

struct Browser {
    tab: Tab,
    catalog: Option<Catalog>,
    icons: IconStore,
    loading: Option<mpsc::Receiver<Loaded>>,
    root: Option<PathBuf>,
    error: Option<String>,
    /// One half failed to load while the other worked.
    load_notes: Vec<String>,
    filter: Filter,
    sort: Sort,
    descending: bool,
    results: Vec<usize>,
    selected: Option<usize>,
    quantity: i32,
    mobs: MonsterView,
    copied: Option<(String, std::time::Instant)>,
    /// How table text is decoded; Auto reads the script off each file.
    codec: Codec,
    /// Set when no CJK fallback font could be loaded from the system.
    font_note: Option<String>,
    /// The loaded data, kept for the 3D preview's lazy reads.
    assets: Option<Arc<Assets>>,
    preview: PreviewPane,
    mob_preview: MonsterPreview,
}

/// The 3D view: the items put on, on a character, in motion.
struct PreviewPane {
    /// Read on the first preview after each load.
    models: Option<AvatarModels>,
    /// The loaded data has avatar models (the tester package does not).
    available: bool,
    sex: Sex,
    action: Action,
    paused: bool,
    camera: Camera,
    /// Seconds into the motion.
    clock: f32,
    /// One item per slot, with its name: selecting an item puts it on, and
    /// it stays on until taken off.
    worn: Vec<(Slot, ItemCategory, i32, String)>,
    /// The last selection put on, so a selection is put on once.
    put_on: Option<(ItemCategory, i32)>,
    /// What `scene` shows: (worn items, item looked at, sex).
    shown: Option<(Vec<(ItemCategory, i32)>, Option<ItemCategory>, Sex)>,
    outfit: Option<Outfit>,
    scene: Option<Arc<Scene>>,
    /// The action `animation` and `framing` were made for.
    animated: Option<Action>,
    animation: Option<Animation>,
    framing: Option<Framing>,
    motion_note: Option<String>,
    gl: Arc<Mutex<GlState>>,
}

impl PreviewPane {
    fn new() -> Self {
        Self {
            models: None,
            available: false,
            sex: Sex::Male,
            action: Action::Idle,
            paused: false,
            camera: Camera::default(),
            clock: 0.0,
            worn: Vec::new(),
            put_on: None,
            shown: None,
            outfit: None,
            scene: None,
            animated: None,
            animation: None,
            framing: None,
            motion_note: None,
            gl: Arc::new(Mutex::new(GlState::default())),
        }
    }

    /// Forget the tables and what is worn: item numbers belong to one data folder.
    fn reset(&mut self) {
        *self = Self {
            sex: self.sex,
            action: self.action,
            camera: self.camera,
            gl: self.gl.clone(),
            ..Self::new()
        };
    }
}

/// The 3D view of the selected monster, in motion.
struct MonsterPreview {
    /// Read on the first preview after each load.
    models: Option<MonsterModels>,
    /// The loaded data has monster models (the tester package does not).
    available: bool,
    /// `MOB_ANI_*` slot, `None` for the bind pose. Kept from monster to
    /// monster when the next one has it, so motions compare side by side.
    motion: Option<usize>,
    paused: bool,
    camera: Camera,
    /// Seconds into the motion.
    clock: f32,
    /// The LIST_NPC row `scene` shows.
    shown: Option<i32>,
    scene: Option<Arc<Scene>>,
    /// The motion slots that row has.
    slots: Vec<usize>,
    /// Metres tall in game: the first idle pose, times the table scale.
    height: f32,
    /// The motion `animation` and `framing` were made for.
    animated: Option<Option<usize>>,
    animation: Option<Animation>,
    framing: Option<Framing>,
    motion_note: Option<String>,
}

impl MonsterPreview {
    fn new() -> Self {
        Self {
            models: None,
            available: false,
            motion: Some(0),
            paused: false,
            camera: Camera::default(),
            clock: 0.0,
            shown: None,
            scene: None,
            slots: Vec::new(),
            height: 0.0,
            animated: None,
            animation: None,
            framing: None,
            motion_note: None,
        }
    }

    /// Forget the tables: row numbers belong to one data folder.
    fn reset(&mut self) {
        *self = Self {
            motion: self.motion,
            ..Self::new()
        };
    }
}

/// GPU side of the pane, touched only inside paint callbacks (where eframe's
/// GL context is current) and on exit.
#[derive(Default)]
struct GlState {
    renderer: Option<GlScene>,
    /// The scene in the GPU buffers.
    uploaded: Option<Arc<Scene>>,
    error: Option<String>,
}

impl GlState {
    fn paint(
        &mut self,
        gl: &eframe::glow::Context,
        info: &egui::PaintCallbackInfo,
        scene: &Arc<Scene>,
        posed: &Posed,
        view_projection: &preview::Mat4,
        light: [f32; 3],
    ) {
        if self.renderer.is_none() && self.error.is_none() {
            match GlScene::new(gl) {
                Ok(renderer) => self.renderer = Some(renderer),
                Err(e) => self.error = Some(format!("3D preview unavailable: {e}")),
            }
        }
        let Some(renderer) = &mut self.renderer else {
            return;
        };
        if !self.uploaded.as_ref().is_some_and(|u| Arc::ptr_eq(u, scene)) {
            if let Err(e) = renderer.upload(gl, scene) {
                self.error = Some(format!("Could not upload the model: {e}"));
            }
            self.uploaded = Some(scene.clone());
        }
        // The pane, cut to what of it is visible.
        let (pane, clip) = (info.viewport_in_pixels(), info.clip_rect_in_pixels());
        let left = pane.left_px.max(clip.left_px);
        let bottom = pane.from_bottom_px.max(clip.from_bottom_px);
        let right = (pane.left_px + pane.width_px).min(clip.left_px + clip.width_px);
        let top = (pane.from_bottom_px + pane.height_px).min(clip.from_bottom_px + clip.height_px);
        if right > left && top > bottom {
            let scissor = [left, bottom, right - left, top - bottom];
            renderer.paint(gl, posed, view_projection, light, scissor);
        }
    }
}

impl Browser {
    fn new(cc: &eframe::CreationContext<'_>, root: Option<PathBuf>, codec: Codec) -> Self {
        cc.egui_ctx.set_visuals(egui::Visuals::dark());
        let fonts = install_fallback_fonts(&cc.egui_ctx);
        let font_note = fonts.is_empty().then(|| {
            "No Japanese/Korean font found in the Windows Fonts folder: foreign names draw as \
             boxes. Set ROSE_GM_FONT to a .ttf/.ttc to use one."
                .to_string()
        });
        let mut app = Self {
            codec,
            font_note,
            tab: Tab::Items,
            catalog: None,
            icons: IconStore::empty(Path::new(".")),
            loading: None,
            root: None,
            error: None,
            load_notes: Vec::new(),
            filter: Filter::default(),
            sort: Sort::Type,
            descending: false,
            results: Vec::new(),
            selected: None,
            quantity: 1,
            mobs: MonsterView::new(),
            copied: None,
            assets: None,
            preview: PreviewPane::new(),
            mob_preview: MonsterPreview::new(),
        };
        if let Some(root) = root {
            app.load(root, &cc.egui_ctx);
        }
        app
    }

    fn load(&mut self, root: PathBuf, ctx: &egui::Context) {
        self.root = Some(root.clone());
        self.catalog = None;
        self.results.clear();
        self.selected = None;
        self.mobs.catalog = None;
        self.mobs.results.clear();
        self.mobs.selected = None;
        self.error = None;
        self.load_notes.clear();
        self.icons = IconStore::empty(Path::new("."));
        self.assets = None;
        self.preview.reset();
        self.mob_preview.reset();
        let (tx, rx) = mpsc::channel();
        let ctx = ctx.clone();
        let codec = self.codec;
        self.loading = Some(rx);
        std::thread::spawn(move || {
            let result = Assets::open(&root)
                .map(|assets| {
                    let assets = Arc::new(assets);
                    LoadedData {
                        items: Catalog::load_with(&assets, codec).map_err(|e| format!("{e:#}")),
                        monsters: MonsterCatalog::load_with(&assets, codec)
                            .map_err(|e| format!("{e:#}")),
                        assets,
                    }
                })
                .map_err(|e| format!("{e:#}"));
            let _ = tx.send(result);
            ctx.request_repaint();
        });
    }

    fn poll_load(&mut self) {
        let Some(receiver) = &self.loading else {
            return;
        };
        let result = match receiver.try_recv() {
            Ok(result) => result,
            Err(mpsc::TryRecvError::Empty) => return,
            Err(mpsc::TryRecvError::Disconnected) => {
                Err("Loading stopped unexpectedly. Please reopen the data folder.".into())
            }
        };
        self.loading = None;
        let loaded = match result {
            Ok(loaded) => loaded,
            Err(e) => {
                self.error = Some(e);
                return;
            }
        };
        self.preview.available = preview::has_avatar_models(&loaded.assets);
        self.mob_preview.available = preview_monster::has_npc_models(&loaded.assets);
        self.assets = Some(loaded.assets.clone());
        match (loaded.items, loaded.monsters) {
            (Err(items), Err(monsters)) => {
                self.error = Some(format!("Items: {items}\n\nMonsters: {monsters}"));
            }
            (items, monsters) => {
                match items {
                    Ok(mut catalog) => {
                        match IconStore::from_assets(loaded.assets) {
                            Ok(icons) => self.icons = icons,
                            Err(e) => catalog.warnings.push(format!("Icons unavailable: {e:#}")),
                        }
                        self.catalog = Some(catalog);
                        self.refresh();
                    }
                    Err(e) => {
                        self.load_notes.push(format!("Items unavailable: {e}"));
                        self.tab = Tab::Monsters;
                    }
                }
                match monsters {
                    Ok(catalog) => {
                        self.mobs.catalog = Some(catalog);
                        self.mobs.refresh();
                    }
                    Err(e) => {
                        self.load_notes.push(format!("Monsters unavailable: {e}"));
                        self.tab = Tab::Items;
                    }
                }
            }
        }
    }

    fn refresh(&mut self) {
        let Some(catalog) = &self.catalog else {
            return;
        };
        self.results = catalog
            .items
            .iter()
            .enumerate()
            .filter(|(_, item)| self.filter.matches(item))
            .map(|(i, _)| i)
            .collect();
        self.results.sort_by(|&a, &b| {
            let (a, b) = (&catalog.items[a], &catalog.items[b]);
            let cmp = match self.sort {
                Sort::Type => (a.item.category as u8).cmp(&(b.item.category as u8)),
                Sort::Id => a.item.id.cmp(&b.item.id),
                Sort::Name => a.item.name.to_lowercase().cmp(&b.item.name.to_lowercase()),
                Sort::Level => a.level.cmp(&b.level),
                Sort::Attack => a.attack.cmp(&b.attack),
                Sort::Defense => a.defense.cmp(&b.defense),
                Sort::Resistance => a.resistance.cmp(&b.resistance),
                Sort::Quality => a.quality.cmp(&b.quality),
            };
            (if self.descending { cmp.reverse() } else { cmp }).then_with(|| {
                (a.item.category as u8, a.item.id).cmp(&(b.item.category as u8, b.item.id))
            })
        });
        if self
            .selected
            .is_some_and(|selected| !self.results.contains(&selected))
        {
            self.selected = None;
        }
    }

    fn copy(&mut self, command: String, ctx: &egui::Context) {
        ctx.copy_text(command.clone());
        self.copied = Some((command, std::time::Instant::now()));
        ctx.request_repaint_after(std::time::Duration::from_secs(4));
    }

    fn filters(&mut self, ui: &mut egui::Ui) {
        ui.heading("Find an item");
        ui.add_space(8.0);
        search_box(
            ui,
            &mut self.filter.search,
            ITEM_SEARCH,
            "Name, ID, type:ID...",
        );
        ui.add_space(8.0);
        let previous_category = self.filter.category;
        egui::ComboBox::from_label("Item type")
            .width(160.0)
            .selected_text(self.filter.category.map_or("All types".into(), |c| {
                format!("{} - {}", c as u8, c.display())
            }))
            .show_ui(ui, |ui| {
                ui.selectable_value(&mut self.filter.category, None, "All types");
                for &cat in ItemCategory::ALL {
                    ui.selectable_value(
                        &mut self.filter.category,
                        Some(cat),
                        format!("{} - {}", cat as u8, cat.display()),
                    );
                }
            });
        if previous_category != self.filter.category {
            self.filter.class = None;
        }
        let mut classes: Vec<i32> = self
            .catalog
            .iter()
            .flat_map(|c| &c.items)
            .filter(|item| {
                self.filter
                    .category
                    .map_or(true, |cat| cat == item.item.category)
            })
            .map(|item| item.class)
            .filter(|&class| class > 0)
            .collect();
        classes.sort_unstable();
        classes.dedup();
        egui::ComboBox::from_label("Subtype")
            .width(160.0)
            .selected_text(
                self.filter
                    .class
                    .map_or("All subtypes".into(), |c| c.to_string()),
            )
            .show_ui(ui, |ui| {
                ui.selectable_value(&mut self.filter.class, None, "All subtypes");
                for class in classes {
                    ui.selectable_value(&mut self.filter.class, Some(class), class.to_string());
                }
            });
        ui.add_space(12.0);
        ui.label(RichText::new("Stat ranges").strong());
        ui.small("Leave a bound blank for no limit.");
        range_ui(ui, "Required level", &mut self.filter.level);
        range_ui(ui, "Attack power", &mut self.filter.attack);
        range_ui(ui, "Defense", &mut self.filter.defense);
        range_ui(ui, "Resistance", &mut self.filter.resistance);
        range_ui(ui, "Quality", &mut self.filter.quality);
        ui.add_space(8.0);
        ui.checkbox(&mut self.filter.spawnable_only, "Spawnable items only")
            .on_hover_text("Hide rows without an icon or outside the game's item ID range.");
        if ui.button("Clear filters").clicked() {
            self.filter = Filter::default();
        }
        ui.add_space(12.0);
        ui.small(
            "Stats are base item values, before bonuses, gems, refinement or character stats.",
        );
        ui.small("Level 0 = no level requirement. A dash means the stat does not apply.");
        ui.separator();
        ui.label("Select an item to inspect it.");
        ui.label("Copy its command, then paste it into game chat.");
        ui.small("Ctrl+F jumps to the search box. Double-click a name to copy its command.");
    }

    fn details(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        let Some(item) = self
            .selected
            .and_then(|i| self.catalog.as_ref()?.items.get(i))
        else {
            ui.label("Select an item for details and its GM command.");
            return;
        };
        let mut command_to_copy = None;
        ui.horizontal(|ui| {
            draw_icon(ui, ctx, &mut self.icons, item, 48.0);
            ui.vertical(|ui| {
                ui.strong(&item.item.name);
                ui.label(format!(
                    "Type {} ({})  |  ID {}  |  Subtype {}",
                    item.item.category as u8,
                    item.item.category.display(),
                    item.item.id,
                    item.class
                ));
                ui.label(format!(
                    "Level {}   Attack {}   Defense {}   Resistance {}   Quality {}",
                    stat(item.level),
                    stat(item.attack),
                    stat(item.defense),
                    stat(item.resistance),
                    item.quality
                ));
            });
            ui.separator();
            ui.vertical(|ui| {
                if item.stackable() {
                    ui.horizontal(|ui| {
                        ui.label("Quantity");
                        ui.add(egui::DragValue::new(&mut self.quantity).clamp_range(1..=100));
                    });
                }
                if let Some(command) = item.command(self.quantity) {
                    ui.horizontal(|ui| {
                        ui.monospace(&command);
                        if ui.button("Copy command").clicked() {
                            command_to_copy = Some(command);
                        }
                    });
                } else {
                    ui.colored_label(Color32::LIGHT_RED, "This row cannot be spawned.");
                }
            });
        });
        if !item.description.is_empty() {
            ui.label(&item.description);
        }
        if let Some(command) = command_to_copy {
            self.copy(command, ctx);
        }
    }

    fn table(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        let Some(catalog) = &self.catalog else {
            return;
        };
        ui.horizontal(|ui| {
            ui.strong(format!(
                "{} matching / {} items",
                self.results.len(),
                catalog.items.len()
            ));
            ui.separator();
            egui::ComboBox::from_label("Sort")
                .selected_text(self.sort.label())
                .show_ui(ui, |ui| {
                    for (sort, label) in Sort::ALL {
                        ui.selectable_value(&mut self.sort, sort, label);
                    }
                });
            ui.checkbox(&mut self.descending, "Descending");
        });
        ui.add_space(6.0);
        if self.results.is_empty() {
            ui.label("No matching items. Adjust or clear the filters.");
            return;
        }
        let mut copy = None;
        TableBuilder::new(ui)
            .striped(true)
            .resizable(true)
            .cell_layout(egui::Layout::left_to_right(egui::Align::Center))
            .column(Column::exact(42.0))
            .column(Column::remainder().at_least(150.0))
            .column(Column::initial(115.0))
            .column(Column::initial(50.0))
            .columns(Column::initial(55.0), 4)
            .column(Column::exact(62.0))
            .header(24.0, |mut header| {
                for label in [
                    "Icon", "Name", "Type", "ID", "Level", "Attack", "Defense", "Resist.",
                    "Command",
                ] {
                    header.col(|ui| {
                        ui.strong(label);
                    });
                }
            })
            .body(|body| {
                body.rows(44.0, self.results.len(), |mut row| {
                    let index = self.results[row.index()];
                    let item = &catalog.items[index];
                    row.set_selected(self.selected == Some(index));
                    row.col(|ui| {
                        draw_icon(ui, ctx, &mut self.icons, item, 40.0);
                    });
                    row.col(|ui| {
                        let response = ui
                            .selectable_label(self.selected == Some(index), &item.item.name)
                            .on_hover_text(&item.description);
                        if response.clicked() {
                            self.selected = Some(index);
                        }
                        if response.double_clicked() {
                            copy = item.command(1);
                        }
                    });
                    row.col(|ui| {
                        ui.label(format!(
                            "{} - {}",
                            item.item.category as u8,
                            item.item.category.display()
                        ));
                    });
                    row.col(|ui| {
                        ui.monospace(item.item.id.to_string());
                    });
                    for value in [item.level, item.attack, item.defense, item.resistance] {
                        row.col(|ui| {
                            ui.label(stat(value));
                        });
                    }
                    row.col(|ui| {
                        if ui
                            .add_enabled(item.spawnable(), egui::Button::new("Copy"))
                            .on_hover_text("Copy /item command for one item")
                            .clicked()
                        {
                            copy = item.command(1);
                            self.selected = Some(index);
                        }
                    });
                });
            });
        if let Some(command) = copy {
            self.copy(command, ctx);
        }
    }

    fn monster_filters(&mut self, ui: &mut egui::Ui) {
        let mobs = &mut self.mobs;
        ui.heading("Find a monster");
        ui.add_space(8.0);
        search_box(
            ui,
            &mut mobs.filter.search,
            MOB_SEARCH,
            "Name or ID (#123 = exact ID)",
        );
        ui.add_space(8.0);
        egui::ComboBox::from_label("Show")
            .width(160.0)
            .selected_text(mobs.filter.status.label())
            .show_ui(ui, |ui| {
                for (status, label) in StatusFilter::ALL {
                    ui.selectable_value(&mut mobs.filter.status, status, label);
                }
            });
        ui.checkbox(&mut mobs.filter.include_town_npcs, "Include town NPCs")
            .on_hover_text("Town NPCs are in the same table, but /mon cannot spawn them.");
        ui.add_space(12.0);
        ui.label(RichText::new("Stat ranges").strong());
        ui.small("Leave a bound blank for no limit.");
        range_ui(ui, "Level", &mut mobs.filter.level);
        range_ui(ui, "Max HP", &mut mobs.filter.hp);
        ui.add_space(8.0);
        ui.horizontal(|ui| {
            ui.label("Spawn count");
            ui.add(egui::DragValue::new(&mut mobs.count).clamp_range(1..=MAX_SPAWN_COUNT));
        })
        .response
        .on_hover_text("Second /mon argument. The server caps it at 100.");
        if ui.button("Clear filters").clicked() {
            mobs.filter = MonsterFilter::default();
        }
        ui.add_space(12.0);
        ui.label(RichText::new("Names in rose are broken.").color(ROSE));
        ui.small(
            "The server refuses them, or they spawn invisible, untextured or frozen. \
             Select one to see why.",
        );
        ui.label(RichText::new("Amber status = minor problems.").color(AMBER));
        ui.small("For example a missing weapon prop or a blank in-game name.");
        ui.separator();
        ui.label("Copy a command, then paste it into game chat.");
        ui.small("/mon ID COUNT spawns around you. GM access is required in the game.");
        ui.small("Ctrl+F jumps to the search box. Double-click a name to copy its command.");
    }

    fn monster_details(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        let Some(monster) = self
            .mobs
            .selected
            .and_then(|i| self.mobs.catalog.as_ref()?.monsters.get(i))
        else {
            ui.label("Select a monster for details, problems and its GM command.");
            return;
        };
        let mut command_to_copy = None;
        ui.horizontal(|ui| {
            ui.vertical(|ui| {
                ui.label(monster_name(monster).strong().size(15.0));
                let mut line = format!("ID {}", monster.id);
                if monster.table_name != monster.name && !monster.table_name.is_empty() {
                    line += &format!("  |  Table name \"{}\"", monster.table_name);
                }
                if monster.town_npc() {
                    line += "  |  Town NPC";
                }
                ui.label(line);
                ui.label(format!(
                    "Level {}   HP {} ({} per level)   Attack {}   Hit {}   Defense {}   Resistance {}   Dodge {}",
                    monster.level,
                    monster.max_hp,
                    monster.hp,
                    monster.attack,
                    monster.hit,
                    monster.defense,
                    monster.resistance,
                    monster.avoid
                ));
                ui.label(format!(
                    "EXP {}   {} damage   Attack speed {}   Range {:.1} m   Walk/run {}/{}",
                    monster.exp,
                    if monster.magic_damage {
                        "Magic"
                    } else {
                        "Physical"
                    },
                    monster.attack_speed,
                    monster.attack_range as f32 / 100.0,
                    monster.walk_speed,
                    monster.run_speed
                ));
            });
            ui.separator();
            ui.vertical(|ui| {
                ui.horizontal(|ui| {
                    ui.label("Count");
                    ui.add(
                        egui::DragValue::new(&mut self.mobs.count).clamp_range(1..=MAX_SPAWN_COUNT),
                    );
                });
                match monster.command(self.mobs.count) {
                    Some(command) => {
                        ui.horizontal(|ui| {
                            ui.monospace(&command);
                            if ui.button("Copy command").clicked() {
                                command_to_copy = Some(command);
                            }
                        });
                    }
                    None => {
                        ui.colored_label(ROSE, "/mon cannot spawn this row.");
                    }
                }
            });
        });
        if monster.issues.is_empty() {
            ui.colored_label(Color32::LIGHT_GREEN, "No problems found.");
        } else {
            ui.add_space(4.0);
            for issue in &monster.issues {
                let (color, tag) = match issue.severity {
                    Severity::Error => (ROSE, "Broken"),
                    Severity::Warning => (AMBER, "Minor"),
                };
                ui.horizontal_wrapped(|ui| {
                    ui.label(RichText::new(tag).color(color).strong());
                    ui.label(&issue.text);
                });
            }
        }
        if let Some(blocker) = monster.spawn_blocker.filter(|_| monster.town_npc()) {
            ui.small(blocker);
        }
        if let Some(command) = command_to_copy {
            self.copy(command, ctx);
        }
    }

    fn monster_table(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        let MonsterView {
            catalog,
            filter: _,
            sort,
            descending,
            results,
            selected,
            count,
        } = &mut self.mobs;
        let Some(catalog) = catalog.as_ref() else {
            return;
        };
        let broken = results
            .iter()
            .filter(|&&i| catalog.monsters[i].worst() == Some(Severity::Error))
            .count();
        ui.horizontal(|ui| {
            ui.strong(format!(
                "{} matching / {} rows",
                results.len(),
                catalog.monsters.len()
            ));
            if broken > 0 {
                ui.label(RichText::new(format!("{broken} broken")).color(ROSE));
            }
            ui.separator();
            egui::ComboBox::from_label("Sort")
                .selected_text(sort.label())
                .show_ui(ui, |ui| {
                    for (value, label) in MobSort::ALL {
                        ui.selectable_value(sort, value, label);
                    }
                });
            ui.checkbox(descending, "Descending");
        });
        if !catalog.file_checks {
            ui.colored_label(
                AMBER,
                "Missing-file checks are off for this folder: see Data warnings above.",
            );
        }
        ui.add_space(6.0);
        if results.is_empty() {
            ui.label("No matching monsters. Adjust or clear the filters.");
            return;
        }
        let mut copy = None;
        // Own ID: egui keeps column widths per table ID, and the item table
        // would otherwise hand its widths to this one.
        ui.push_id("monster_table", |ui| {
            TableBuilder::new(ui)
                .striped(true)
                .resizable(true)
                .cell_layout(egui::Layout::left_to_right(egui::Align::Center))
                .column(Column::initial(300.0).at_least(150.0).clip(true))
                .column(Column::initial(55.0))
                .column(Column::initial(50.0))
                .columns(Column::initial(62.0), 4)
                .column(Column::initial(95.0))
                .column(Column::remainder().at_least(62.0))
                .header(24.0, |mut header| {
                    for label in [
                        "Name", "ID", "Level", "Max HP", "Attack", "Defense", "Resist.", "Status",
                        "Command",
                    ] {
                        header.col(|ui| {
                            ui.strong(label);
                        });
                    }
                })
                .body(|body| {
                    body.rows(26.0, results.len(), |mut row| {
                        let index = results[row.index()];
                        let monster = &catalog.monsters[index];
                        row.set_selected(*selected == Some(index));
                        row.col(|ui| {
                            let response = ui
                                .selectable_label(*selected == Some(index), monster_name(monster));
                            if response.clicked() {
                                *selected = Some(index);
                            }
                            if response.double_clicked() {
                                copy = monster.command(*count);
                            }
                        });
                        row.col(|ui| {
                            ui.monospace(monster.id.to_string());
                        });
                        for value in [
                            monster.level,
                            monster.max_hp,
                            monster.attack,
                            monster.defense,
                            monster.resistance,
                        ] {
                            row.col(|ui| {
                                ui.label(value.to_string());
                            });
                        }
                        row.col(|ui| {
                            let (text, color) = status(monster);
                            let response = ui.label(RichText::new(text).color(color));
                            if !monster.issues.is_empty() {
                                response.on_hover_ui(|ui| {
                                    for issue in &monster.issues {
                                        ui.label(&issue.text);
                                    }
                                });
                            }
                        });
                        row.col(|ui| {
                            let command = monster.command(*count);
                            let hover = command
                                .clone()
                                .unwrap_or_else(|| monster.spawn_blocker.unwrap_or("").to_string());
                            if ui
                                .add_enabled(command.is_some(), egui::Button::new("Copy"))
                                .on_hover_text(&hover)
                                .on_disabled_hover_text(&hover)
                                .clicked()
                            {
                                copy = command;
                                *selected = Some(index);
                            }
                        });
                    });
                });
        });
        if let Some(command) = copy {
            self.copy(command, ctx);
        }
    }

    fn warnings(&self) -> Vec<&str> {
        let items = self.catalog.iter().flat_map(|c| &c.warnings);
        let mobs = self.mobs.catalog.iter().flat_map(|c| &c.warnings);
        self.font_note
            .iter()
            .chain(&self.load_notes)
            .chain(items)
            .chain(mobs)
            .map(String::as_str)
            .collect()
    }
}

impl eframe::App for Browser {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.poll_load();
        let loaded = self.catalog.is_some() || self.mobs.catalog.is_some();
        egui::TopBottomPanel::top("top").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.heading("ROSE GM Browser");
                ui.separator();
                if loaded {
                    let items = self.catalog.as_ref().map_or(0, |c| c.items.len());
                    let mobs = self.mobs.catalog.as_ref().map_or(0, |c| c.monsters.len());
                    ui.add_enabled_ui(self.catalog.is_some(), |ui| {
                        ui.selectable_value(&mut self.tab, Tab::Items, format!("Items ({items})"));
                    });
                    ui.add_enabled_ui(self.mobs.catalog.is_some(), |ui| {
                        ui.selectable_value(
                            &mut self.tab,
                            Tab::Monsters,
                            format!("Monsters ({mobs})"),
                        );
                    });
                    ui.separator();
                }
                if ui
                    .add_enabled(
                        self.loading.is_none(),
                        egui::Button::new("Open data folder..."),
                    )
                    .clicked()
                {
                    if let Some(root) = rfd::FileDialog::new()
                        .set_title("Loose data folder (or game folder containing data.idx)")
                        .pick_folder()
                    {
                        self.load(root, ctx);
                    }
                }
                if ui
                    .add_enabled(self.loading.is_none(), egui::Button::new("Open VFS..."))
                    .clicked()
                {
                    if let Some(index) = rfd::FileDialog::new()
                        .set_title("Open a packed game data index")
                        .add_filter("ROSE VFS index", &["idx"])
                        .pick_file()
                    {
                        self.load(index, ctx);
                    }
                }
                if ui
                    .add_enabled(
                        self.loading.is_none() && self.root.is_some(),
                        egui::Button::new("Reload"),
                    )
                    .clicked()
                {
                    self.load(self.root.clone().unwrap(), ctx);
                }
                let before = self.codec;
                egui::ComboBox::from_id_source("text-codec")
                    .selected_text(format!("Text: {}", self.codec.label()))
                    .show_ui(ui, |ui| {
                        for codec in Codec::ALL {
                            ui.selectable_value(&mut self.codec, codec, codec.label());
                        }
                    })
                    .response
                    .on_hover_text(
                        "How the tables' text is decoded. Auto reads the script off each \
                         file (Jrose is Japanese, retail and RoseZA are Korean, China \
                         Rose is Chinese, ours is Western); pick one only if Auto gets \
                         a folder wrong.",
                    );
                if self.codec != before && self.loading.is_none() {
                    if let Some(root) = self.root.clone() {
                        self.load(root, ctx);
                    }
                }
                if let Some((command, when)) = &self.copied {
                    if when.elapsed().as_secs() < 4 {
                        ui.colored_label(Color32::LIGHT_GREEN, format!("Copied {command}"));
                    }
                }
            });
            if let Some(root) = &self.root {
                ui.small(root.display().to_string());
            }
            let warnings = self.warnings();
            if !warnings.is_empty() {
                egui::CollapsingHeader::new(format!("Data warnings ({})", warnings.len())).show(
                    ui,
                    |ui| {
                        egui::ScrollArea::vertical()
                            .max_height(110.0)
                            .show(ui, |ui| {
                                for warning in warnings {
                                    ui.colored_label(Color32::LIGHT_YELLOW, warning);
                                }
                            });
                    },
                );
            }
        });
        if !loaded {
            egui::CentralPanel::default().show(ctx, |ui| {
                ui.add_space(55.0);
                if self.loading.is_some() {
                    ui.spinner();
                    ui.heading("Loading items and monsters...");
                } else {
                    ui.heading("Find items and monsters. Copy commands. Get testing.");
                    ui.label(
                        "Keep the supplied data folder beside this tool for automatic loading.",
                    );
                    ui.label("You can also open another data folder or a VFS index.");
                    ui.label(
                        "The tool reads game data. GM access is required to use /item and /mon \
                         in the game.",
                    );
                    if let Some(error) = &self.error {
                        ui.colored_label(Color32::LIGHT_RED, error);
                    }
                }
            });
            return;
        }
        if ctx.input(|i| i.modifiers.command && i.key_pressed(egui::Key::F)) {
            let id = match self.tab {
                Tab::Items => ITEM_SEARCH,
                Tab::Monsters => MOB_SEARCH,
            };
            ctx.memory_mut(|m| m.request_focus(egui::Id::new(id)));
        }
        match self.tab {
            Tab::Items => self.items_page(ctx),
            Tab::Monsters => self.monsters_page(ctx),
        }
    }

    fn on_exit(&mut self, gl: Option<&eframe::glow::Context>) {
        let renderer = self.preview.gl.lock().ok().and_then(|mut state| state.renderer.take());
        if let (Some(gl), Some(mut renderer)) = (gl, renderer) {
            renderer.destroy(gl);
        }
    }
}

impl Browser {
    fn items_page(&mut self, ctx: &egui::Context) {
        let old_filter = self.filter.clone();
        let old_sort = (self.sort, self.descending);
        egui::SidePanel::left("filters")
            .exact_width(255.0)
            .show(ctx, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| self.filters(ui));
            });
        if old_filter != self.filter {
            self.refresh();
        }
        // Before the details panel, so the preview gets the full height.
        self.wear_selection();
        if self.preview.available
            && (self.selected_wearable().is_some() || !self.preview.worn.is_empty())
        {
            egui::SidePanel::right("preview")
                .default_width(340.0)
                .width_range(260.0..=720.0)
                .show(ctx, |ui| self.preview_pane(ui));
        }
        egui::TopBottomPanel::bottom("details")
            .min_height(112.0)
            .show(ctx, |ui| {
                egui::ScrollArea::vertical()
                    .max_height(190.0)
                    .show(ui, |ui| self.details(ui, ctx));
            });
        egui::CentralPanel::default().show(ctx, |ui| self.table(ui, ctx));
        if old_sort != (self.sort, self.descending) {
            self.refresh();
            ctx.request_repaint();
        }
    }

    /// (type, id) of the selected item when it is worn and the data has
    /// models to show it with.
    fn selected_wearable(&self) -> Option<(ItemCategory, i32)> {
        if !self.preview.available {
            return None;
        }
        let item = &self.catalog.as_ref()?.items.get(self.selected?)?.item;
        Slot::for_category(item.category).map(|_| (item.category, item.id))
    }

    /// Put a newly selected item on, in place of whatever its slot held.
    fn wear_selection(&mut self) {
        let selection = self.selected_wearable();
        if selection == self.preview.put_on {
            return;
        }
        self.preview.put_on = selection;
        let Some((category, id)) = selection else {
            return;
        };
        let Some(slot) = Slot::for_category(category) else {
            return;
        };
        let name = self
            .selected
            .and_then(|i| self.catalog.as_ref()?.items.get(i))
            .map_or_else(String::new, |item| item.item.name.clone());
        let worn = &mut self.preview.worn;
        worn.retain(|(s, ..)| *s != slot);
        worn.push((slot, category, id, name));
        worn.sort_by_key(|(s, ..)| *s);
    }

    /// The worn items on a character, drawn by `preview_gl`.
    fn preview_pane(&mut self, ui: &mut egui::Ui) {
        let focus = self.selected_wearable().map(|(category, _)| category);
        let (assets, codec) = (self.assets.clone(), self.codec);
        let pane = &mut self.preview;
        ui.heading("3D preview");
        ui.horizontal(|ui| {
            ui.selectable_value(&mut pane.sex, Sex::Male, "Male");
            ui.selectable_value(&mut pane.sex, Sex::Female, "Female");
            ui.separator();
            if ui.button("Reset view").clicked() {
                pane.camera = Camera::default();
            }
        });
        ui.horizontal(|ui| {
            egui::ComboBox::from_id_source("preview_motion")
                .selected_text(pane.action.label())
                .show_ui(ui, |ui| {
                    for action in Action::ALL {
                        ui.selectable_value(&mut pane.action, action, action.label());
                    }
                })
                .response
                .on_hover_text("The motion the client plays for this action with the weapon worn.");
            ui.add_enabled(pane.animation.is_some(), egui::Checkbox::new(&mut pane.paused, "Pause"));
        });
        let worn: Vec<(ItemCategory, i32)> = pane.worn.iter().map(|w| (w.1, w.2)).collect();
        let key = (worn, focus, pane.sex);
        if pane.shown.as_ref() != Some(&key) {
            pane.scene = None;
            pane.outfit = None;
            pane.animated = None;
            if let Some(assets) = &assets {
                let models = pane
                    .models
                    .get_or_insert_with(|| AvatarModels::load(assets, codec));
                let outfit = models.outfit(pane.sex, &key.0, focus);
                pane.scene = Some(Arc::new(models.build(assets, &outfit)));
                pane.outfit = Some(outfit);
            }
            pane.shown = Some(key);
        }
        let (Some(scene), Some(outfit), Some(assets), Some(models)) =
            (pane.scene.clone(), &pane.outfit, &assets, &pane.models)
        else {
            return;
        };
        if pane.animated != Some(pane.action) {
            (pane.animation, pane.motion_note) = match models.motion(assets, outfit, pane.action) {
                Ok(animation) => (animation, None),
                Err(e) => (None, Some(e)),
            };
            pane.framing = Some(scene.framing(pane.animation.as_ref()));
            pane.animated = Some(pane.action);
            pane.clock = 0.0;
        }
        if pane.animation.is_some() && !pane.paused {
            pane.clock += ui.input(|i| i.stable_dt).min(0.1);
            ui.ctx().request_repaint();
        }
        let notes: Vec<String> = models
            .warnings
            .iter()
            .chain(&scene.notes)
            .chain(&pane.motion_note)
            .cloned()
            .collect();
        let reserved = 60.0
            + 24.0 * pane.worn.len().max(1) as f32
            + if notes.is_empty() { 0.0 } else { 140.0 };
        let size = egui::vec2(
            ui.available_width(),
            (ui.available_height() - reserved).max(220.0),
        );
        let framing = pane.framing.unwrap_or_else(|| scene.framing(None));
        let posed = scene.pose(pane.animation.as_ref(), pane.clock);
        orbit_view(ui, size, &mut pane.camera, &pane.gl, scene, posed, &framing);
        ui.small("Drag to turn, scroll to zoom in on the selected item, double-click to reset.");
        ui.horizontal(|ui| {
            ui.strong("Wearing");
            if ui
                .small_button("Clear")
                .on_hover_text("Take everything off but the selected item.")
                .clicked()
            {
                pane.worn.clear();
                pane.put_on = None;
            }
        });
        if pane.worn.is_empty() {
            ui.small("Nothing: select an item to put it on.");
        }
        let mut take_off = None;
        for (i, (slot, _, id, name)) in pane.worn.iter().enumerate() {
            ui.horizontal(|ui| {
                if ui.small_button("Take off").clicked() {
                    take_off = Some(i);
                }
                ui.label(RichText::new(slot.label()).weak());
                ui.label(name).on_hover_text(format!("ID {id}"));
            });
        }
        if let Some(i) = take_off {
            pane.worn.remove(i);
        }
        if !notes.is_empty() {
            ui.label(RichText::new(format!("Not shown ({})", notes.len())).color(AMBER));
            egui::ScrollArea::vertical()
                .id_source("preview_notes")
                .max_height(110.0)
                .show(ui, |ui| {
                    for note in &notes {
                        ui.small(note);
                    }
                });
        }
    }

    /// The selected monster, drawn by `preview_gl`.
    fn monster_preview_pane(&mut self, ui: &mut egui::Ui) {
        let (Some(monster), Some(assets)) = (
            self.mobs
                .selected
                .and_then(|i| self.mobs.catalog.as_ref()?.monsters.get(i)),
            self.assets.clone(),
        ) else {
            return;
        };
        let pane = &mut self.mob_preview;
        ui.heading("3D preview");
        if pane.shown != Some(monster.id) {
            let models = pane
                .models
                .get_or_insert_with(|| MonsterModels::load(&assets));
            let scene = models.build(&assets, monster);
            pane.slots = models.motion_slots(monster.id);
            let idle = models.motion(&assets, monster.id, Some(0)).ok().flatten();
            pane.height = scene.height(idle.as_ref(), 0.0) * monster.scale as f32 / 100.0;
            if pane.motion.is_some_and(|slot| !pane.slots.contains(&slot)) {
                pane.motion = pane.slots.first().copied();
            }
            pane.scene = Some(Arc::new(scene));
            pane.animated = None;
            // Monsters run from a jelly bean to a dragon: frame each anew.
            pane.camera = Camera::default();
            pane.shown = Some(monster.id);
        }
        let label =
            |motion: Option<usize>| motion.map_or_else(|| "Bind pose".to_string(), motion_label);
        ui.horizontal(|ui| {
            egui::ComboBox::from_id_source("monster_motion")
                .selected_text(label(pane.motion))
                .show_ui(ui, |ui| {
                    for &slot in &pane.slots {
                        ui.selectable_value(&mut pane.motion, Some(slot), label(Some(slot)));
                    }
                    ui.selectable_value(&mut pane.motion, None, label(None));
                })
                .response
                .on_hover_text("The motions LIST_NPC.CHR gives this row.");
            ui.add_enabled(
                pane.animation.is_some(),
                egui::Checkbox::new(&mut pane.paused, "Pause"),
            );
            ui.separator();
            if ui.button("Reset view").clicked() {
                pane.camera = Camera::default();
            }
        });
        let (Some(scene), Some(models)) = (pane.scene.clone(), &pane.models) else {
            return;
        };
        if pane.animated != Some(pane.motion) {
            (pane.animation, pane.motion_note) =
                match models.motion(&assets, monster.id, pane.motion) {
                    Ok(animation) => (animation, None),
                    Err(e) => (None, Some(e)),
                };
            pane.framing = Some(scene.framing(pane.animation.as_ref()));
            pane.animated = Some(pane.motion);
            pane.clock = 0.0;
        }
        if pane.animation.is_some() && !pane.paused {
            pane.clock += ui.input(|i| i.stable_dt).min(0.1);
            ui.ctx().request_repaint();
        }
        let notes: Vec<String> = scene
            .notes
            .iter()
            .chain(&pane.motion_note)
            .cloned()
            .collect();
        let weapon_name = |category: ItemCategory, id: i32| {
            self.catalog
                .as_ref()
                .and_then(|c| {
                    c.items
                        .iter()
                        .find(|i| i.item.category == category && i.item.id == id)
                })
                .map_or_else(
                    || format!("model {id}"),
                    |i| format!("{} ({id})", i.item.name),
                )
        };
        let hands: Vec<(&str, String)> = [
            ("Right hand", ItemCategory::Weapon, monster.right_weapon),
            ("Left hand", ItemCategory::SubWpn, monster.left_weapon),
        ]
        .into_iter()
        .filter(|&(_, _, id)| id > 0)
        .map(|(hand, category, id)| (hand, weapon_name(category, id)))
        .collect();
        let reserved =
            84.0 + 24.0 * hands.len() as f32 + if notes.is_empty() { 0.0 } else { 140.0 };
        let size = egui::vec2(
            ui.available_width(),
            (ui.available_height() - reserved).max(220.0),
        );
        let framing = pane.framing.unwrap_or_else(|| scene.framing(None));
        let posed = scene.pose(pane.animation.as_ref(), pane.clock);
        orbit_view(
            ui,
            size,
            &mut pane.camera,
            &self.preview.gl,
            scene,
            posed,
            &framing,
        );
        ui.small("Drag to turn, scroll to zoom, double-click to reset.");
        ui.label(format!(
            "Scale {} %  |  about {:.1} m tall in game",
            monster.scale, pane.height
        ))
        .on_hover_text("LIST_NPC column 4, and the first idle pose's height times it. The view always fits the model.");
        for (hand, name) in &hands {
            ui.horizontal(|ui| {
                ui.label(RichText::new(*hand).weak());
                ui.label(name);
            });
        }
        if !notes.is_empty() {
            ui.label(RichText::new(format!("Not shown ({})", notes.len())).color(AMBER));
            egui::ScrollArea::vertical()
                .id_source("monster_preview_notes")
                .max_height(110.0)
                .show(ui, |ui| {
                    for note in &notes {
                        ui.small(note);
                    }
                });
        }
    }

    fn monsters_page(&mut self, ctx: &egui::Context) {
        let old_filter = self.mobs.filter.clone();
        let old_sort = (self.mobs.sort, self.mobs.descending);
        egui::SidePanel::left("monster_filters")
            .exact_width(255.0)
            .show(ctx, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| self.monster_filters(ui));
            });
        if old_filter != self.mobs.filter {
            self.mobs.refresh();
        }
        // Before the details panel, so the preview gets the full height.
        if self.mob_preview.available && self.mobs.selected.is_some() {
            egui::SidePanel::right("monster_preview")
                .default_width(340.0)
                .width_range(260.0..=720.0)
                .show(ctx, |ui| self.monster_preview_pane(ui));
        }
        egui::TopBottomPanel::bottom("monster_details")
            .min_height(130.0)
            .show(ctx, |ui| {
                egui::ScrollArea::vertical()
                    .max_height(230.0)
                    .show(ui, |ui| self.monster_details(ui, ctx));
            });
        egui::CentralPanel::default().show(ctx, |ui| self.monster_table(ui, ctx));
        if old_sort != (self.mobs.sort, self.mobs.descending) {
            self.mobs.refresh();
            ctx.request_repaint();
        }
    }
}

/// Draw `scene` posed as `posed` into a `size` pane both previews share:
/// drag turns, scroll zooms, double-click resets the camera.
fn orbit_view(
    ui: &mut egui::Ui,
    size: egui::Vec2,
    camera: &mut Camera,
    gl: &Arc<Mutex<GlState>>,
    scene: Arc<Scene>,
    posed: Posed,
    framing: &Framing,
) {
    let (rect, response) = ui.allocate_exact_size(size, egui::Sense::click_and_drag());
    if response.dragged() {
        let delta = response.drag_delta();
        camera.yaw -= delta.x * 0.01;
        camera.pitch = (camera.pitch + delta.y * 0.01).clamp(-1.3, 1.3);
    }
    if response.hovered() {
        let scroll = ui.input(|i| i.smooth_scroll_delta.y);
        if scroll != 0.0 {
            camera.zoom = (camera.zoom * (-scroll * 0.002).exp()).clamp(MIN_ZOOM, 3.0);
        }
    }
    if response.double_clicked() {
        *camera = Camera::default();
    }
    let (view_projection, light) = camera.matrices(framing, rect.aspect_ratio());
    let painter_gl = gl.clone();
    ui.painter().add(egui::PaintCallback {
        rect,
        callback: Arc::new(eframe::egui_glow::CallbackFn::new(move |info, painter| {
            if let Ok(mut gl) = painter_gl.lock() {
                gl.paint(painter.gl(), &info, &scene, &posed, &view_projection, light);
            }
        })),
    });
    let gl_error = gl.lock().ok().and_then(|gl| gl.error.clone());
    if let Some(error) = gl_error {
        ui.colored_label(Color32::LIGHT_RED, error);
    }
}

const ITEM_SEARCH: &str = "item_search";
const MOB_SEARCH: &str = "monster_search";

/// A full-width search field with a clear button; Ctrl+F focuses it by `id`.
fn search_box(ui: &mut egui::Ui, text: &mut String, id: &str, hint: &str) {
    ui.horizontal(|ui| {
        let clear = !text.is_empty()
            && ui
                .small_button("x")
                .on_hover_text("Clear the search")
                .clicked();
        ui.add(
            egui::TextEdit::singleline(text)
                .id(egui::Id::new(id))
                .hint_text(hint)
                .desired_width(f32::INFINITY),
        );
        if clear {
            text.clear();
        }
    });
}

fn monster_name(monster: &Monster) -> RichText {
    let text = RichText::new(&monster.name);
    match monster.worst() {
        Some(Severity::Error) => text.color(ROSE),
        _ if monster.town_npc() => text.color(Color32::GRAY),
        _ => text,
    }
}

fn status(monster: &Monster) -> (String, Color32) {
    let errors = monster
        .issues
        .iter()
        .filter(|i| i.severity == Severity::Error)
        .count();
    let warnings = monster.issues.len() - errors;
    if errors > 0 {
        (format!("Broken ({errors})"), ROSE)
    } else if warnings > 0 {
        (format!("{warnings} minor"), AMBER)
    } else if monster.town_npc() {
        ("Town NPC".into(), Color32::GRAY)
    } else {
        ("OK".into(), Color32::LIGHT_GREEN)
    }
}

fn range_ui(ui: &mut egui::Ui, label: &str, range: &mut RangeFilter) {
    ui.label(label);
    ui.horizontal(|ui| {
        ui.add(
            egui::TextEdit::singleline(&mut range.min)
                .hint_text("Min")
                .desired_width(95.0),
        );
        ui.label("to");
        ui.add(
            egui::TextEdit::singleline(&mut range.max)
                .hint_text("Max")
                .desired_width(95.0),
        );
    });
    if let Err(error) = range.bounds() {
        ui.colored_label(Color32::LIGHT_RED, error.to_string());
    }
}

fn stat(value: Option<i32>) -> String {
    value.map_or("-".into(), |v| v.to_string())
}

fn draw_icon(
    ui: &mut egui::Ui,
    ctx: &egui::Context,
    icons: &mut IconStore,
    item: &CatalogItem,
    size: f32,
) {
    if let Some(texture) = icons.icon_texture(ctx, item.item.icon_no) {
        ui.image((texture.id(), egui::vec2(size, size)));
    } else {
        let (rect, response) = ui.allocate_exact_size(egui::vec2(size, size), egui::Sense::hover());
        ui.painter().rect_filled(rect, 4.0, Color32::from_gray(45));
        ui.painter().text(
            rect.center(),
            egui::Align2::CENTER_CENTER,
            "?",
            egui::FontId::proportional(18.0),
            Color32::GRAY,
        );
        response.on_hover_text("Icon unavailable");
    }
}
