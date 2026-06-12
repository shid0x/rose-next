use std::path::PathBuf;

use serde::{Deserialize, Serialize};

#[derive(Default, Serialize, Deserialize)]
pub struct ManifestFile {
    pub path: PathBuf,
    pub size: u64,
    pub checksum: String,
}

#[derive(Serialize, Deserialize)]
pub struct Manifest {
    pub version: u32,
    pub launcher: ManifestFile,
    pub files: Vec<ManifestFile>,
}

impl Default for Manifest {
    fn default() -> Self {
        Manifest {
            version: 0,
            launcher: ManifestFile::default(),
            files: Vec::new(),
        }
    }
}
