use std::collections::{HashMap, HashSet, VecDeque};
use std::fs::{self, File, OpenOptions};
use std::io::{Cursor, Read, Seek, SeekFrom, Write};
use std::num::Wrapping;
use std::path::{Path, PathBuf};

use anyhow::{anyhow, Context, Result};
use roselib::files::idx::{VfsFileMetadata, VfsIndex};
use roselib::files::{IDX, LIT, STB, TSI, ZON, ZSC};
use roselib::io::RoseFile;

/// A single file entry from the VFS index, flattened with the index of the VFS
/// file system it belongs to.
#[derive(Debug, Clone)]
pub struct VfsEntry {
    pub vfs_index: usize,
    pub path: String,
    pub offset: u64,
    pub size: u64,
    pub block_size: u64,
    pub is_deleted: bool,
    pub is_compressed: bool,
    pub is_encrypted: bool,
    pub version: i32,
    pub file_hash: Option<u32>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VfsFormat {
    Auto,
    Rose,
    Titan,
}

#[derive(Debug, Clone)]
pub struct VfsOpenOptions {
    pub format: VfsFormat,
    pub path_list: Option<PathBuf>,
    pub show_hashes: bool,
}

impl Default for VfsOpenOptions {
    fn default() -> Self {
        Self {
            format: VfsFormat::Auto,
            path_list: None,
            show_hashes: false,
        }
    }
}

enum VfsBackend {
    Rose { index: VfsIndex },
    Titan { index: TitanIndex },
}

/// A loaded VFS index along with the directory containing the archive files.
/// Handles all read/write operations on the packed files.
pub struct Vfs {
    pub idx_path: PathBuf,
    pub base_dir: PathBuf,
    backend: VfsBackend,
    show_hashes: bool,
}

impl Vfs {
    pub fn open_with_options(idx_path: impl AsRef<Path>, options: VfsOpenOptions) -> Result<Self> {
        let idx_path = idx_path.as_ref().to_path_buf();
        let base_dir = idx_path
            .parent()
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| PathBuf::from("."));

        let backend = match options.format {
            VfsFormat::Rose => VfsBackend::Rose {
                index: load_rose_index(&idx_path)?,
            },
            VfsFormat::Titan => VfsBackend::Titan {
                index: TitanIndex::load(&idx_path, options.path_list.as_deref())?,
            },
            VfsFormat::Auto => {
                let titan_data_path = TitanIndex::data_path_for(&idx_path);
                if titan_data_path.exists() {
                    match TitanIndex::load(&idx_path, options.path_list.as_deref()) {
                        Ok(index) => VfsBackend::Titan { index },
                        Err(titan_error) => match load_rose_index(&idx_path) {
                            Ok(index) => VfsBackend::Rose { index },
                            Err(_) => return Err(titan_error.context("auto-detecting TitanVFS")),
                        },
                    }
                } else {
                    VfsBackend::Rose {
                        index: load_rose_index(&idx_path)?,
                    }
                }
            }
        };

        Ok(Self {
            idx_path,
            base_dir,
            backend,
            show_hashes: options.show_hashes,
        })
    }

    pub fn format_name(&self) -> &'static str {
        match self.backend {
            VfsBackend::Rose { .. } => "ROSE TriggerVFS",
            VfsBackend::Titan { .. } => "TitanVFS",
        }
    }

    pub fn is_writable(&self) -> bool {
        matches!(self.backend, VfsBackend::Rose { .. })
    }

    pub fn vfs_file_path(&self, vfs_index: usize) -> Result<PathBuf> {
        match &self.backend {
            VfsBackend::Rose { index } => {
                let meta = index
                    .file_systems
                    .get(vfs_index)
                    .ok_or_else(|| anyhow!("invalid vfs index {}", vfs_index))?;
                Ok(self.base_dir.join(&meta.filename))
            }
            VfsBackend::Titan { index } => {
                if vfs_index == 0 {
                    Ok(index.data_path.clone())
                } else {
                    Err(anyhow!("invalid titan vfs index {}", vfs_index))
                }
            }
        }
    }

    pub fn vfs_file_name(&self, vfs_index: usize) -> String {
        match self.vfs_file_path(vfs_index) {
            Ok(path) => path
                .file_name()
                .map(|name| name.to_string_lossy().to_string())
                .unwrap_or_else(|| path.display().to_string()),
            Err(_) => String::new(),
        }
    }

    /// Flatten every file across all file systems into a single Vec.
    pub fn entries(&self) -> Vec<VfsEntry> {
        match &self.backend {
            VfsBackend::Rose { index } => {
                let mut out = Vec::new();
                for (i, vfs) in index.file_systems.iter().enumerate() {
                    for f in &vfs.files {
                        out.push(VfsEntry {
                            vfs_index: i,
                            path: f.filepath.to_string_lossy().replace('\\', "/"),
                            offset: f.offset as u64,
                            size: f.size as u64,
                            block_size: f.block_size as u64,
                            is_deleted: f.is_deleted,
                            is_compressed: f.is_compressed,
                            is_encrypted: f.is_encrypted,
                            version: f.version,
                            file_hash: None,
                        });
                    }
                }
                out
            }
            VfsBackend::Titan { index } => index.entries(self.show_hashes),
        }
    }

    /// Read the raw bytes for a given entry from its backing .vfs file.
    pub fn read_entry(&self, entry: &VfsEntry) -> Result<Vec<u8>> {
        match &self.backend {
            VfsBackend::Rose { .. } => {
                let vfs_path = self.vfs_file_path(entry.vfs_index)?;
                read_range(&vfs_path, entry.offset, entry.size as usize)
            }
            VfsBackend::Titan { index } => {
                let file_hash = entry
                    .file_hash
                    .ok_or_else(|| anyhow!("titan entry is missing its file hash"))?;
                let entry = index
                    .entry_by_hash(file_hash)
                    .ok_or_else(|| anyhow!("titan entry hash {:08X} not found", file_hash))?;
                read_range(&index.data_path, entry.offset, entry.size as usize)
            }
        }
    }

    /// Extract a single entry to an output file. Parent directories are created.
    pub fn extract_entry(&self, entry: &VfsEntry, output: &Path) -> Result<()> {
        let data = self.read_entry(entry)?;
        if let Some(parent) = output.parent() {
            fs::create_dir_all(parent).ok();
        }
        let mut out =
            File::create(output).with_context(|| format!("creating {}", output.display()))?;
        out.write_all(&data)?;
        Ok(())
    }

    /// Find an entry by VFS path (case-insensitive match).
    pub fn find(&self, vfs_path: &str) -> Option<VfsEntry> {
        match &self.backend {
            VfsBackend::Rose { .. } => {
                let needle = normalize_path(vfs_path);
                self.entries()
                    .into_iter()
                    .find(|e| normalize_path(&e.path) == needle)
            }
            VfsBackend::Titan { index } => index.find(vfs_path),
        }
    }

    /// Extract every (non-deleted) entry under a given folder prefix to `out_dir`,
    /// preserving the folder structure relative to that prefix.
    /// If `prefix` is empty, extracts everything.
    pub fn extract_tree(
        &self,
        prefix: &str,
        out_dir: &Path,
        mut on_progress: impl FnMut(&str, usize, usize),
    ) -> Result<usize> {
        let prefix_norm = normalize_path(prefix);
        let entries: Vec<VfsEntry> = self
            .entries()
            .into_iter()
            .filter(|e| !e.is_deleted)
            .filter(|e| {
                if prefix_norm.is_empty() {
                    true
                } else {
                    let p = normalize_path(&e.path);
                    p == prefix_norm || p.starts_with(&format!("{}/", prefix_norm))
                }
            })
            .collect();

        let total = entries.len();
        fs::create_dir_all(out_dir).ok();

        for (i, entry) in entries.iter().enumerate() {
            let rel_full = normalize_path(&entry.path);
            let rel = if prefix_norm.is_empty() {
                rel_full.clone()
            } else {
                rel_full
                    .strip_prefix(&prefix_norm)
                    .map(|s| s.trim_start_matches('/').to_string())
                    .unwrap_or(rel_full)
            };
            let target = out_dir.join(&rel);
            on_progress(&entry.path, i + 1, total);
            self.extract_entry(entry, &target)?;
        }
        Ok(total)
    }

    /// Add or replace a file in the VFS. The source file is read, appended to
    /// the .vfs file, and the index is updated in place. If the entry already
    /// exists, it is replaced (the old data stays orphaned in the .vfs).
    ///
    /// Files are always added to the first VFS file system in the index.
    pub fn add_file(&mut self, source: &Path, vfs_path: &str) -> Result<()> {
        let VfsBackend::Rose { index } = &mut self.backend else {
            return Err(anyhow!("TitanVFS support is read-only"));
        };

        if index.file_systems.is_empty() {
            return Err(anyhow!("index has no vfs file systems"));
        }

        let data =
            fs::read(source).with_context(|| format!("reading source {}", source.display()))?;
        let size = data.len() as i32;

        let vfs_file_path = self.base_dir.join(&index.file_systems[0].filename);
        let mut vfs_file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(&vfs_file_path)
            .with_context(|| format!("opening vfs {}", vfs_file_path.display()))?;

        let offset = vfs_file.seek(SeekFrom::End(0))? as i32;
        vfs_file.write_all(&data)?;
        vfs_file.flush()?;

        let stored_path = vfs_path.replace('\\', "/").to_uppercase();
        let needle = normalize_path(&stored_path);

        let files = &mut index.file_systems[0].files;
        let existing = files
            .iter_mut()
            .find(|f| normalize_path(&f.filepath.to_string_lossy()) == needle);

        if let Some(existing) = existing {
            existing.offset = offset;
            existing.size = size;
            existing.block_size = size;
            existing.is_deleted = false;
            existing.is_compressed = false;
            existing.is_encrypted = false;
            existing.version = existing.version.saturating_add(1).max(1);
            existing.checksum = 0;
        } else {
            let mut new_file = VfsFileMetadata::new();
            new_file.filepath = PathBuf::from(&stored_path);
            new_file.offset = offset;
            new_file.size = size;
            new_file.block_size = size;
            new_file.is_deleted = false;
            new_file.is_compressed = false;
            new_file.is_encrypted = false;
            new_file.version = 1;
            new_file.checksum = 0;
            files.push(new_file);
        }

        index
            .write_to_path(&self.idx_path)
            .map_err(|e| anyhow!("failed to write idx: {}", e))?;

        Ok(())
    }
}

fn load_rose_index(idx_path: &Path) -> Result<VfsIndex> {
    IDX::from_path(idx_path)
        .map_err(|e| anyhow!("failed to load idx '{}': {}", idx_path.display(), e))
}

fn read_range(path: &Path, offset: u64, size: usize) -> Result<Vec<u8>> {
    let mut file = File::open(path).with_context(|| format!("opening {}", path.display()))?;
    file.seek(SeekFrom::Start(offset))?;
    let mut buf = vec![0u8; size];
    file.read_exact(&mut buf)
        .with_context(|| format!("reading {} bytes at offset {}", size, offset))?;
    Ok(buf)
}

#[derive(Debug, Clone)]
struct TitanEntry {
    hash: u32,
    offset: u64,
    size: u32,
    known_path: Option<String>,
}

#[derive(Debug)]
struct TitanIndex {
    version: u32,
    data_path: PathBuf,
    entries: Vec<TitanEntry>,
    by_hash: HashMap<u32, usize>,
}

impl TitanIndex {
    fn data_path_for(idx_path: &Path) -> PathBuf {
        idx_path.with_file_name("data.trf")
    }

    fn load(idx_path: &Path, path_list: Option<&Path>) -> Result<Self> {
        let data_path = Self::data_path_for(idx_path);
        let mut index_data =
            fs::read(idx_path).with_context(|| format!("reading {}", idx_path.display()))?;
        if index_data.len() < 8 {
            return Err(anyhow!("titan idx '{}' is too short", idx_path.display()));
        }
        if !data_path.exists() {
            return Err(anyhow!(
                "TitanVFS data file not found next to idx: {}",
                data_path.display()
            ));
        }

        let version = read_u32_at(&index_data, 0)?;
        let mut file_count = read_u32_at(&index_data, 4)?;

        if (file_count & (1 << 28)) != 0 {
            let mut hash = file_count;
            file_count ^= 0x1337_BEEF;

            let mut pos = 8usize;
            while pos + 32 < index_data.len() {
                let next_hash = titan_generate_hash(&index_data[pos..pos + 32], hash);
                titan_crypt_block(&mut index_data[pos..pos + 32], hash);
                pos += 32;
                hash = next_hash;
            }
        }

        let file_count = file_count as usize;
        let expected_len = 8usize
            .checked_add(
                file_count
                    .checked_mul(16)
                    .ok_or_else(|| anyhow!("too many files"))?,
            )
            .ok_or_else(|| anyhow!("titan index length overflow"))?;
        if index_data.len() < expected_len {
            return Err(anyhow!(
                "titan idx '{}' is truncated: expected at least {} bytes, got {}",
                idx_path.display(),
                expected_len,
                index_data.len()
            ));
        }

        let mut entries = Vec::with_capacity(file_count);
        let mut by_hash = HashMap::with_capacity(file_count);
        let mut pos = 8usize;
        for idx in 0..file_count {
            let hash = read_u32_at(&index_data, pos)?;
            let size = read_u32_at(&index_data, pos + 4)?;
            let offset = read_u64_at(&index_data, pos + 8)?;
            pos += 16;

            entries.push(TitanEntry {
                hash,
                offset,
                size,
                known_path: None,
            });
            by_hash.insert(hash, idx);
        }

        let mut index = Self {
            version,
            data_path,
            entries,
            by_hash,
        };

        if let Some(path_list) = path_list {
            index.apply_path_list(path_list)?;
        }

        if !index.apply_path_cache(idx_path)? {
            index.discover_paths();
            index.write_path_cache(idx_path).ok();
        }

        Ok(index)
    }

    fn apply_path_list(&mut self, path_list: &Path) -> Result<()> {
        let text = fs::read_to_string(path_list)
            .with_context(|| format!("reading path list {}", path_list.display()))?;
        for line in text.lines() {
            let path = line.trim();
            if path.is_empty() || path.starts_with('#') {
                continue;
            }
            let hash = titan_path_hash(path);
            if let Some(index) = self.by_hash.get(&hash).copied() {
                self.entries[index].known_path =
                    Some(path.replace('\\', "/").trim_matches('/').to_string());
            }
        }
        Ok(())
    }

    fn path_cache_path(idx_path: &Path) -> PathBuf {
        idx_path.with_file_name("rose-vfs-path-cache.txt")
    }

    fn apply_path_cache(&mut self, idx_path: &Path) -> Result<bool> {
        let cache_path = Self::path_cache_path(idx_path);
        if !cache_path.exists() {
            return Ok(false);
        }

        self.apply_path_list(&cache_path)?;
        Ok(true)
    }

    fn write_path_cache(&self, idx_path: &Path) -> Result<()> {
        let mut paths: Vec<&str> = self
            .entries
            .iter()
            .filter_map(|entry| entry.known_path.as_deref())
            .collect();
        paths.sort_by_key(|path| path.to_ascii_uppercase());
        paths.dedup_by(|a, b| a.eq_ignore_ascii_case(b));
        fs::write(Self::path_cache_path(idx_path), paths.join("\n"))
            .with_context(|| "writing Titan path cache")?;
        Ok(())
    }

    fn entry_by_hash(&self, hash: u32) -> Option<&TitanEntry> {
        self.by_hash
            .get(&hash)
            .and_then(|index| self.entries.get(*index))
    }

    fn read_entry_data(&self, entry: &TitanEntry) -> Result<Vec<u8>> {
        read_range(&self.data_path, entry.offset, entry.size as usize)
    }

    fn register_path(&mut self, path: &str) -> bool {
        let display_path = clean_vfs_path(path);
        if display_path.is_empty() {
            return false;
        }

        let hash = titan_path_hash(&display_path);
        let Some(index) = self.by_hash.get(&hash).copied() else {
            return false;
        };

        if self.entries[index].known_path.is_none() {
            self.entries[index].known_path = Some(display_path);
        }
        true
    }

    fn discover_paths(&mut self) {
        let mut queue = VecDeque::new();
        let mut queued = HashSet::new();

        for path in TITAN_SEED_PATHS {
            enqueue_path(path, &mut queue, &mut queued);
        }

        while let Some(path) = queue.pop_front() {
            if !self.register_path(&path) {
                continue;
            }

            let current_dir = parent_vfs_path(&path);
            match extension_of(&path).as_deref() {
                Some("STB") => {
                    let Some(data) = self.read_known_path(&path) else {
                        continue;
                    };
                    if let Some(stb) = read_rose_file::<STB>(&data) {
                        for row in &stb.data {
                            for cell in row {
                                enqueue_candidates(
                                    cell,
                                    current_dir.as_deref(),
                                    &mut queue,
                                    &mut queued,
                                );
                            }
                        }
                    }
                }
                Some("ZSC") => {
                    let Some(data) = self.read_known_path(&path) else {
                        continue;
                    };
                    if let Some(zsc) = read_rose_file::<ZSC>(&data) {
                        for mesh in &zsc.meshes {
                            enqueue_candidates(
                                &mesh.to_string_lossy(),
                                current_dir.as_deref(),
                                &mut queue,
                                &mut queued,
                            );
                        }
                        for material in &zsc.materials {
                            enqueue_candidates(
                                &material.path.to_string_lossy(),
                                current_dir.as_deref(),
                                &mut queue,
                                &mut queued,
                            );
                        }
                        for effect in &zsc.effects {
                            enqueue_candidates(
                                &effect.to_string_lossy(),
                                current_dir.as_deref(),
                                &mut queue,
                                &mut queued,
                            );
                        }
                        for object in &zsc.objects {
                            for part in &object.parts {
                                enqueue_candidates(
                                    &part.animation_path.to_string_lossy(),
                                    current_dir.as_deref(),
                                    &mut queue,
                                    &mut queued,
                                );
                            }
                        }
                    }
                }
                Some("ZON") => {
                    let Some(data) = self.read_known_path(&path) else {
                        continue;
                    };
                    if let Some(zon) = read_rose_file::<ZON>(&data) {
                        for texture in &zon.textures {
                            enqueue_candidates(
                                texture,
                                current_dir.as_deref(),
                                &mut queue,
                                &mut queued,
                            );
                        }
                        enqueue_zone_block_paths(&path, &mut queue, &mut queued);
                    }
                }
                Some("TSI") => {
                    let Some(data) = self.read_known_path(&path) else {
                        continue;
                    };
                    if let Some(tsi) = read_rose_file::<TSI>(&data) {
                        for sheet in &tsi.sprite_sheets {
                            enqueue_candidates(
                                &sheet.path.to_string_lossy(),
                                current_dir.as_deref(),
                                &mut queue,
                                &mut queued,
                            );
                        }
                    }
                }
                Some("LIT") => {
                    let Some(data) = self.read_known_path(&path) else {
                        continue;
                    };
                    if let Some(lit) = read_rose_file::<LIT>(&data) {
                        for object in &lit.objects {
                            for part in &object.parts {
                                enqueue_candidates(
                                    &part.filename,
                                    current_dir.as_deref(),
                                    &mut queue,
                                    &mut queued,
                                );
                            }
                        }
                        for filename in &lit.filenames {
                            enqueue_candidates(
                                filename,
                                current_dir.as_deref(),
                                &mut queue,
                                &mut queued,
                            );
                        }
                    }
                }
                _ => {}
            }
        }
    }

    fn read_known_path(&self, path: &str) -> Option<Vec<u8>> {
        let entry = self.entry_by_hash(titan_path_hash(path))?;
        self.read_entry_data(entry).ok()
    }

    fn find(&self, vfs_path: &str) -> Option<VfsEntry> {
        let hash = parse_hash_path(vfs_path).unwrap_or_else(|| titan_path_hash(vfs_path));
        self.entry_by_hash(hash)
            .map(|entry| self.to_vfs_entry(entry, Some(normalize_path(vfs_path))))
    }

    fn entries(&self, show_hashes: bool) -> Vec<VfsEntry> {
        self.entries
            .iter()
            .filter(|entry| show_hashes || entry.known_path.is_some())
            .map(|entry| self.to_vfs_entry(entry, None))
            .collect()
    }

    fn to_vfs_entry(&self, entry: &TitanEntry, path_override: Option<String>) -> VfsEntry {
        let path = path_override
            .or_else(|| entry.known_path.clone())
            .unwrap_or_else(|| format!("@HASH/{:08X}.bin", entry.hash));

        VfsEntry {
            vfs_index: 0,
            path,
            offset: entry.offset,
            size: entry.size as u64,
            block_size: entry.size as u64,
            is_deleted: false,
            is_compressed: false,
            is_encrypted: false,
            version: self.version as i32,
            file_hash: Some(entry.hash),
        }
    }
}

fn read_rose_file<T: RoseFile>(data: &[u8]) -> Option<T> {
    let mut file = T::new();
    let mut cursor = Cursor::new(data);
    file.read(&mut cursor).ok()?;
    Some(file)
}

fn enqueue_path(path: &str, queue: &mut VecDeque<String>, queued: &mut HashSet<String>) {
    let path = clean_vfs_path(path);
    if path.is_empty() {
        return;
    }

    let key = normalize_path(&path);
    if queued.insert(key) {
        queue.push_back(path);
    }
}

fn enqueue_candidates(
    raw: &str,
    current_dir: Option<&str>,
    queue: &mut VecDeque<String>,
    queued: &mut HashSet<String>,
) {
    let raw = clean_vfs_path(raw);
    if !looks_like_vfs_file(&raw) {
        return;
    }

    enqueue_path(&raw, queue, queued);

    if !raw.contains('/') {
        if let Some(dir) = current_dir {
            enqueue_path(&format!("{}/{}", dir, raw), queue, queued);
        }
        if matches!(extension_of(&raw).as_deref(), Some("STB" | "STL")) {
            enqueue_path(&format!("3DDATA/STB/{}", raw), queue, queued);
        }
        if matches!(extension_of(&raw).as_deref(), Some("TSI" | "DDS" | "TGA" | "PNG" | "JPG" | "BMP")) {
            enqueue_path(&format!("3DDATA/CONTROL/RES/{}", raw), queue, queued);
        }
    }

    if raw.starts_with("3DDATA/") {
        return;
    }

    match extension_of(&raw).as_deref() {
        Some("ZSC") => {
            enqueue_path(&format!("3DDATA/AVATAR/{}", raw), queue, queued);
            enqueue_path(&format!("3DDATA/NPC/{}", raw), queue, queued);
            enqueue_path(&format!("3DDATA/ITEM/{}", raw), queue, queued);
            enqueue_path(&format!("3DDATA/WEAPON/{}", raw), queue, queued);
            enqueue_path(&format!("3DDATA/PAT/{}", raw), queue, queued);
            enqueue_path(&format!("3DDATA/SPECIAL/{}", raw), queue, queued);
        }
        Some("ZON") => {
            enqueue_path(&format!("3DDATA/MAPS/{}", raw), queue, queued);
        }
        Some("EFT") => {
            enqueue_path(&format!("3DDATA/EFFECT/{}", raw), queue, queued);
        }
        Some("QSD") => {
            enqueue_path(&format!("3DDATA/QUESTDATA/{}", raw), queue, queued);
        }
        Some("CON") => {
            enqueue_path(&format!("3DDATA/EVENT/{}", raw), queue, queued);
        }
        _ => {}
    }
}

fn enqueue_zone_block_paths(path: &str, queue: &mut VecDeque<String>, queued: &mut HashSet<String>) {
    let Some(zone_dir) = parent_vfs_path(path) else {
        return;
    };

    for block_y in 0..64 {
        for block_x in 0..64 {
            enqueue_path(&format!("{}/{}_{}.HIM", zone_dir, block_x, block_y), queue, queued);
            enqueue_path(&format!("{}/{}_{}.TIL", zone_dir, block_x, block_y), queue, queued);
            enqueue_path(&format!("{}/{}_{}.IFO", zone_dir, block_x, block_y), queue, queued);
            enqueue_path(&format!("{}/{}_{}.MOV", zone_dir, block_x, block_y), queue, queued);
            enqueue_path(
                &format!(
                    "{}/{}_{}/LIGHTMAP/BUILDINGLIGHTMAPDATA.LIT",
                    zone_dir, block_x, block_y
                ),
                queue,
                queued,
            );
            enqueue_path(
                &format!(
                    "{}/{}_{}/LIGHTMAP/OBJECTLIGHTMAPDATA.LIT",
                    zone_dir, block_x, block_y
                ),
                queue,
                queued,
            );
            enqueue_path(
                &format!(
                    "{}/{}_{}/{}_{}/LIGHTMAP/_PLANELIGHTINGMAP.DDS",
                    zone_dir, block_x, block_y, block_x, block_y
                ),
                queue,
                queued,
            );
        }
    }
}

fn clean_vfs_path(path: &str) -> String {
    path.trim()
        .trim_matches('"')
        .trim_matches('\0')
        .replace('\\', "/")
        .replace("//", "/")
        .trim_matches('/')
        .to_string()
}

fn parent_vfs_path(path: &str) -> Option<String> {
    clean_vfs_path(path)
        .rsplit_once('/')
        .map(|(parent, _)| parent.to_string())
}

fn extension_of(path: &str) -> Option<String> {
    clean_vfs_path(path)
        .rsplit_once('.')
        .map(|(_, ext)| ext.to_ascii_uppercase())
}

fn looks_like_vfs_file(path: &str) -> bool {
    let path = clean_vfs_path(path);
    if path.len() < 5 || path.contains('*') || path.contains('?') || path.contains(':') {
        return false;
    }

    matches!(
        extension_of(&path).as_deref(),
        Some(
            "STB" | "STL" | "ZSC" | "ZON" | "ZMS" | "ZMD" | "ZMO" | "DDS" | "TGA" | "PNG"
                | "JPG" | "BMP" | "TSI" | "EFT" | "PTL" | "WAV" | "OGG" | "QSD" | "CON"
                | "IFO" | "HIM" | "TIL" | "MOV" | "LIT" | "CHR" | "XML" | "ID" | "LTB"
        )
    )
}

fn read_u32_at(data: &[u8], pos: usize) -> Result<u32> {
    let bytes = data
        .get(pos..pos + 4)
        .ok_or_else(|| anyhow!("unexpected end of titan idx at byte {}", pos))?;
    Ok(u32::from_le_bytes(bytes.try_into().unwrap()))
}

fn read_u64_at(data: &[u8], pos: usize) -> Result<u64> {
    let bytes = data
        .get(pos..pos + 8)
        .ok_or_else(|| anyhow!("unexpected end of titan idx at byte {}", pos))?;
    Ok(u64::from_le_bytes(bytes.try_into().unwrap()))
}

fn titan_crypt_block(block: &mut [u8], hash: u32) {
    let hash_bytes = hash.to_le_bytes();
    let order = [3usize, 2, 0, 1];
    let mut key_index = 0usize;

    for byte in block.iter_mut().take(32) {
        *byte ^= hash_bytes[key_index];
        key_index = order[key_index];
    }
}

fn titan_generate_hash(block: &[u8], next_hash: u32) -> u32 {
    let mut hash_bytes = next_hash.to_le_bytes();

    for byte in block.iter().take(32) {
        let select = ((*byte as u32 % 1337) % 4) as usize;
        let mut value = hash_bytes[select];

        if value == 0 {
            for b in hash_bytes {
                value |= b;
            }
            for b in hash_bytes {
                value ^= b;
            }
        }

        let target = ((*byte % 23) % 4) as usize;
        let mixed = (((value as i32 - 3) % 63)
            | ((value as i32 + 5) % 37) ^ ((value as i32 % 25) + 6)) as u8;
        hash_bytes[target] ^= mixed;
    }

    u32::from_le_bytes(hash_bytes)
}

fn titan_path_hash(path: &str) -> u32 {
    let path = path.replace('/', "\\").replace("\\\\", "\\").to_uppercase();
    if path.is_empty() {
        return 0;
    }

    let mut seed1 = Wrapping(0xDEAD_C0DEu32);
    let mut seed2 = Wrapping(0x7FED_7FEDu32);

    for ch in path
        .chars()
        .map(|c| Wrapping(c.to_ascii_uppercase() as u32))
    {
        seed1 += seed2;
        seed2 *= Wrapping(0x21);
        seed1 ^= Wrapping(TITAN_HASH_TABLE[(ch.0 & 0xff) as usize]);
        seed2 = seed2 + seed1 + ch + Wrapping(3);
    }

    seed1.0
}

fn parse_hash_path(path: &str) -> Option<u32> {
    let trimmed = path
        .trim()
        .trim_start_matches("@HASH/")
        .trim_start_matches("@HASH\\")
        .trim_end_matches(".bin")
        .trim_end_matches(".BIN");
    let trimmed = trimmed
        .strip_prefix("0x")
        .or_else(|| trimmed.strip_prefix("0X"))
        .unwrap_or(trimmed);
    u32::from_str_radix(trimmed, 16).ok()
}

const TITAN_SEED_PATHS: &[&str] = &[
    "3DDATA/AVATAR/FEMALE.ZMD",
    "3DDATA/AVATAR/MALE.ZMD",
    "3DDATA/AVATAR/LIST_BACK.ZSC",
    "3DDATA/AVATAR/LIST_FACEIEM.ZSC",
    "3DDATA/AVATAR/LIST_FACEITEM.ZSC",
    "3DDATA/AVATAR/LIST_MARMS.ZSC",
    "3DDATA/AVATAR/LIST_MBODY.ZSC",
    "3DDATA/AVATAR/LIST_MCAP.ZSC",
    "3DDATA/AVATAR/LIST_MFACE.ZSC",
    "3DDATA/AVATAR/LIST_MFOOT.ZSC",
    "3DDATA/AVATAR/LIST_MHAIR.ZSC",
    "3DDATA/AVATAR/LIST_WARMS.ZSC",
    "3DDATA/AVATAR/LIST_WBODY.ZSC",
    "3DDATA/AVATAR/LIST_WCAP.ZSC",
    "3DDATA/AVATAR/LIST_WFACE.ZSC",
    "3DDATA/AVATAR/LIST_WFOOT.ZSC",
    "3DDATA/AVATAR/LIST_WHAIR.ZSC",
    "3DDATA/CONTROL/RES/CLANBACK.TSI",
    "3DDATA/CONTROL/RES/CLANCENTER.TSI",
    "3DDATA/CONTROL/RES/ITEM1.TSI",
    "3DDATA/CONTROL/RES/MINIMAP_ARROW.TGA",
    "3DDATA/CONTROL/RES/SKILLICON.TSI",
    "3DDATA/CONTROL/RES/SOKET.DDS",
    "3DDATA/CONTROL/RES/SOKETJAM.TSI",
    "3DDATA/CONTROL/RES/STATEICON.TSI",
    "3DDATA/CONTROL/RES/TARGETMARK.TSI",
    "3DDATA/CONTROL/XML/EXUI_STRID.ID",
    "3DDATA/CONTROL/XML/UI_STRID.ID",
    "3DDATA/EFFECT/LEVELUP_01.EFT",
    "3DDATA/EFFECT/TRAIL.DDS",
    "3DDATA/EVENT/OBJECT001.CON",
    "3DDATA/EVENT/OBJECT002.CON",
    "3DDATA/EVENT/OBJECT003.CON",
    "3DDATA/EVENT/OBJECT004.CON",
    "3DDATA/EVENT/OBJECT005.CON",
    "3DDATA/EVENT/OBJECT006.CON",
    "3DDATA/EVENT/OBJECT007.CON",
    "3DDATA/EVENT/OBJECT008.CON",
    "3DDATA/EVENT/OBJECT009.CON",
    "3DDATA/EVENT/ULNGTB_CON.LTB",
    "3DDATA/ITEM/LIST_FIELDITEM.ZSC",
    "3DDATA/NPC/LIST_NPC.CHR",
    "3DDATA/NPC/PART_NPC.ZSC",
    "3DDATA/PAT/CART/CART01.ZMD",
    "3DDATA/PAT/LIST_PAT.ZSC",
    "3DDATA/SPECIAL/EVENT_OBJECT.ZSC",
    "3DDATA/SPECIAL/LIST_DECO_SPECIAL.ZSC",
    "3DDATA/TITLE/CAMERA01_CREATE01.ZMO",
    "3DDATA/TITLE/CAMERA01_INGAME01.ZMO",
    "3DDATA/TITLE/CAMERA01_INSELECT01.ZMO",
    "3DDATA/TITLE/CAMERA01_INTRO01.ZMO",
    "3DDATA/TITLE/CAMERA01_OUTCREATE01.ZMO",
    "3DDATA/WEAPON/LIST_SUBWPN.ZSC",
    "3DDATA/WEAPON/LIST_WEAPON.ZSC",
    "3DDATA/STB/BADNAMES.STB",
    "3DDATA/STB/BADWORDS.STB",
    "3DDATA/STB/EVENT_OBJECT.STB",
    "3DDATA/STB/EVENTBUTTON.STB",
    "3DDATA/STB/FILE_AI.STB",
    "3DDATA/STB/FILE_EFFECT.STB",
    "3DDATA/STB/FILE_MOTION.STB",
    "3DDATA/STB/FILE_SKEL.STB",
    "3DDATA/STB/FILE_SOUND.STB",
    "3DDATA/STB/FILE_SUFFIX_COLOR.STB",
    "3DDATA/STB/FILE_TUTORIAL.STB",
    "3DDATA/STB/HELP.STB",
    "3DDATA/STB/HELP_S.STL",
    "3DDATA/STB/INIT_AVATAR.STB",
    "3DDATA/STB/ITEM_DROP.STB",
    "3DDATA/STB/LEVELUPEVENT.STB",
    "3DDATA/STB/LIST_APPRAISAL_STAT.STB",
    "3DDATA/STB/LIST_ARMS.STB",
    "3DDATA/STB/LIST_ARMS_S.STL",
    "3DDATA/STB/LIST_BACK.STB",
    "3DDATA/STB/LIST_BACK_S.STL",
    "3DDATA/STB/LIST_BODY.STB",
    "3DDATA/STB/LIST_BODY_S.STL",
    "3DDATA/STB/LIST_BREAK.STB",
    "3DDATA/STB/LIST_BULLET.STB",
    "3DDATA/STB/LIST_CAMERA.STB",
    "3DDATA/STB/LIST_CAP.STB",
    "3DDATA/STB/LIST_CAP_S.STL",
    "3DDATA/STB/LIST_CLAN_COLOR.STB",
    "3DDATA/STB/LIST_CLASS.STB",
    "3DDATA/STB/LIST_CLASS_S.STL",
    "3DDATA/STB/LIST_CNST_EJ.STB",
    "3DDATA/STB/LIST_CNST_JD.STB",
    "3DDATA/STB/LIST_CNST_JDT.STB",
    "3DDATA/STB/LIST_CNST_JG.STB",
    "3DDATA/STB/LIST_CNST_JPT.STB",
    "3DDATA/STB/LIST_CNST_LMT.STB",
    "3DDATA/STB/LIST_CNST_ODD.STB",
    "3DDATA/STB/LIST_CNST_ODT.STB",
    "3DDATA/STB/LIST_CURRENCY.STB",
    "3DDATA/STB/LIST_CURRENCY_S.STL",
    "3DDATA/STB/LIST_DUEL_CONSUMABLES.STB",
    "3DDATA/STB/LIST_EFFECT.STB",
    "3DDATA/STB/LIST_EVENT.STB",
    "3DDATA/STB/LIST_EVENTSTRING.STL",
    "3DDATA/STB/LIST_FACE.STB",
    "3DDATA/STB/LIST_FACEITEM.STB",
    "3DDATA/STB/LIST_FACEITEM_S.STL",
    "3DDATA/STB/LIST_FIELDITEM.STB",
    "3DDATA/STB/LIST_FOOT.STB",
    "3DDATA/STB/LIST_FOOT_S.STL",
    "3DDATA/STB/LIST_GAMEARENA.STB",
    "3DDATA/STB/LIST_GAMEARENA_S.STL",
    "3DDATA/STB/LIST_GEMITEM.STB",
    "3DDATA/STB/LIST_GEMITEM_S.STL",
    "3DDATA/STB/LIST_GRADE.STB",
    "3DDATA/STB/LIST_GRADE_COLOR.STB",
    "3DDATA/STB/LIST_HAIR.STB",
    "3DDATA/STB/LIST_HELP.STB",
    "3DDATA/STB/LIST_HITSOUND.STB",
    "3DDATA/STB/LIST_ITEM_RESTRICTION.STB",
    "3DDATA/STB/LIST_ITEM_RESTRICTION_S.STL",
    "3DDATA/STB/LIST_JEMITEM.STB",
    "3DDATA/STB/LIST_JEMITEM_S.STL",
    "3DDATA/STB/LIST_JEWEL.STB",
    "3DDATA/STB/LIST_JEWEL_S.STL",
    "3DDATA/STB/LIST_LANGUAGE.STB",
    "3DDATA/STB/LIST_LANGUAGE_S.STL",
    "3DDATA/STB/LIST_LOADING.STB",
    "3DDATA/STB/LIST_MACRO.STB",
    "3DDATA/STB/LIST_MESH_EFFECT.STB",
    "3DDATA/STB/LIST_MORPH_OBJECT.STB",
    "3DDATA/STB/LIST_MOUNT.STB",
    "3DDATA/STB/LIST_MOUNT_S.STL",
    "3DDATA/STB/LIST_NATURAL.STB",
    "3DDATA/STB/LIST_NATURAL_S.STL",
    "3DDATA/STB/LIST_NPC.STB",
    "3DDATA/STB/LIST_NPC_S.STL",
    "3DDATA/STB/LIST_NPCFACE.STB",
    "3DDATA/STB/LIST_PARTICLES.STB",
    "3DDATA/STB/LIST_PAT.STB",
    "3DDATA/STB/LIST_PAT_S.STL",
    "3DDATA/STB/LIST_PATWPN.STB",
    "3DDATA/STB/LIST_PRODUCT.STB",
    "3DDATA/STB/LIST_QUEST.STB",
    "3DDATA/STB/LIST_QUEST_S.STL",
    "3DDATA/STB/LIST_QUESTDATA.STB",
    "3DDATA/STB/LIST_QUESTIMAGE.STB",
    "3DDATA/STB/LIST_QUESTITEM.STB",
    "3DDATA/STB/LIST_QUESTITEM_S.STL",
    "3DDATA/STB/LIST_REFINE.STB",
    "3DDATA/STB/LIST_SELL.STB",
    "3DDATA/STB/LIST_SELL_S.STL",
    "3DDATA/STB/LIST_SET.STB",
    "3DDATA/STB/LIST_SET_S.STL",
    "3DDATA/STB/LIST_SKILL.STB",
    "3DDATA/STB/LIST_SKILL_P.STB",
    "3DDATA/STB/LIST_SKILL_S.STL",
    "3DDATA/STB/LIST_SKY.STB",
    "3DDATA/STB/LIST_STATUS.STB",
    "3DDATA/STB/LIST_STATUS_ITEMMALL.STB",
    "3DDATA/STB/LIST_STATUS_ITEMMALL_S.STL",
    "3DDATA/STB/LIST_STATUS_S.STL",
    "3DDATA/STB/LIST_STEPSOUND.STB",
    "3DDATA/STB/LIST_STRING.STB",
    "3DDATA/STB/LIST_STRING.STL",
    "3DDATA/STB/LIST_SUBWPN.STB",
    "3DDATA/STB/LIST_SUBWPN_S.STL",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_EJ.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_EZ.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_JD.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_JDT.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_JG.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_JPT.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_JZ.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_JZC.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_JZP.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_LP.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_LZ.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_ODD.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_ODG.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_ODT.STB",
    "3DDATA/STB/LIST_TERRAIN_OBJECT_SPECIAL.STB",
    "3DDATA/STB/LIST_UNION.STB",
    "3DDATA/STB/LIST_UNION_S.STL",
    "3DDATA/STB/LIST_UPGRADE.STB",
    "3DDATA/STB/LIST_USEITEM.STB",
    "3DDATA/STB/LIST_USEITEM_S.STL",
    "3DDATA/STB/LIST_WEAPON.STB",
    "3DDATA/STB/LIST_WEAPON_S.STL",
    "3DDATA/STB/LIST_ZONE.STB",
    "3DDATA/STB/LIST_ZONE_BLOCKUSEITEM.STB",
    "3DDATA/STB/LIST_ZONE_S.STL",
    "3DDATA/STB/PART_NPC.STB",
    "3DDATA/STB/PRODUCT.STB",
    "3DDATA/STB/QUEST_TRACKER.STB",
    "3DDATA/STB/QUEST_TRACKER_ITEM.STB",
    "3DDATA/STB/RANGESET.STB",
    "3DDATA/STB/RESOLUTION.STB",
    "3DDATA/STB/STR_ABILITY.STL",
    "3DDATA/STB/STR_CLAN.STL",
    "3DDATA/STB/STR_ITEMGRADE.STL",
    "3DDATA/STB/STR_ITEMGRADECOLOR.STL",
    "3DDATA/STB/STR_ITEMMALL_CATEGORY.STL",
    "3DDATA/STB/STR_ITEMMALL_COMMENT.STL",
    "3DDATA/STB/STR_ITEMPREFIX.STL",
    "3DDATA/STB/STR_ITEMSUFFIX.STL",
    "3DDATA/STB/STR_ITEMTYPE.STL",
    "3DDATA/STB/STR_JOB.STL",
    "3DDATA/STB/STR_PLANET.STL",
    "3DDATA/STB/STR_SKILLFORMULA.STL",
    "3DDATA/STB/STR_SKILLTARGET.STL",
    "3DDATA/STB/STR_SKILLTYPE.STL",
    "3DDATA/STB/TYPE_MOTION.STB",
    "3DDATA/STB/WARP.STB",
];

const TITAN_HASH_TABLE: [u32; 256] = [
    0x697A5, 0x6045C, 0xAB4E2, 0x409E4, 0x71209, 0x32392, 0xA7292, 0xB09FC, 0x4B658, 0xAAAD5,
    0x9B9CF, 0xA326A, 0x8DD12, 0x38150, 0x8E14D, 0x2EB7F, 0xE0A56, 0x7E6FA, 0xDFC27, 0xB1301,
    0x8B4F7, 0xA7F70, 0xAA713, 0x6CC0F, 0x6FEDF, 0x2EC87, 0xC0F1C, 0x45CA4, 0x30DF8, 0x60E99,
    0xBC13E, 0x4E0B5, 0x6318B, 0x82679, 0x26EF2, 0x79C95, 0x86DDC, 0x99BC0, 0xB7167, 0x72532,
    0x68765, 0xC7446, 0xDA70D, 0x9D132, 0xE5038, 0x2F755, 0x9171F, 0xCB49E, 0x6F925, 0x601D3,
    0x5BD8A, 0x2A4F4, 0x9B022, 0x706C3, 0x28C10, 0x2B24B, 0x7CD55, 0xCA355, 0xD95F4, 0x727BC,
    0xB1138, 0x9AD21, 0xC0ACA, 0xCD928, 0x953E5, 0x97A20, 0x345F3, 0xBDC03, 0x7E157, 0x96C99,
    0x968EF, 0x92AA9, 0xC2276, 0xA695D, 0x6743B, 0x2723B, 0x58980, 0x66E08, 0x51D1B, 0xB97D2,
    0x6CAEE, 0xCC80F, 0x3BA6C, 0xB0BF5, 0x9E27B, 0xD122C, 0x48611, 0x8C326, 0xD2AF8, 0xBB3B7,
    0xDED7F, 0x4B236, 0xD298F, 0xBE912, 0xDC926, 0xC873F, 0xD0716, 0x9E1D3, 0x48D94, 0x9BD91,
    0x5825D, 0x55637, 0xB2057, 0xBCC6C, 0x460DE, 0xAE7FB, 0x81B03, 0x34D8F, 0xC0528, 0xC9B59,
    0x3D260, 0x6051D, 0x93757, 0x8027F, 0xB7C34, 0x4A14E, 0xB12B8, 0xE4945, 0x28203, 0xA1C0F,
    0xAA382, 0x46ABB, 0x330B9, 0x5A114, 0xA754B, 0xC68D0, 0x9040E, 0x6C955, 0xBB1EF, 0x51E6B,
    0x9FF21, 0x51BCA, 0x4C879, 0xDFF70, 0x5B5EE, 0x29936, 0xB9247, 0x42611, 0x2E353, 0x26F3A,
    0x683A3, 0xA1082, 0x67333, 0x74EB7, 0x754BA, 0x369D5, 0x8E0BC, 0xABAFD, 0x6630B, 0xA3A7E,
    0xCDBB1, 0x8C2DE, 0x92D32, 0x2F8ED, 0x7EC54, 0x572F5, 0x77461, 0xCB3F5, 0x82C64, 0x35FE0,
    0x9203B, 0xADA2D, 0xBAEBD, 0xCB6AF, 0xC8C9A, 0x5D897, 0xCB727, 0xA13B3, 0xB4D6D, 0xC4929,
    0xB8732, 0xCCE5A, 0xD3E69, 0xD4B60, 0x89941, 0x79D85, 0x39E0F, 0x6945B, 0xC37F8, 0x77733,
    0x45D7D, 0x25565, 0xA3A4E, 0xB9F9E, 0x316E4, 0x36734, 0x6F5C3, 0xA8BA6, 0xC0871, 0x42D05,
    0x40A74, 0x2E7ED, 0x67C1F, 0x28BE0, 0xE162B, 0xA1C0F, 0x2F7E5, 0xD505A, 0x9FCC8, 0x78381,
    0x29394, 0x53D6B, 0x7091D, 0xA2FB1, 0xBB942, 0x29906, 0xC412D, 0x3FCD5, 0x9F2EB, 0x8F0CC,
    0xE25C3, 0x7E519, 0x4E7D9, 0x5F043, 0xBBA1B, 0x6710A, 0x819FB, 0x9A223, 0x38E47, 0xE28AD,
    0xB690B, 0x42328, 0x7CF7E, 0xAE108, 0xE54BA, 0xBA5A1, 0xA09A6, 0x9CAB7, 0xDB2B3, 0xA98CC,
    0x5CEBA, 0x9245D, 0x5D083, 0x8EA21, 0xAE349, 0x54940, 0x8E557, 0x83EFD, 0xDC504, 0xA6059,
    0xB85C9, 0x9D162, 0x7AEB6, 0xBED34, 0xB4963, 0xE367B, 0x4C891, 0x9E42C, 0xD4304, 0x96EAA,
    0xD5D69, 0x866B8, 0x83508, 0x7BAEC, 0xD03FD, 0xDA122,
];

/// Normalize a VFS path for comparison: forward slashes, uppercase, trim.
pub fn normalize_path(p: &str) -> String {
    p.replace('\\', "/").trim_matches('/').to_uppercase()
}

/// Build a simple directory tree from a flat list of entries.
#[derive(Debug, Default)]
pub struct TreeNode {
    pub name: String,
    pub full_path: String,
    pub is_dir: bool,
    pub entry_index: Option<usize>,
    pub children: Vec<TreeNode>,
}

impl TreeNode {
    pub fn build(entries: &[VfsEntry]) -> TreeNode {
        let mut root = TreeNode {
            name: "<root>".to_string(),
            full_path: String::new(),
            is_dir: true,
            entry_index: None,
            children: Vec::new(),
        };

        // Index children by name for fast insertion during build.
        let mut dir_children: HashMap<String, HashMap<String, usize>> = HashMap::new();
        dir_children.insert(String::new(), HashMap::new());

        for (idx, entry) in entries.iter().enumerate() {
            if entry.is_deleted {
                continue;
            }
            let normalized = entry.path.replace('\\', "/");
            let parts: Vec<&str> = normalized.split('/').filter(|s| !s.is_empty()).collect();
            if parts.is_empty() {
                continue;
            }

            let mut current = &mut root;
            let mut current_path = String::new();

            for (i, part) in parts.iter().enumerate() {
                let is_last = i == parts.len() - 1;
                let child_path = if current_path.is_empty() {
                    part.to_string()
                } else {
                    format!("{}/{}", current_path, part)
                };

                let pos = current
                    .children
                    .iter()
                    .position(|c| c.name.eq_ignore_ascii_case(part));

                let pos = match pos {
                    Some(p) => p,
                    None => {
                        current.children.push(TreeNode {
                            name: part.to_string(),
                            full_path: child_path.clone(),
                            is_dir: !is_last,
                            entry_index: if is_last { Some(idx) } else { None },
                            children: Vec::new(),
                        });
                        current.children.len() - 1
                    }
                };

                current = &mut current.children[pos];
                current_path = child_path;
            }
        }

        // Suppress unused warning for dir_children (kept in case of future use).
        let _ = dir_children;

        sort_tree(&mut root);
        root
    }
}

fn sort_tree(node: &mut TreeNode) {
    node.children.sort_by(|a, b| match (a.is_dir, b.is_dir) {
        (true, false) => std::cmp::Ordering::Less,
        (false, true) => std::cmp::Ordering::Greater,
        _ => match (a.name == "@HASH", b.name == "@HASH") {
            (true, false) => std::cmp::Ordering::Greater,
            (false, true) => std::cmp::Ordering::Less,
            _ => a
                .name
                .to_ascii_uppercase()
                .cmp(&b.name.to_ascii_uppercase()),
        },
    });
    for child in &mut node.children {
        sort_tree(child);
    }
}
