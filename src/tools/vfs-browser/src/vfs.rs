use std::collections::HashMap;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use anyhow::{anyhow, Context, Result};
use roselib::files::idx::{VfsFileMetadata, VfsIndex};
use roselib::files::IDX;
use roselib::io::RoseFile;

/// A single file entry from the VFS index, flattened with the index of the VFS
/// file system it belongs to.
#[derive(Debug, Clone)]
pub struct VfsEntry {
    pub vfs_index: usize,
    pub path: String,
    pub offset: i32,
    pub size: i32,
    pub block_size: i32,
    pub is_deleted: bool,
    pub is_compressed: bool,
    pub is_encrypted: bool,
    pub version: i32,
}

/// A loaded VFS index along with the directory containing the .idx and .vfs
/// files. Handles all read/write operations on the packed files.
pub struct Vfs {
    pub idx_path: PathBuf,
    pub base_dir: PathBuf,
    pub index: VfsIndex,
}

impl Vfs {
    pub fn open(idx_path: impl AsRef<Path>) -> Result<Self> {
        let idx_path = idx_path.as_ref().to_path_buf();
        let base_dir = idx_path
            .parent()
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| PathBuf::from("."));

        let index = IDX::from_path(&idx_path)
            .map_err(|e| anyhow!("failed to load idx '{}': {}", idx_path.display(), e))?;

        Ok(Self {
            idx_path,
            base_dir,
            index,
        })
    }

    pub fn vfs_file_path(&self, vfs_index: usize) -> Result<PathBuf> {
        let meta = self
            .index
            .file_systems
            .get(vfs_index)
            .ok_or_else(|| anyhow!("invalid vfs index {}", vfs_index))?;
        Ok(self.base_dir.join(&meta.filename))
    }

    /// Flatten every file across all file systems into a single Vec.
    pub fn entries(&self) -> Vec<VfsEntry> {
        let mut out = Vec::new();
        for (i, vfs) in self.index.file_systems.iter().enumerate() {
            for f in &vfs.files {
                out.push(VfsEntry {
                    vfs_index: i,
                    path: f.filepath.to_string_lossy().replace('\\', "/"),
                    offset: f.offset,
                    size: f.size,
                    block_size: f.block_size,
                    is_deleted: f.is_deleted,
                    is_compressed: f.is_compressed,
                    is_encrypted: f.is_encrypted,
                    version: f.version,
                });
            }
        }
        out
    }

    /// Read the raw bytes for a given entry from its backing .vfs file.
    pub fn read_entry(&self, entry: &VfsEntry) -> Result<Vec<u8>> {
        let vfs_path = self.vfs_file_path(entry.vfs_index)?;
        let mut file = File::open(&vfs_path)
            .with_context(|| format!("opening {}", vfs_path.display()))?;
        file.seek(SeekFrom::Start(entry.offset as u64))?;
        let mut buf = vec![0u8; entry.size as usize];
        file.read_exact(&mut buf)
            .with_context(|| format!("reading {} bytes at offset {}", entry.size, entry.offset))?;
        Ok(buf)
    }

    /// Extract a single entry to an output file. Parent directories are created.
    pub fn extract_entry(&self, entry: &VfsEntry, output: &Path) -> Result<()> {
        let data = self.read_entry(entry)?;
        if let Some(parent) = output.parent() {
            fs::create_dir_all(parent).ok();
        }
        let mut out = File::create(output)
            .with_context(|| format!("creating {}", output.display()))?;
        out.write_all(&data)?;
        Ok(())
    }

    /// Find an entry by VFS path (case-insensitive match).
    pub fn find(&self, vfs_path: &str) -> Option<VfsEntry> {
        let needle = normalize_path(vfs_path);
        self.entries()
            .into_iter()
            .find(|e| normalize_path(&e.path) == needle)
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
        if self.index.file_systems.is_empty() {
            return Err(anyhow!("index has no vfs file systems"));
        }

        let data = fs::read(source)
            .with_context(|| format!("reading source {}", source.display()))?;
        let size = data.len() as i32;

        let vfs_file_path = self.vfs_file_path(0)?;
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

        let files = &mut self.index.file_systems[0].files;
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

        self.index
            .write_to_path(&self.idx_path)
            .map_err(|e| anyhow!("failed to write idx: {}", e))?;

        Ok(())
    }
}

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
        _ => a.name.to_ascii_uppercase().cmp(&b.name.to_ascii_uppercase()),
    });
    for child in &mut node.children {
        sort_tree(child);
    }
}
