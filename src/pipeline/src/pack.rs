use std::fs;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;

use clap::ArgMatches;
use globset::GlobSetBuilder;
use roselib::files::idx::{VfsFileMetadata, VfsIndex, VfsMetadata};
use roselib::io::RoseFile;
use walkdir::WalkDir;

use crate::command::{collect_command_globs, parse_command_file};

use crate::error::PipelineError;

pub const PACK_INPUT_DIR: &str = "INPUT_DIR";
pub const PACK_OUTPUT_DIR: &str = "OUTOUT_DIR";
pub const PACK_CONFIG_NAME: &str = "config_name";

pub const DEFAULT_IDX_NAME: &str = "data.idx";
pub const DEFAULT_VFS_NAME: &str = "rose.vfs";

pub fn pack(matches: &ArgMatches) -> Result<(), PipelineError> {
    println!("Starting pack process");

    let config_name = matches.value_of(PACK_CONFIG_NAME).unwrap();

    let input_dir = PathBuf::from(matches.value_of(PACK_INPUT_DIR).unwrap());
    if !input_dir.exists() {
        return Err(PipelineError::Message(format!(
            "Invalid input dir: {}",
            input_dir.to_str().unwrap()
        )));
    }

    println!("Setting input dir to {}", input_dir.display());

    let config_path = input_dir.join(config_name);
    if !config_path.exists() {
        return Err(PipelineError::Message(format!(
            "Config file does not exists: {}",
            config_path.to_str().unwrap()
        )));
    }

    if !config_path.is_file() {
        return Err(PipelineError::Message(format!(
            "Config argument is not a file: {}",
            config_path.to_str().unwrap()
        )));
    }

    println!("Reading config file {}", config_path.display());

    let commands = match parse_command_file(&config_path) {
        Ok(c) => c,
        Err(e) => {
            return Err(PipelineError::Message(format!(
                "Failed to load config file: {}",
                e
            )));
        }
    };

    let output_dir = PathBuf::from(matches.value_of(PACK_OUTPUT_DIR).unwrap());
    if !output_dir.exists() {
        fs::create_dir_all(&output_dir)?;
    }

    println!("Setting output dir to {}", output_dir.display());

    let globs = match collect_command_globs(commands.as_slice()) {
        Ok(c) => c,
        Err(e) => {
            return Err(PipelineError::Message(format!(
                "Failed to build file globs: {}",
                e
            )));
        }
    };

    let empty_globset = GlobSetBuilder::new();

    let no_pack_globs = globs.get("nopack").unwrap_or(&empty_globset);
    let ignore_globs = globs.get("ignore").unwrap_or(&empty_globset);

    let output_vfs_path = output_dir.join(DEFAULT_VFS_NAME);
    let mut vfs = match fs::File::create(&output_vfs_path) {
        Ok(f) => f,
        Err(e) => {
            return Err(PipelineError::Message(format!(
                "Failed to create vfs file {}: {}",
                output_vfs_path.display(),
                e
            )));
        }
    };

    let mut vfs_metadata = VfsMetadata::new();
    vfs_metadata.filename = PathBuf::from(DEFAULT_VFS_NAME);

    let is_hidden = |entry: &walkdir::DirEntry| -> bool {
        entry
            .file_name()
            .to_str()
            .map(|s| s.starts_with('.'))
            .unwrap_or(false)
    };

    let walker = WalkDir::new(&input_dir)
        .into_iter()
        .filter_entry(|e| !is_hidden(e));

    println!("Building vfs file {}", output_vfs_path.display());

    for entry in walker {
        if let Ok(ent) = entry {
            if !ent.file_type().is_file() {
                continue;
            }

            let input_path = ent.into_path();
            let input_relative_path = input_path.strip_prefix(&input_dir).unwrap().to_path_buf();

            if ignore_globs.build()?.is_match(&input_relative_path) {
                println!("Ignoring file {}", input_path.display());
                continue;
            }

            if no_pack_globs.build()?.is_match(&input_relative_path) {
                let output_path = output_dir.join(input_relative_path);
                let output_path_parent = output_path.parent().unwrap();

                if let Err(e) = fs::create_dir_all(&output_path_parent) {
                    return Err(PipelineError::Message(format!(
                        "Error creating output dir {}: {}",
                        output_path_parent.display(),
                        e
                    )));
                }

                println!("Copying file {}", input_path.display());
                if let Err(e) = fs::copy(&input_path, &output_path) {
                    return Err(PipelineError::Message(format!(
                        "Error copying file {}: {}",
                        input_path.display(),
                        e
                    )));
                }
                continue;
            }

            println!("Packing file {}", input_path.display());

            let mut input_file = match fs::File::open(&input_path) {
                Ok(f) => f,
                Err(e) => {
                    return Err(PipelineError::Message(format!(
                        "Failed to open file {}: {}",
                        input_path.display(),
                        e
                    )));
                }
            };

            let mut input_data = Vec::new();
            let input_size = match input_file.read_to_end(&mut input_data) {
                Ok(s) => s,
                Err(e) => {
                    return Err(PipelineError::Message(format!(
                        "Failed to read file {}: {}",
                        input_path.display(),
                        e
                    )));
                }
            };

            let cur_offset = vfs.seek(SeekFrom::Current(0)).unwrap();

            if let Err(e) = vfs.write_all(&input_data) {
                return Err(PipelineError::Message(format!(
                    "Failed to write file {} to vfs: {}",
                    input_path.display(),
                    e
                )));
            };

            // Client requires upper case
            let relative_path_upper = input_relative_path.to_string_lossy().to_uppercase();

            let mut vfs_file_metadata = VfsFileMetadata::new();
            vfs_file_metadata.filepath = PathBuf::from(relative_path_upper);
            vfs_file_metadata.offset = cur_offset as i32;
            vfs_file_metadata.size = input_size as i32;
            vfs_file_metadata.block_size = input_size as i32;
            vfs_file_metadata.version = 1;

            vfs_metadata.files.push(vfs_file_metadata);
        }
    }
    let output_idx_path = output_dir.join(DEFAULT_IDX_NAME);

    println!("Saving vfs index file {}", output_idx_path.display());

    let mut vfs_idx = VfsIndex::new();
    vfs_idx.base_version = 100;
    vfs_idx.current_version = 100;
    vfs_idx.file_systems.push(vfs_metadata);

    if let Err(e) = vfs_idx.write_to_path(&output_idx_path) {
        return Err(PipelineError::Message(format!(
            "Failed to save idx {}: {}",
            output_idx_path.display(),
            e
        )));
    };

    Ok(())
}
