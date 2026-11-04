//! 3D preview of worn items, built the way the client builds an avatar
//! (client source in brackets):
//!
//! - A slot's model is the item number itself, looked up in that slot's part
//!   table for the character's sex [`CObjAVT::Create` -> `SetPartMODEL`; the
//!   tables are listed in `CBasicDATA::Load3DDATA`]. An empty slot is item 0,
//!   which for body, arms and feet is the bare body.
//! - Hair is `hair + HELMET_HAIR_TYPE(cap)` (LIST_CAP column 33): the cut that
//!   fits under that hat.
//! - Body, arms and feet are skinned, and a skinned mesh is stored in the
//!   skeleton's bind pose [`zz_bone`: bone offset = inverse bind world]: a
//!   vertex is the weighted sum of `animated world x inverse bind world` over
//!   its bones, which is the vertex itself in the bind pose.
//! - Face and hair hang off the head bone; the hat, mask and back item off
//!   skeleton dummies; weapons off the hand and shield dummies their own ZSC
//!   parts name. A part's own bone/dummy tag wins over the slot's default, and
//!   a bone wins over a dummy [`CCharPART::Load`, `Load_ZMODEL`]. Avatar parts
//!   ignore their ZSC position, rotation and scale.
//! - The motion is `TYPE_MOTION[action][weapon motion type]` (LIST_WEAPON
//!   column 34) -> a FILE_MOTION row (male file, female file); no row falls back
//!   to bare hands, no female file to the male one [`CObjAVT::Get_MOTION`,
//!   `CMotionLIST::IDX_GetMOTION`].
//! - A ZMO channel replaces its bone's local position or rotation; bones
//!   without one keep the bind pose [`zz_motion::load`]. ZMD and ZMO
//!   translations are centimetres (ZZ_SCALE_IN); ZMS 7/8 positions are metres.
//!
//! Nothing here touches the GPU: `preview_gl` draws a [`Scene`] posed by
//! [`Scene::pose`], and [`render`] rasterises one on the CPU for the headless
//! `--preview` check.

use std::collections::HashMap;
use std::io::Cursor;
use std::sync::Arc;

use anyhow::{anyhow, Result};
use roselib::files::zmo::ChannelData;
use roselib::files::{ZMD, ZMO, ZMS};
use roselib::io::RoseFile;

use crate::assets::{normalize, Assets};
use crate::data::ItemCategory;
use crate::dds::{self, DecodedImage};
use crate::monsters::{file_name, WeaponTables, Zsc, ZscMaterial, ZscObject};
use crate::text::{self, Codec};

/// `BONE_IDX_HEAD`, `DUMMY_IDX_BACK`, `DUMMY_IDX_MOUSE`, `DUMMY_IDX_CAP`
/// (common/shared/datatype.h).
const BONE_HEAD: i16 = 4;
const DUMMY_BACK: i16 = 3;
const DUMMY_MOUTH: i16 = 4;
const DUMMY_CAP: i16 = 6;
/// `HELMET_HAIR_TYPE` and `WEAPON_MOTION_TYPE` (common/include/rose/io/stb.h).
const HELMET_HAIR_TYPE_COLUMN: usize = 33;
const WEAPON_MOTION_TYPE_COLUMN: usize = 34;
/// ZZ_SCALE_IN: skeletons and motions are in centimetres, meshes in metres.
const CENTIMETRES: f32 = 0.01;

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Sex {
    Male,
    Female,
}

impl Sex {
    fn skeleton(self) -> &'static str {
        match self {
            Sex::Male => "3DDATA/AVATAR/MALE.ZMD",
            Sex::Female => "3DDATA/AVATAR/FEMALE.ZMD",
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug, PartialOrd, Ord)]
pub enum Slot {
    Face,
    Hair,
    Cap,
    Body,
    Arms,
    Foot,
    FaceItem,
    Back,
    WeaponR,
    WeaponL,
}

impl Slot {
    const ALL: [Slot; 10] = [
        Slot::Face,
        Slot::Hair,
        Slot::Cap,
        Slot::Body,
        Slot::Arms,
        Slot::Foot,
        Slot::FaceItem,
        Slot::Back,
        Slot::WeaponR,
        Slot::WeaponL,
    ];

    /// The slot an item category is worn in, if the preview can show it.
    pub fn for_category(category: ItemCategory) -> Option<Slot> {
        Some(match category {
            ItemCategory::Face => Slot::FaceItem,
            ItemCategory::Cap => Slot::Cap,
            ItemCategory::Body => Slot::Body,
            ItemCategory::Arms => Slot::Arms,
            ItemCategory::Foot => Slot::Foot,
            ItemCategory::Back => Slot::Back,
            ItemCategory::Weapon => Slot::WeaponR,
            ItemCategory::SubWpn => Slot::WeaponL,
            _ => return None,
        })
    }

    pub fn label(self) -> &'static str {
        match self {
            Slot::Face => "Face",
            Slot::Hair => "Hair",
            Slot::Cap => "Helmet",
            Slot::Body => "Body",
            Slot::Arms => "Gauntlets",
            Slot::Foot => "Boots",
            Slot::FaceItem => "Face item",
            Slot::Back => "Back",
            Slot::WeaponR => "Weapon",
            Slot::WeaponL => "Off-hand",
        }
    }

    fn table(self, sex: Sex, weapons: WeaponTables) -> &'static str {
        let male = sex == Sex::Male;
        match self {
            Slot::Face if male => "3DDATA/AVATAR/LIST_MFACE.ZSC",
            Slot::Face => "3DDATA/AVATAR/LIST_WFACE.ZSC",
            Slot::Hair if male => "3DDATA/AVATAR/LIST_MHAIR.ZSC",
            Slot::Hair => "3DDATA/AVATAR/LIST_WHAIR.ZSC",
            Slot::Cap if male => "3DDATA/AVATAR/LIST_MCAP.ZSC",
            Slot::Cap => "3DDATA/AVATAR/LIST_WCAP.ZSC",
            Slot::Body if male => "3DDATA/AVATAR/LIST_MBODY.ZSC",
            Slot::Body => "3DDATA/AVATAR/LIST_WBODY.ZSC",
            Slot::Arms if male => "3DDATA/AVATAR/LIST_MARMS.ZSC",
            Slot::Arms => "3DDATA/AVATAR/LIST_WARMS.ZSC",
            Slot::Foot if male => "3DDATA/AVATAR/LIST_MFOOT.ZSC",
            Slot::Foot => "3DDATA/AVATAR/LIST_WFOOT.ZSC",
            // One table for both sexes.
            Slot::FaceItem => "3DDATA/AVATAR/LIST_FACEIEM.ZSC",
            Slot::Back => "3DDATA/AVATAR/LIST_BACK.ZSC",
            Slot::WeaponR => weapons.weapon(!male),
            Slot::WeaponL => weapons.subweapon(!male),
        }
    }

    /// (bone, dummy) a part links to when its ZSC entry names neither.
    /// Weapons have no default: their parts name a hand or shield dummy.
    fn default_link(self) -> (i16, i16) {
        match self {
            Slot::Face | Slot::Hair => (BONE_HEAD, -1),
            Slot::Cap => (-1, DUMMY_CAP),
            Slot::FaceItem => (-1, DUMMY_MOUTH),
            Slot::Back => (-1, DUMMY_BACK),
            _ => (-1, -1),
        }
    }
}

/// What the character is doing.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Action {
    BindPose,
    Idle,
    Battle,
    Walk,
    Run,
    Attack,
    Sit,
}

impl Action {
    pub const ALL: [Action; 7] = [
        Action::Idle,
        Action::Battle,
        Action::Walk,
        Action::Run,
        Action::Attack,
        Action::Sit,
        Action::BindPose,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Action::BindPose => "Bind pose",
            Action::Idle => "Idle",
            Action::Battle => "Battle stance",
            Action::Walk => "Walk",
            Action::Run => "Run",
            Action::Attack => "Attack",
            Action::Sit => "Sitting",
        }
    }

    pub fn parse(text: &str) -> Option<Action> {
        Action::ALL.into_iter().find(|a| {
            a.label()
                .split(' ')
                .next()
                .unwrap()
                .eq_ignore_ascii_case(text)
        })
    }

    /// `AVT_ANI_*` (common/shared/datatype.h); `None` for the bind pose.
    fn index(self) -> Option<usize> {
        match self {
            Action::BindPose => None,
            Action::Idle => Some(0),
            Action::Walk => Some(2),
            Action::Run => Some(3),
            Action::Sit => Some(5),
            Action::Battle => Some(7),
            Action::Attack => Some(8),
        }
    }
}

/// Whether `assets` holds avatar models at all: the tester package ships
/// tables and icons only, and gets no preview rather than a list of misses.
pub fn has_avatar_models(assets: &Assets) -> bool {
    assets.exists(Slot::Body.table(Sex::Male, WeaponTables::Shared))
}

/// Everything the preview reads once per data folder: the part tables, the two
/// skeletons, each hat's hair type, each weapon's motion type and the motion tables.
pub struct AvatarModels {
    tables: HashMap<&'static str, Result<Zsc, String>>,
    /// Shared weapon tables, or China Rose's per-sex ones.
    weapons: WeaponTables,
    rigs: HashMap<Sex, Result<Arc<Rig>, String>>,
    hair_types: Vec<i32>,
    weapon_motion_types: Vec<i32>,
    /// TYPE_MOTION: [action][weapon motion type] -> FILE_MOTION row.
    motion_types: Vec<Vec<i32>>,
    /// FILE_MOTION: (male, female) file per row.
    motion_files: Vec<(String, String)>,
    /// Problems that apply to every preview from this folder.
    pub warnings: Vec<String>,
}

impl AvatarModels {
    pub fn load(assets: &Assets, codec: Codec) -> Self {
        let mut tables = HashMap::new();
        let mut rigs = HashMap::new();
        let weapons = WeaponTables::detect(assets);
        for sex in [Sex::Male, Sex::Female] {
            for slot in Slot::ALL {
                let path = slot.table(sex, weapons);
                tables.entry(path).or_insert_with(|| {
                    assets
                        .read(path)
                        .and_then(|bytes| Zsc::parse(&bytes))
                        .map_err(|e| format!("{path}: {e:#}"))
                });
            }
            rigs.insert(sex, load_rig(assets, sex.skeleton()));
        }
        let mut warnings = Vec::new();
        // The reader keeps the root column, so table column N is cell N + 1.
        let numbers = |path: &str| -> Result<Vec<Vec<i32>>> {
            let rows = text::read_stb(&assets.read(path)?, codec)?;
            Ok(rows
                .iter()
                .map(|row| {
                    row.iter()
                        .skip(1)
                        .map(|cell| cell.trim().parse().unwrap_or(0))
                        .collect()
                })
                .collect())
        };
        let mut column = |path: &str, column: usize, consequence: &str| -> Vec<i32> {
            match numbers(path) {
                Ok(rows) => rows
                    .iter()
                    .map(|row| row.get(column).copied().unwrap_or(0))
                    .collect(),
                Err(e) => {
                    warnings.push(format!("{consequence}: {path}: {e:#}"));
                    Vec::new()
                }
            }
        };
        let hair_types = column(
            "3DDATA/STB/LIST_CAP.STB",
            HELMET_HAIR_TYPE_COLUMN,
            "Hats keep the full haircut",
        );
        let weapon_motion_types = column(
            "3DDATA/STB/LIST_WEAPON.STB",
            WEAPON_MOTION_TYPE_COLUMN,
            "Weapons get bare-hand motions",
        );
        let motions = numbers("3DDATA/STB/TYPE_MOTION.STB").and_then(|types| {
            let rows = text::read_stb(&assets.read("3DDATA/STB/FILE_MOTION.STB")?, codec)?;
            let cell = |row: &Vec<String>, i: usize| row.get(i).cloned().unwrap_or_default();
            let files = rows
                .iter()
                .map(|row| (cell(row, 1), cell(row, 2)))
                .collect();
            Ok((types, files))
        });
        let (motion_types, motion_files) = motions.unwrap_or_else(|e| {
            warnings.push(format!(
                "No animation, bind pose only: motion tables: {e:#}"
            ));
            (Vec::new(), Vec::new())
        });
        Self {
            tables,
            weapons,
            rigs,
            hair_types,
            weapon_motion_types,
            motion_types,
            motion_files,
            warnings,
        }
    }

    fn table(&self, slot: Slot, sex: Sex) -> Result<&Zsc, &str> {
        match &self.tables[slot.table(sex, self.weapons)] {
            Ok(zsc) => Ok(zsc),
            Err(e) => Err(e),
        }
    }

    /// The first model in `slot` from `start`, stepping by `step`, that has any parts.
    fn first_model(&self, slot: Slot, sex: Sex, start: usize, step: usize) -> i32 {
        self.table(slot, sex)
            .ok()
            .and_then(|zsc| {
                (start..zsc.objects.len())
                    .step_by(step)
                    .find(|&i| !zsc.objects[i].parts.is_empty())
            })
            .map_or(0, |i| i as i32)
    }

    /// A character of `sex` wearing `worn` (a later item replaces an earlier one
    /// in the same slot) and bare elsewhere. `focus` is the item being looked
    /// at: the camera closes in on it, and only its empty model is a problem.
    pub fn outfit(
        &self,
        sex: Sex,
        worn: &[(ItemCategory, i32)],
        focus: Option<ItemCategory>,
    ) -> Outfit {
        // Character creation (SCRIPTS/SYSTEMFUNC.LUA) offers faces from 1 and
        // hair 0, 5, 10... (each followed by its cuts for the hat types); take
        // the first of each that has a model.
        let mut parts = vec![
            (Slot::Face, self.first_model(Slot::Face, sex, 1, 1)),
            (Slot::Hair, self.first_model(Slot::Hair, sex, 0, 5)),
            (Slot::Body, 0),
            (Slot::Arms, 0),
            (Slot::Foot, 0),
        ];
        for &(category, id) in worn {
            let Some(slot) = Slot::for_category(category) else {
                continue;
            };
            match parts.iter_mut().find(|(s, _)| *s == slot) {
                Some(part) => part.1 = id,
                None => parts.push((slot, id)),
            }
        }
        let mut outfit = Outfit {
            sex,
            focus: focus.and_then(Slot::for_category),
            parts,
        };
        if let Some(cap) = outfit.worn(Slot::Cap) {
            let hair_type = self.hair_types.get(cap as usize).copied().unwrap_or(0);
            outfit.parts[1].1 += hair_type.max(0);
        }
        outfit
    }

    pub fn build(&self, assets: &Assets, outfit: &Outfit) -> Scene {
        let rig = self.rigs[&outfit.sex].clone();
        let mut builder = SceneBuilder::new(assets, rig.as_ref().ok().cloned());
        if let Err(e) = rig {
            builder.note(format!(
                "Skeleton unavailable, no animation and face, hats and weapons float: {e}"
            ));
        }
        for &(slot, index) in &outfit.parts {
            let label = slot.label();
            let file = file_name(slot.table(outfit.sex, self.weapons));
            let zsc = match self.table(slot, outfit.sex) {
                Ok(zsc) => zsc,
                Err(e) => {
                    builder.note(format!("{label}: {e}"));
                    continue;
                }
            };
            let Some(object) = builder.object(zsc, file, index, label) else {
                continue;
            };
            let focus = Some(slot) == outfit.focus;
            if object.parts.is_empty() && focus {
                builder.note(format!("{label}: model {index} of {file} is empty"));
            }
            let weapon = matches!(slot, Slot::WeaponR | Slot::WeaponL);
            builder.add(zsc, file, object, label, slot.default_link(), weapon, focus);
        }
        builder.finish()
    }

    /// The motion `outfit` plays for `action`: `None` for the bind pose.
    pub fn motion(
        &self,
        assets: &Assets,
        outfit: &Outfit,
        action: Action,
    ) -> Result<Option<Animation>, String> {
        let Some(row) = action.index() else {
            return Ok(None);
        };
        let label = action.label();
        if self.motion_types.is_empty() {
            return Err(format!("{label}: no motion tables"));
        }
        let weapon = outfit.worn(Slot::WeaponR).unwrap_or(0);
        let weapon_type = self
            .weapon_motion_types
            .get(weapon as usize)
            .copied()
            .unwrap_or(0)
            .max(0) as usize;
        let file_row = |weapon_type: usize| {
            self.motion_types
                .get(row)
                .and_then(|r| r.get(weapon_type))
                .copied()
                .unwrap_or(0)
        };
        let mut file = file_row(weapon_type);
        if file == 0 {
            file = file_row(0);
        }
        let Some((male, female)) = self.motion_files.get(file.max(0) as usize) else {
            return Err(format!("{label}: FILE_MOTION has no row {file}"));
        };
        let path = if outfit.sex == Sex::Female && !female.trim().is_empty() {
            female
        } else {
            male
        };
        if path.trim().is_empty() {
            return Err(format!("{label}: FILE_MOTION row {file} names no file"));
        }
        Animation::load(assets, path)
            .map(Some)
            .map_err(|e| format!("{label}: {path}: {e:#}"))
    }
}

/// Which model each slot shows.
#[derive(Clone, PartialEq, Debug)]
pub struct Outfit {
    pub sex: Sex,
    /// The slot of the item being looked at.
    focus: Option<Slot>,
    parts: Vec<(Slot, i32)>,
}

impl Outfit {
    fn worn(&self, slot: Slot) -> Option<i32> {
        self.parts
            .iter()
            .find(|(s, _)| *s == slot)
            .map(|&(_, id)| id)
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Vertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub uv: [f32; 2],
}

/// What moves a mesh.
enum Binding {
    /// Drawn as stored.
    Fixed,
    /// Stored in the bone's or dummy's space.
    Bone(usize),
    Dummy(usize),
    /// Stored in the bind pose; per vertex up to four (skeleton bone, weight).
    Skin(Vec<[(u16, f32); 4]>),
}

pub struct SceneMesh {
    /// Left out of the framing.
    weapon: bool,
    /// As stored in the file: see [`Binding`].
    source: Vec<Vertex>,
    binding: Binding,
    pub indices: Vec<u32>,
    /// Index into [`Scene::textures`]; `None` draws flat grey.
    pub texture: Option<usize>,
    pub material: ZscMaterial,
}

impl SceneMesh {
    /// Alpha-tested parts (hair, fringes) draw as cut-outs with depth; the
    /// engine blends them too, but blending without sorting only adds artefacts.
    /// 0..1, `None` for no test.
    pub fn alpha_cutoff(&self) -> Option<f32> {
        let m = &self.material;
        (m.uses_texture_alpha() && m.alpha_test)
            .then(|| f32::from(m.alpha_ref.clamp(0, 255)) / 255.0)
    }

    /// Texture-alpha parts without a test, drawn after the rest, blended.
    pub fn blended(&self) -> bool {
        self.material.uses_texture_alpha() && !self.material.alpha_test
    }

    pub fn vertex_count(&self) -> usize {
        self.source.len()
    }

    fn posed(&self, joints: Option<&Joints>) -> Vec<Vertex> {
        let moved = |m: &Mat4, v: &Vertex| Vertex {
            position: m.point(v.position),
            normal: normalized(m.direction(v.normal)),
            uv: v.uv,
        };
        let Some(joints) = joints else {
            return self.source.clone();
        };
        match &self.binding {
            Binding::Fixed => self.source.clone(),
            Binding::Bone(i) => self
                .source
                .iter()
                .map(|v| moved(&joints.bones[*i], v))
                .collect(),
            Binding::Dummy(i) => self
                .source
                .iter()
                .map(|v| moved(&joints.dummies[*i], v))
                .collect(),
            Binding::Skin(weights) => self
                .source
                .iter()
                .zip(weights)
                .map(|(v, influences)| {
                    let mut blend = Mat4([0.0; 16]);
                    for &(bone, weight) in influences.iter().filter(|(_, w)| *w > 0.0) {
                        blend.add_scaled(&joints.skin[bone as usize], weight);
                    }
                    moved(&blend, v)
                })
                .collect(),
        }
    }
}

/// Posed vertices, one list per [`Scene::meshes`] entry.
pub type Posed = Vec<Vec<Vertex>>;

#[derive(Default)]
pub struct Scene {
    pub meshes: Vec<SceneMesh>,
    pub textures: Vec<DecodedImage>,
    rig: Option<Arc<Rig>>,
    /// The meshes of the item being looked at.
    focus_meshes: Vec<usize>,
    /// Missing files and other problems, for the panel to list.
    pub notes: Vec<String>,
}

impl Scene {
    /// Every mesh `seconds` into `animation`, or in the bind pose without one.
    pub fn pose(&self, animation: Option<&Animation>, seconds: f32) -> Posed {
        let joints = self.rig.as_ref().map(|rig| rig.joints(animation, seconds));
        self.meshes
            .iter()
            .map(|mesh| mesh.posed(joints.as_ref()))
            .collect()
    }

    /// Bounds over a few poses through `animation`, so the camera holds still
    /// while it plays. Weapons are left out: a two-hand swing sweeps 3 m and
    /// would shrink the figure to a third.
    pub fn framing(&self, animation: Option<&Animation>) -> Framing {
        const SAMPLES: usize = 8;
        let duration = animation.map_or(0.0, Animation::duration);
        let poses: Vec<Posed> = (0..SAMPLES)
            .map(|i| self.pose(animation, duration * i as f32 / SAMPLES as f32))
            .collect();
        let bounds = |meshes: &[usize]| {
            let mut min = [f32::MAX; 3];
            let mut max = [f32::MIN; 3];
            for posed in &poses {
                for vertex in meshes.iter().flat_map(|&m| &posed[m]) {
                    for axis in 0..3 {
                        min[axis] = min[axis].min(vertex.position[axis]);
                        max[axis] = max[axis].max(vertex.position[axis]);
                    }
                }
            }
            (min[0] <= max[0]).then_some((min, max))
        };
        let body: Vec<usize> = (0..self.meshes.len())
            .filter(|&m| !self.meshes[m].weapon)
            .collect();
        let all: Vec<usize> = (0..self.meshes.len()).collect();
        let (min, max) = bounds(&body)
            .or_else(|| bounds(&all))
            .unwrap_or(([-0.5, -0.5, 0.0], [0.5, 0.5, 1.8]));
        let (lo, hi) = bounds(&self.focus_meshes).unwrap_or((min, max));
        Framing {
            min,
            max,
            focus: [0, 1, 2].map(|a| (lo[a] + hi[a]) * 0.5),
        }
    }

    /// Metres from the lowest to the highest point of everything but the
    /// weapons, `seconds` into `animation`, before any table scale.
    pub fn height(&self, animation: Option<&Animation>, seconds: f32) -> f32 {
        let posed = self.pose(animation, seconds);
        let (low, high) = self
            .meshes
            .iter()
            .zip(&posed)
            .filter(|(mesh, _)| !mesh.weapon)
            .flat_map(|(_, vertices)| vertices)
            .fold((f32::MAX, f32::MIN), |(low, high), v| {
                (low.min(v.position[2]), high.max(v.position[2]))
            });
        (high - low).max(0.0)
    }

    pub fn triangles(&self) -> usize {
        self.meshes.iter().map(|m| m.indices.len() / 3).sum()
    }
}

/// What the camera keeps in view.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Framing {
    pub min: [f32; 3],
    pub max: [f32; 3],
    /// Centre of the item being looked at, which the camera closes in on as it zooms.
    pub focus: [f32; 3],
}

/// Puts ZSC models into a [`Scene`] the way `CCharPART::Load_ZMODEL` links
/// them: avatar parts and monster parts follow the same rules.
pub(crate) struct SceneBuilder<'a> {
    assets: &'a Assets,
    scene: Scene,
    /// Each texture is decoded once, by normalised path.
    textures: HashMap<String, Option<usize>>,
}

impl<'a> SceneBuilder<'a> {
    pub(crate) fn new(assets: &'a Assets, rig: Option<Arc<Rig>>) -> Self {
        Self {
            assets,
            scene: Scene {
                rig,
                ..Scene::default()
            },
            textures: HashMap::new(),
        }
    }

    pub(crate) fn note(&mut self, note: String) {
        self.scene.notes.push(note);
    }

    /// Model `index` of `zsc` (file name `file`), or a note that it is out of range.
    pub(crate) fn object<'z>(
        &mut self,
        zsc: &'z Zsc,
        file: &str,
        index: i32,
        label: &str,
    ) -> Option<&'z ZscObject> {
        let object = usize::try_from(index).ok().and_then(|i| zsc.objects.get(i));
        if object.is_none() {
            self.note(format!(
                "{label}: model {index} is outside {file} ({} models)",
                zsc.objects.len()
            ));
        }
        object
    }

    /// Every part of `object`. `default_link` is the (bone, dummy) a part
    /// links to when it names neither; `weapon` keeps it out of the framing;
    /// `focus` marks the item being looked at.
    #[allow(clippy::too_many_arguments)]
    pub(crate) fn add(
        &mut self,
        zsc: &Zsc,
        file: &str,
        object: &ZscObject,
        label: &str,
        default_link: (i16, i16),
        weapon: bool,
        focus: bool,
    ) {
        let rig = self.scene.rig.clone();
        for part in &object.parts {
            let mesh_path = usize::try_from(part.mesh)
                .ok()
                .and_then(|i| zsc.meshes.get(i));
            let Some(mesh_path) = mesh_path else {
                self.note(format!("{label}: mesh {} is outside {file}", part.mesh));
                continue;
            };
            let mesh = match load_mesh(self.assets, mesh_path) {
                Ok(mesh) => mesh,
                Err(e) => {
                    self.note(format!("{label}: {mesh_path}: {e:#}"));
                    continue;
                }
            };
            let (default_bone, default_dummy) = default_link;
            let bone = if part.bone >= 0 {
                part.bone
            } else {
                default_bone
            };
            let dummy = if part.dummy >= 0 {
                part.dummy
            } else {
                default_dummy
            };
            let binding = match &rig {
                _ if bone < 0 && dummy < 0 => skin_binding(&mesh, rig.as_deref()),
                None => Binding::Fixed,
                Some(rig) if bone >= 0 && (bone as usize) < rig.bone_count() => {
                    Binding::Bone(bone as usize)
                }
                Some(rig) if bone < 0 && (dummy as usize) < rig.dummies.len() => {
                    Binding::Dummy(dummy as usize)
                }
                Some(_) => {
                    self.note(format!(
                        "{label}: the skeleton has no bone {bone} / dummy {dummy}"
                    ));
                    Binding::Fixed
                }
            };
            let material = usize::try_from(part.material).ok();
            let flags = material
                .and_then(|i| zsc.material_flags.get(i))
                .copied()
                .unwrap_or_default();
            let texture = match material.and_then(|i| zsc.materials.get(i)) {
                Some(path) => self.texture(path, label),
                None => {
                    self.note(format!(
                        "{label}: material {} is outside {file}",
                        part.material
                    ));
                    None
                }
            };
            if focus {
                self.scene.focus_meshes.push(self.scene.meshes.len());
            }
            self.scene
                .meshes
                .push(scene_mesh(weapon, &mesh, binding, texture, flags));
        }
    }

    fn texture(&mut self, path: &str, label: &str) -> Option<usize> {
        let key = normalize(path);
        if let Some(&index) = self.textures.get(&key) {
            return index;
        }
        let index = match self.assets.read(&key).and_then(|b| dds::decode(&b)) {
            Ok(image) => {
                self.scene.textures.push(image);
                Some(self.scene.textures.len() - 1)
            }
            Err(e) => {
                self.note(format!("{label}: {path}: {e:#}"));
                None
            }
        };
        self.textures.insert(key, index);
        index
    }

    pub(crate) fn finish(self) -> Scene {
        self.scene
    }
}

/// A skeleton file, for every part and motion that hangs off it.
pub(crate) fn load_rig(assets: &Assets, path: &str) -> Result<Arc<Rig>, String> {
    assets
        .read(&normalize(path))
        .and_then(|bytes| {
            let mut zmd = ZMD::new();
            zmd.read(&mut Cursor::new(bytes))
                .map_err(|e| anyhow!("{e}"))?;
            Ok(Arc::new(Rig::from_zmd(&zmd)))
        })
        .map_err(|e| format!("{path}: {e:#}"))
}

fn load_mesh(assets: &Assets, path: &str) -> Result<ZMS> {
    let bytes = assets.read(&normalize(path))?;
    let mut mesh = ZMS::new();
    mesh.read(&mut Cursor::new(bytes))
        .map_err(|e| anyhow!("{e}"))?;
    Ok(mesh)
}

/// A mesh with no bone or dummy link: skinned if it has weights and there is
/// a skeleton to skin it with, drawn as stored otherwise.
fn skin_binding(mesh: &ZMS, rig: Option<&Rig>) -> Binding {
    let Some(rig) = rig.filter(|_| mesh.bones_enabled() && !mesh.bones.is_empty()) else {
        return Binding::Fixed;
    };
    // A vertex names slots of the mesh's own bone list, which names skeleton bones.
    let skeleton_bone = |slot: i16| {
        usize::try_from(slot)
            .ok()
            .and_then(|s| mesh.bones.get(s))
            .and_then(|&b| usize::try_from(b).ok())
            .filter(|&b| b < rig.bone_count())
    };
    Binding::Skin(
        mesh.vertices
            .iter()
            .map(|v| {
                let slots = [
                    v.bone_indices.x,
                    v.bone_indices.y,
                    v.bone_indices.z,
                    v.bone_indices.w,
                ];
                let weights = [
                    v.bone_weights.x,
                    v.bone_weights.y,
                    v.bone_weights.z,
                    v.bone_weights.w,
                ];
                let mut influences = [(0u16, 0.0f32); 4];
                for k in 0..4 {
                    if let Some(bone) = skeleton_bone(slots[k]).filter(|_| weights[k] > 0.0) {
                        influences[k] = (bone as u16, weights[k]);
                    }
                }
                let total: f32 = influences.iter().map(|(_, w)| w).sum();
                if total <= 1e-6 {
                    // Unweighted: hold it to the root, as the engine forces weight 1 on bone 0.
                    influences[0] = (0, 1.0);
                } else {
                    influences.iter_mut().for_each(|(_, w)| *w /= total);
                }
                influences
            })
            .collect(),
    )
}

fn scene_mesh(
    weapon: bool,
    mesh: &ZMS,
    binding: Binding,
    texture: Option<usize>,
    material: ZscMaterial,
) -> SceneMesh {
    let count = mesh.vertices.len();
    let indices: Vec<u32> = mesh
        .indices
        .iter()
        // Counts are u16 in the engine; roselib reads them as i16.
        .flat_map(|t| [t.x as u16 as u32, t.y as u16 as u32, t.z as u16 as u32])
        .collect();
    let indices: Vec<u32> = indices
        .chunks_exact(3)
        .filter(|t| t.iter().all(|&i| (i as usize) < count))
        .flatten()
        .copied()
        .collect();
    let mut normals: Vec<[f32; 3]> = mesh
        .vertices
        .iter()
        .map(|v| [v.normal.x, v.normal.y, v.normal.z])
        .collect();
    if !mesh.normals_enabled() {
        normals = smooth_normals(mesh, &indices);
    }
    let source = mesh
        .vertices
        .iter()
        .zip(normals)
        .map(|(v, n)| Vertex {
            position: [v.position.x, v.position.y, v.position.z],
            normal: normalized(n),
            uv: [v.uv1.x, v.uv1.y],
        })
        .collect();
    SceneMesh {
        weapon,
        source,
        binding,
        indices,
        texture,
        material,
    }
}

fn smooth_normals(mesh: &ZMS, indices: &[u32]) -> Vec<[f32; 3]> {
    let pos = |i: u32| {
        let p = &mesh.vertices[i as usize].position;
        [p.x, p.y, p.z]
    };
    let mut normals = vec![[0.0; 3]; mesh.vertices.len()];
    for t in indices.chunks_exact(3) {
        let n = cross(sub(pos(t[1]), pos(t[0])), sub(pos(t[2]), pos(t[0])));
        for &i in t {
            for axis in 0..3 {
                normals[i as usize][axis] += n[axis];
            }
        }
    }
    normals.into_iter().map(normalized).collect()
}

/// A skeleton: its bones' bind pose and its dummies.
pub struct Rig {
    parents: Vec<Option<usize>>,
    /// Bone indices, parents before children.
    order: Vec<usize>,
    /// Local (translation in metres, rotation w x y z) per bone.
    bind: Vec<([f32; 3], [f32; 4])>,
    inverse_bind: Vec<Mat4>,
    /// (parent bone, local transform) per dummy.
    dummies: Vec<(Option<usize>, Mat4)>,
}

/// World transforms of one pose.
struct Joints {
    bones: Vec<Mat4>,
    dummies: Vec<Mat4>,
    /// Bind-pose space to posed space, per bone.
    skin: Vec<Mat4>,
}

impl Rig {
    fn from_zmd(zmd: &ZMD) -> Self {
        let count = zmd.bones.len();
        let local = |bone: &roselib::files::zmd::Bone| {
            let (p, q) = (&bone.position, &bone.rotation);
            (
                [p.x, p.y, p.z].map(|c| c * CENTIMETRES),
                [q.w, q.x, q.y, q.z],
            )
        };
        let bind: Vec<_> = zmd.bones.iter().map(local).collect();
        let mut parents: Vec<Option<usize>> = zmd
            .bones
            .iter()
            .enumerate()
            .map(|(i, bone)| {
                usize::try_from(bone.parent)
                    .ok()
                    .filter(|&p| p < count && p != i)
            })
            .collect();
        // Parents usually come first, but nothing promises it: order until
        // nothing changes, then let anything left (a cycle) stand on its own.
        let mut order = Vec::with_capacity(count);
        let mut placed = vec![false; count];
        loop {
            let before = order.len();
            for i in 0..count {
                if !placed[i] && parents[i].is_none_or(|p| placed[p]) {
                    placed[i] = true;
                    order.push(i);
                }
            }
            if order.len() == before {
                break;
            }
        }
        for i in 0..count {
            if !placed[i] {
                parents[i] = None;
                order.push(i);
            }
        }
        let mut rig = Self {
            parents,
            order,
            bind,
            inverse_bind: Vec::new(),
            dummies: Vec::new(),
        };
        let bind_locals: Vec<Mat4> = rig.bind.iter().map(|&(t, q)| transform(t, q)).collect();
        rig.inverse_bind = rig
            .world(&bind_locals)
            .iter()
            .map(Mat4::inverse_rigid)
            .collect();
        rig.dummies = zmd
            .dummy_bones
            .iter()
            .map(|dummy| {
                let parent = usize::try_from(dummy.parent).ok().filter(|&p| p < count);
                let (t, q) = local(dummy);
                (parent, transform(t, q))
            })
            .collect();
        // zz_skeleton appends a dummy at the root ("_p1") that parts may name.
        rig.dummies.push(((count > 0).then_some(0), IDENTITY));
        rig
    }

    fn bone_count(&self) -> usize {
        self.parents.len()
    }

    fn world(&self, locals: &[Mat4]) -> Vec<Mat4> {
        let mut world = locals.to_vec();
        for &i in &self.order {
            if let Some(p) = self.parents[i] {
                world[i] = world[p].mul(&locals[i]);
            }
        }
        world
    }

    fn joints(&self, animation: Option<&Animation>, seconds: f32) -> Joints {
        let locals: Vec<Mat4> = (0..self.bone_count())
            .map(|i| {
                let (mut t, mut q) = self.bind[i];
                if let Some(animation) = animation {
                    t = animation.position(i, seconds).unwrap_or(t);
                    q = animation.rotation(i, seconds).unwrap_or(q);
                }
                transform(t, q)
            })
            .collect();
        let bones = self.world(&locals);
        let skin = bones
            .iter()
            .zip(&self.inverse_bind)
            .map(|(b, inverse)| b.mul(inverse))
            .collect();
        let dummies = self
            .dummies
            .iter()
            .map(|(parent, local)| match parent {
                Some(p) => bones[*p].mul(local),
                None => *local,
            })
            .collect();
        Joints {
            bones,
            dummies,
            skin,
        }
    }
}

/// A skeletal motion (ZMO).
pub struct Animation {
    pub path: String,
    fps: f32,
    frames: usize,
    positions: HashMap<usize, Vec<[f32; 3]>>,
    /// w x y z.
    rotations: HashMap<usize, Vec<[f32; 4]>>,
}

impl Animation {
    pub(crate) fn load(assets: &Assets, path: &str) -> Result<Self> {
        let bytes = assets.read(&normalize(path))?;
        let mut zmo = ZMO::new();
        zmo.read(&mut Cursor::new(bytes))
            .map_err(|e| anyhow!("{e}"))?;
        let mut positions = HashMap::new();
        let mut rotations = HashMap::new();
        for channel in &zmo.channels {
            let bone = channel.index as usize;
            match &channel.frames {
                ChannelData::Position(frames) => {
                    let frames = frames
                        .iter()
                        .map(|p| [p.x, p.y, p.z].map(|c| c * CENTIMETRES));
                    positions.insert(bone, frames.collect());
                }
                ChannelData::Rotation(frames) => {
                    // The file stores w x y z; roselib's read_quaternion fills
                    // x y z w in file order, so its x holds w.
                    let frames = frames.iter().map(|q| [q.x, q.y, q.z, q.w]);
                    rotations.insert(bone, frames.collect());
                }
                _ => {}
            }
        }
        if zmo.frames == 0 {
            anyhow::bail!("the motion has no frames");
        }
        Ok(Self {
            path: path.to_string(),
            fps: zmo.fps.max(1) as f32,
            frames: zmo.frames as usize,
            positions,
            rotations,
        })
    }

    pub fn duration(&self) -> f32 {
        self.frames as f32 / self.fps
    }

    /// (frame, next frame, blend) at `seconds`, looping.
    fn frame(&self, seconds: f32) -> (usize, usize, f32) {
        let f = (seconds.max(0.0) * self.fps) % self.frames as f32;
        let a = (f.floor() as usize).min(self.frames - 1);
        (a, (a + 1) % self.frames, f.fract())
    }

    fn position(&self, bone: usize, seconds: f32) -> Option<[f32; 3]> {
        let frames = self.positions.get(&bone)?;
        let (a, b, t) = self.frame(seconds);
        let (p, q) = (frames.get(a)?, frames.get(b).unwrap_or(frames.get(a)?));
        Some([0, 1, 2].map(|k| p[k] + (q[k] - p[k]) * t))
    }

    fn rotation(&self, bone: usize, seconds: f32) -> Option<[f32; 4]> {
        let frames = self.rotations.get(&bone)?;
        let (a, b, t) = self.frame(seconds);
        let (p, mut q) = (*frames.get(a)?, *frames.get(b).unwrap_or(frames.get(a)?));
        // Take the short way round.
        if (0..4).map(|k| p[k] * q[k]).sum::<f32>() < 0.0 {
            q = q.map(|c| -c);
        }
        Some([0, 1, 2, 3].map(|k| p[k] + (q[k] - p[k]) * t))
    }
}

/// Translation then rotation (w x y z): a bone's local transform.
fn transform(t: [f32; 3], q: [f32; 4]) -> Mat4 {
    Mat4::translation(t).mul(&Mat4::rotation(q))
}

/// Orbit camera around a [`Framing`].
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Camera {
    /// Radians around the vertical axis; 0 looks at the character's face.
    pub yaw: f32,
    pub pitch: f32,
    /// 1 frames the whole figure; smaller is closer.
    pub zoom: f32,
}

impl Default for Camera {
    fn default() -> Self {
        Self {
            yaw: 0.0,
            pitch: 0.12,
            zoom: 1.0,
        }
    }
}

const FIELD_OF_VIEW: f32 = 0.6;
/// Closest zoom the pane allows.
pub const MIN_ZOOM: f32 = 0.2;

impl Camera {
    /// (view-projection, light direction toward the light) for a viewport of `aspect` = w / h.
    pub fn matrices(&self, framing: &Framing, aspect: f32) -> (Mat4, [f32; 3]) {
        let Framing { min, max, focus } = *framing;
        // Whole figure at zoom 1, the item itself by the closest zoom.
        let t = ((1.0 - self.zoom) / (1.0 - MIN_ZOOM)).clamp(0.0, 1.0);
        let center = [0, 1, 2].map(|a| {
            let middle = (min[a] + max[a]) * 0.5;
            middle + (focus[a] - middle) * t
        });
        let radius = (0..3)
            .map(|a| (max[a] - min[a]) * 0.5)
            .map(|h| h * h)
            .sum::<f32>()
            .sqrt()
            .max(0.05);
        // Fit the box: its footprint at any heading across, its height up.
        // A sphere would be set by the T-pose's arm span and leave the figure small.
        let across = 0.5 * (max[0] - min[0]).hypot(max[1] - min[1]);
        let up = 0.5 * (max[2] - min[2]);
        let tan_up = (FIELD_OF_VIEW * 0.5).tan();
        let fit = (across / (tan_up * aspect)).max(up / tan_up) * 1.1 + across;
        let distance = fit.max(0.1) * self.zoom;
        // The avatar faces -Y in its own space (zz_motion: "negative-y axis is front").
        let yaw = self.yaw - std::f32::consts::FRAC_PI_2;
        let direction = [
            self.pitch.cos() * yaw.cos(),
            self.pitch.cos() * yaw.sin(),
            self.pitch.sin(),
        ];
        let eye = [0, 1, 2].map(|a| center[a] + direction[a] * distance);
        let view = Mat4::look_at(eye, center, [0.0, 0.0, 1.0]);
        let near = (distance - radius * 1.5).max(distance * 0.02);
        // The orbit centre can sit a radius off the middle when zoomed onto the item.
        let far = distance + radius * 2.5;
        let projection = Mat4::perspective(FIELD_OF_VIEW, aspect, near, far);
        // Key light from above, front and a little to the side of the camera.
        let side = [-direction[1], direction[0], 0.0];
        let light = normalized([0, 1, 2].map(|a| direction[a] + side[a] * 0.4));
        let light = normalized([light[0], light[1], light[2] + 0.6]);
        (projection.mul(&view), light)
    }
}

/// Rasterise `posed` on the CPU into RGBA8 the way the GL pane draws it.
/// Used by the headless `--preview` check, never by the window.
pub fn render(
    scene: &Scene,
    posed: &Posed,
    framing: &Framing,
    camera: &Camera,
    width: usize,
    height: usize,
) -> Vec<u8> {
    let (view_projection, light) = camera.matrices(framing, width as f32 / height as f32);
    let mut color = vec![0u8; width * height * 4];
    for pixel in color.chunks_exact_mut(4) {
        pixel.copy_from_slice(&BACKGROUND.map(|c| (c * 255.0) as u8));
    }
    let mut depth = vec![f32::MAX; width * height];
    for blended in [false, true] {
        for (mesh, vertices) in scene
            .meshes
            .iter()
            .zip(posed)
            .filter(|(m, _)| m.blended() == blended)
        {
            let texture = mesh.texture.map(|i| &scene.textures[i]);
            let clip: Vec<[f32; 4]> = vertices
                .iter()
                .map(|v| view_projection.project(v.position))
                .collect();
            for t in mesh.indices.chunks_exact(3) {
                let [a, b, c] = [t[0], t[1], t[2]].map(|i| i as usize);
                if [a, b, c].iter().any(|&i| clip[i][3] <= 1e-4) {
                    continue;
                }
                let screen = [a, b, c].map(|i| {
                    let [x, y, z, w] = clip[i];
                    [
                        (x / w + 1.0) * 0.5 * width as f32,
                        (1.0 - y / w) * 0.5 * height as f32,
                        z / w,
                        1.0 / w,
                    ]
                });
                let area = edge(screen[0], screen[1], screen[2]);
                if area.abs() < 1e-9 {
                    continue;
                }
                let lo = |axis: usize| screen.iter().map(|s| s[axis]).fold(f32::MAX, f32::min);
                let hi = |axis: usize| screen.iter().map(|s| s[axis]).fold(f32::MIN, f32::max);
                let x0 = lo(0).floor().max(0.0) as usize;
                let x1 = (hi(0).ceil().max(0.0) as usize).min(width);
                let y0 = lo(1).floor().max(0.0) as usize;
                let y1 = (hi(1).ceil().max(0.0) as usize).min(height);
                for y in y0..y1 {
                    for x in x0..x1 {
                        let p = [x as f32 + 0.5, y as f32 + 0.5, 0.0, 0.0];
                        let w = [
                            edge(screen[1], screen[2], p) / area,
                            edge(screen[2], screen[0], p) / area,
                            edge(screen[0], screen[1], p) / area,
                        ];
                        if w.iter().any(|&w| w < 0.0) {
                            continue;
                        }
                        let z = w[0] * screen[0][2] + w[1] * screen[1][2] + w[2] * screen[2][2];
                        let at = y * width + x;
                        if !(-1.0..=1.0).contains(&z) || z >= depth[at] {
                            continue;
                        }
                        // Perspective-correct attributes.
                        let inv = [0, 1, 2].map(|k| w[k] * screen[k][3]);
                        let sum: f32 = inv.iter().sum();
                        let vs = [a, b, c].map(|i| &vertices[i]);
                        let mix2 = |f: &dyn Fn(&Vertex) -> f32| {
                            (0..3).map(|k| inv[k] * f(vs[k])).sum::<f32>() / sum
                        };
                        let uv = [mix2(&|v| v.uv[0]), mix2(&|v| v.uv[1])];
                        let normal = [0, 1, 2].map(|axis| mix2(&|v| v.normal[axis]));
                        let texel = texture.map_or([0.6, 0.6, 0.6, 1.0], |t| sample(t, uv));
                        if mesh.alpha_cutoff().is_some_and(|cutoff| texel[3] < cutoff) {
                            continue;
                        }
                        let shade = shade(normalized(normal), light);
                        let alpha = if blended { texel[3] } else { 1.0 };
                        let out = &mut color[at * 4..at * 4 + 4];
                        for k in 0..3 {
                            let lit = (texel[k] * shade).min(1.0) * 255.0;
                            out[k] = (lit * alpha + f32::from(out[k]) * (1.0 - alpha)) as u8;
                        }
                        if !blended {
                            depth[at] = z;
                        }
                    }
                }
            }
        }
    }
    color
}

/// Clear colour of the pane, linear 0..1.
pub const BACKGROUND: [f32; 4] = [0.11, 0.12, 0.14, 1.0];

/// Two-sided Lambert plus ambient; `preview_gl`'s fragment shader matches it.
fn shade(normal: [f32; 3], light: [f32; 3]) -> f32 {
    0.5 + 0.65 * dot(normal, light).abs()
}

fn sample(texture: &DecodedImage, uv: [f32; 2]) -> [f32; 4] {
    let wrap = |c: f32, size: u32| ((c - c.floor()) * size as f32) as u32 % size.max(1);
    let x = wrap(uv[0], texture.width);
    let y = wrap(uv[1], texture.height);
    let at = ((y * texture.width + x) * 4) as usize;
    let p = &texture.rgba[at..at + 4];
    [0, 1, 2, 3].map(|k| f32::from(p[k]) / 255.0)
}

fn edge(a: [f32; 4], b: [f32; 4], p: [f32; 4]) -> f32 {
    (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
}

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn normalized(v: [f32; 3]) -> [f32; 3] {
    let length = dot(v, v).sqrt();
    if length < 1e-12 {
        [0.0, 0.0, 1.0]
    } else {
        v.map(|c| c / length)
    }
}

/// Column-major 4x4 matrix, the layout GL uniforms take.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Mat4(pub [f32; 16]);

const IDENTITY: Mat4 = Mat4([
    1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
]);

impl Mat4 {
    fn at(&self, row: usize, col: usize) -> f32 {
        self.0[col * 4 + row]
    }

    fn mul(&self, other: &Mat4) -> Mat4 {
        let mut out = [0.0; 16];
        for col in 0..4 {
            for row in 0..4 {
                out[col * 4 + row] = (0..4).map(|k| self.at(row, k) * other.at(k, col)).sum();
            }
        }
        Mat4(out)
    }

    fn add_scaled(&mut self, other: &Mat4, scale: f32) {
        for (a, b) in self.0.iter_mut().zip(other.0) {
            *a += b * scale;
        }
    }

    fn translation(t: [f32; 3]) -> Mat4 {
        let mut m = IDENTITY;
        m.0[12..15].copy_from_slice(&t);
        m
    }

    /// A unit quaternion's (w x y z) rotation. A zero quaternion (ZMD0002
    /// dummies carry no rotation) is the identity.
    fn rotation([w, x, y, z]: [f32; 4]) -> Mat4 {
        let length = (w * w + x * x + y * y + z * z).sqrt();
        if length < 1e-6 {
            return IDENTITY;
        }
        let (w, x, y, z) = (w / length, x / length, y / length, z / length);
        Mat4([
            1.0 - 2.0 * (y * y + z * z),
            2.0 * (x * y + w * z),
            2.0 * (x * z - w * y),
            0.0,
            2.0 * (x * y - w * z),
            1.0 - 2.0 * (x * x + z * z),
            2.0 * (y * z + w * x),
            0.0,
            2.0 * (x * z + w * y),
            2.0 * (y * z - w * x),
            1.0 - 2.0 * (x * x + y * y),
            0.0,
            0.0,
            0.0,
            0.0,
            1.0,
        ])
    }

    /// Inverse of a rotation + translation (no scale): bind poses are rigid.
    fn inverse_rigid(&self) -> Mat4 {
        let mut out = IDENTITY;
        for row in 0..3 {
            for col in 0..3 {
                out.0[col * 4 + row] = self.at(col, row);
            }
        }
        let t = [self.at(0, 3), self.at(1, 3), self.at(2, 3)];
        for row in 0..3 {
            out.0[12 + row] = -(0..3).map(|k| out.at(row, k) * t[k]).sum::<f32>();
        }
        out
    }

    fn look_at(eye: [f32; 3], target: [f32; 3], up: [f32; 3]) -> Mat4 {
        let f = normalized(sub(target, eye));
        let s = normalized(cross(f, up));
        let u = cross(s, f);
        Mat4([
            s[0],
            u[0],
            -f[0],
            0.0,
            s[1],
            u[1],
            -f[1],
            0.0,
            s[2],
            u[2],
            -f[2],
            0.0,
            -dot(s, eye),
            -dot(u, eye),
            dot(f, eye),
            1.0,
        ])
    }

    /// Right-handed, depth -1..1 (GL).
    fn perspective(fov_y: f32, aspect: f32, near: f32, far: f32) -> Mat4 {
        let f = 1.0 / (fov_y * 0.5).tan();
        let mut m = [0.0; 16];
        m[0] = f / aspect;
        m[5] = f;
        m[10] = (far + near) / (near - far);
        m[11] = -1.0;
        m[14] = 2.0 * far * near / (near - far);
        Mat4(m)
    }

    fn point(&self, p: [f32; 3]) -> [f32; 3] {
        [0, 1, 2].map(|r| {
            self.at(r, 0) * p[0] + self.at(r, 1) * p[1] + self.at(r, 2) * p[2] + self.at(r, 3)
        })
    }

    fn direction(&self, d: [f32; 3]) -> [f32; 3] {
        [0, 1, 2].map(|r| self.at(r, 0) * d[0] + self.at(r, 1) * d[1] + self.at(r, 2) * d[2])
    }

    fn project(&self, p: [f32; 3]) -> [f32; 4] {
        [0, 1, 2, 3].map(|r| {
            self.at(r, 0) * p[0] + self.at(r, 1) * p[1] + self.at(r, 2) * p[2] + self.at(r, 3)
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn close(a: [f32; 3], b: [f32; 3]) -> bool {
        (0..3).all(|k| (a[k] - b[k]).abs() < 1e-5)
    }

    fn quarter_turn_z() -> [f32; 4] {
        let half = std::f32::consts::FRAC_PI_4;
        [half.cos(), 0.0, 0.0, half.sin()]
    }

    #[test]
    fn quaternion_rotates_like_the_engine() {
        // 90 degrees about Z takes +X to +Y.
        let p = Mat4::rotation(quarter_turn_z()).point([1.0, 0.0, 0.0]);
        assert!(close(p, [0.0, 1.0, 0.0]), "{p:?}");
    }

    #[test]
    fn child_transform_applies_parent_first() {
        let parent = Mat4::translation([1.0, 0.0, 0.0]);
        let child = Mat4::rotation(quarter_turn_z());
        let p = parent.mul(&child).point([1.0, 0.0, 0.0]);
        assert!(close(p, [1.0, 1.0, 0.0]), "{p:?}");
    }

    #[test]
    fn rigid_inverse_undoes_the_transform() {
        let m = transform([0.3, -0.2, 1.1], quarter_turn_z());
        let p = m.inverse_rigid().point(m.point([0.5, 0.25, -1.0]));
        assert!(close(p, [0.5, 0.25, -1.0]), "{p:?}");
    }

    /// Root at the origin, child 1 m up; a dummy on the child.
    fn rig() -> Rig {
        let mut zmd = ZMD::new();
        let mut root = roselib::files::zmd::Bone::new();
        root.rotation.w = 1.0;
        let mut child = roselib::files::zmd::Bone::new();
        child.parent = 0;
        child.position.z = 100.0; // centimetres
        child.rotation.w = 1.0;
        let mut dummy = roselib::files::zmd::Bone::new();
        dummy.parent = 1;
        dummy.position.x = 10.0;
        zmd.bones = vec![root, child];
        zmd.dummy_bones = vec![dummy];
        Rig::from_zmd(&zmd)
    }

    #[test]
    fn bind_pose_leaves_skinned_vertices_where_they_are() {
        let joints = rig().joints(None, 0.0);
        for skin in &joints.skin {
            assert!(close(skin.point([0.2, 0.4, 1.3]), [0.2, 0.4, 1.3]));
        }
        assert!(close(joints.dummies[0].point([0.0; 3]), [0.1, 0.0, 1.0]));
    }

    #[test]
    fn animation_moves_children_and_skin_with_the_bone() {
        let animation = Animation {
            path: String::new(),
            fps: 1.0,
            frames: 2,
            positions: HashMap::new(),
            // Root turns a quarter about Z from frame 1.
            rotations: HashMap::from([(0, vec![[1.0, 0.0, 0.0, 0.0], quarter_turn_z()])]),
        };
        let joints = rig().joints(Some(&animation), 1.0);
        // The dummy, 0.1 m along +X of the child, now points along +Y.
        assert!(close(joints.dummies[0].point([0.0; 3]), [0.0, 0.1, 1.0]));
        // A vertex skinned to the child at its bind position follows the same turn.
        assert!(close(
            joints.skin[1].point([0.1, 0.0, 1.0]),
            [0.0, 0.1, 1.0]
        ));
        // Halfway between frames is halfway round.
        let half = rig().joints(Some(&animation), 0.5);
        let p = half.dummies[0].point([0.0; 3]);
        let angle = p[1].atan2(p[0]);
        assert!((angle - std::f32::consts::FRAC_PI_4).abs() < 1e-3, "{p:?}");
    }

    #[test]
    fn camera_frames_the_figure() {
        let framing = Framing {
            min: [-0.5, -0.3, 0.0],
            max: [0.5, 0.3, 1.8],
            focus: [0.0, 0.0, 0.9],
        };
        let (vp, _) = Camera::default().matrices(&framing, 0.75);
        for corner in [[-0.5, -0.3, 0.0], [0.5, 0.3, 1.8], [0.5, -0.3, 1.8]] {
            let [x, y, z, w] = vp.project(corner);
            assert!(w > 0.0);
            for c in [x / w, y / w, z / w] {
                assert!((-1.0..=1.0).contains(&c), "{corner:?} -> {c}");
            }
        }
    }

    #[test]
    fn actions_parse_by_first_word() {
        assert_eq!(Action::parse("battle"), Some(Action::Battle));
        assert_eq!(Action::parse("bind"), Some(Action::BindPose));
        assert_eq!(Action::parse("idle"), Some(Action::Idle));
    }
}
