use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{anyhow, Context, Result};
use egui::{ColorImage, TextureHandle, TextureOptions};
use roselib::files::TSI;
use roselib::io::RoseFile;

use crate::data::resolve_icon_dir;
use crate::dds;

/// A sprite rect within a named texture sheet.
#[derive(Debug, Clone)]
struct SpriteRef {
    sheet_idx: usize,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
}

pub struct IconStore {
    tsi: Option<TSI>,
    res_dir: PathBuf,
    /// Decoded sheets, keyed by sheet index in the TSI.
    sheets: HashMap<usize, dds::DecodedImage>,
    /// Egui textures keyed by (sheet_idx, sprite_idx_in_sheet).
    textures: HashMap<u64, Option<TextureHandle>>,
}

impl IconStore {
    pub fn load(root: &Path) -> Result<Self> {
        let res_dir = resolve_icon_dir(root)?;
        let tsi_path = find_file_ci(&res_dir, "ITEM1.TSI")
            .context("locating ITEM1.TSI inside 3DDATA/CONTROL/RES/")?;
        let tsi = TSI::from_path(&tsi_path)
            .map_err(|e| anyhow!("TSI::from_path({}): {}", tsi_path.display(), e))?;
        Ok(Self {
            tsi: Some(tsi),
            res_dir,
            sheets: HashMap::new(),
            textures: HashMap::new(),
        })
    }

    /// Used when the icons couldn't be loaded — the editor still works,
    /// just without visible icons.
    pub fn empty(root: &Path) -> Self {
        Self {
            tsi: None,
            res_dir: root.to_path_buf(),
            sheets: HashMap::new(),
            textures: HashMap::new(),
        }
    }

    /// Look up a sprite by its linear index (icon_no in the item tables).
    /// The TSI flattens all sheets' sprites into one global sprite index; we
    /// walk sheets in order to map icon_no -> (sheet, rect).
    fn locate(&self, icon_no: u32) -> Option<SpriteRef> {
        let tsi = self.tsi.as_ref()?;
        let mut seen = 0u32;
        for (sheet_idx, sheet) in tsi.sprite_sheets.iter().enumerate() {
            let count = sheet.sprites.len() as u32;
            if icon_no < seen + count {
                let sprite = &sheet.sprites[(icon_no - seen) as usize];
                let x = sprite.start_point.x.min(sprite.end_point.x);
                let y = sprite.start_point.y.min(sprite.end_point.y);
                let ex = sprite.start_point.x.max(sprite.end_point.x);
                let ey = sprite.start_point.y.max(sprite.end_point.y);
                return Some(SpriteRef {
                    sheet_idx,
                    x,
                    y,
                    w: ex - x,
                    h: ey - y,
                });
            }
            seen += count;
        }
        None
    }

    fn ensure_sheet(&mut self, sheet_idx: usize) -> Result<&dds::DecodedImage> {
        if !self.sheets.contains_key(&sheet_idx) {
            let tsi = self
                .tsi
                .as_ref()
                .ok_or_else(|| anyhow!("no TSI loaded"))?;
            let sheet = tsi
                .sprite_sheets
                .get(sheet_idx)
                .ok_or_else(|| anyhow!("sheet index out of range"))?;
            let sheet_name = sheet.path.to_string_lossy().to_string();
            let sheet_path = find_file_ci(&self.res_dir, &sheet_name)
                .with_context(|| format!("locating sheet texture {}", sheet_name))?;
            let bytes = fs::read(&sheet_path)
                .with_context(|| format!("reading {}", sheet_path.display()))?;
            let decoded = dds::decode(&bytes)
                .with_context(|| format!("decoding DDS {}", sheet_path.display()))?;
            self.sheets.insert(sheet_idx, decoded);
        }
        Ok(self.sheets.get(&sheet_idx).unwrap())
    }

    /// Get (or create) an egui texture handle for an item's icon.
    pub fn icon_texture(
        &mut self,
        ctx: &egui::Context,
        icon_no: i32,
    ) -> Option<TextureHandle> {
        if icon_no <= 0 {
            return None;
        }
        let key = icon_no as u64;
        if let Some(slot) = self.textures.get(&key) {
            return slot.clone();
        }
        let handle = self.try_build_texture(ctx, icon_no as u32);
        self.textures.insert(key, handle.clone());
        handle
    }

    fn try_build_texture(
        &mut self,
        ctx: &egui::Context,
        icon_no: u32,
    ) -> Option<TextureHandle> {
        let sprite = self.locate(icon_no)?;
        if sprite.w == 0 || sprite.h == 0 {
            return None;
        }
        let sheet = match self.ensure_sheet(sprite.sheet_idx) {
            Ok(s) => s,
            Err(e) => {
                log::warn!("ensure_sheet({}) failed: {}", sprite.sheet_idx, e);
                return None;
            }
        };
        let cropped = sheet.crop(sprite.x, sprite.y, sprite.w, sprite.h)?;
        let size = [cropped.width as usize, cropped.height as usize];
        let image = ColorImage::from_rgba_unmultiplied(size, &cropped.rgba);
        Some(ctx.load_texture(
            format!("icon_{}", icon_no),
            image,
            TextureOptions::LINEAR,
        ))
    }
}

fn find_file_ci(dir: &Path, name: &str) -> Result<PathBuf> {
    let direct = dir.join(name);
    if direct.exists() {
        return Ok(direct);
    }
    // Normalize: strip any directory component.
    let basename = Path::new(name)
        .file_name()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_else(|| name.to_string());
    let direct = dir.join(&basename);
    if direct.exists() {
        return Ok(direct);
    }
    for entry in fs::read_dir(dir).with_context(|| format!("reading {}", dir.display()))? {
        let entry = entry?;
        if entry.file_name().to_string_lossy().eq_ignore_ascii_case(&basename) {
            return Ok(entry.path());
        }
    }
    Err(anyhow!("file not found: {}/{}", dir.display(), name))
}
