use std::collections::HashMap;
use std::fs;
use std::fs::File;
use std::path::{Path, PathBuf};
use std::process;
use std::time;

use clap::ArgMatches;
use globset::{Glob, GlobSetBuilder};
use serde::{Deserialize, Serialize};
use walkdir::WalkDir;

use rose_conv::{FromCsv, FromJson};
use roselib::files::*;
use roselib::io::RoseFile;

use crate::error::PipelineError;

pub const BAKE_INPUT_DIR: &str = "INPUT_DIR";
pub const BAKE_OUTPUT_DIR: &str = "OUTOUT_DIR";
pub const BAKE_CONFIG_NAME: &str = "config_name";
pub const BAKE_CLEAN_NAME: &str = "clean";

#[derive(Serialize, Deserialize, Clone, Copy, Debug, PartialEq)]
struct BakeFileMetadata {
    size: u64,
    modified: time::SystemTime,
}

impl Default for BakeFileMetadata {
    fn default() -> BakeFileMetadata {
        BakeFileMetadata {
            size: 0,
            modified: time::SystemTime::now(),
        }
    }
}

impl From<&fs::Metadata> for BakeFileMetadata {
    fn from(m: &fs::Metadata) -> BakeFileMetadata {
        BakeFileMetadata {
            size: m.len(),
            modified: m.modified().unwrap_or_else(|_| time::SystemTime::now()),
        }
    }
}

type BakeCache = HashMap<PathBuf, BakeFileMetadata>;

/// Read the bake file metadata cache from disk
fn get_bake_cache(cache_file_path: &Path) -> Result<BakeCache, PipelineError> {
    if !cache_file_path.exists() {
        return Ok(HashMap::new());
    }

    let cache_file = File::open(cache_file_path)?;
    match bincode::deserialize_from(cache_file) {
        Ok(cache) => Ok(cache),
        Err(_) => Ok(HashMap::new()),
    }
}

/// Save the bake file metadata cache to disk
fn save_bake_cache(cache_file_path: &Path, cache: &BakeCache) -> Result<(), PipelineError> {
    if !cache_file_path.exists() {
        fs::create_dir_all(cache_file_path.parent().unwrap())?;
    }

    let cache_file = File::create(cache_file_path)?;
    if let Err(e) = bincode::serialize_into(cache_file, cache) {
        return Err(PipelineError::Message(format!(
            "Failed to save cache file: {}",
            e
        )));
    }

    Ok(())
}

/// Checks if the path needs to be re-baked by comparing current file data
/// to the value stored in the cache.
fn should_rebake(path: &Path, metadata: &BakeFileMetadata, cache: &mut BakeCache) -> bool {
    if let Some(cached_metadata) = cache.get(path) {
        cached_metadata != metadata
    } else {
        true
    }
}

fn bake_dds(input_filepath: &Path, output_filepath: &Path) -> Result<(), PipelineError> {
    let out_dir = &output_filepath
        .parent()
        .ok_or_else(|| PipelineError::Message("Failed to get output dir".to_string()))?
        .to_str()
        .ok_or_else(|| PipelineError::Message("Failed to get output dir".to_string()))?;

    let in_file = &input_filepath
        .to_str()
        .ok_or_else(|| PipelineError::Message("Failed to get input file as string".to_string()))?;

    let res = process::Command::new("texconv.exe")
        .args(&[
            "-y",
            "-l", // Force lowercase file name
            "-nologo",
            "-sepalpha",
            "-f",
            "DXT5",
            "-o",
            &out_dir,
            &in_file,
        ])
        .output()
        .map_err(|e| PipelineError::Message(format!("Error running texconv: {}", e)))?;

    if !res.status.success() {
        let mut error_string = String::from("texconv_failed");
        for stdres in &[&res.stdout, &res.stderr] {
            if !stdres.is_empty() {
                error_string.push('\n');
                error_string.push_str(&std::str::from_utf8(stdres)?);
            }
        }
        return Err(PipelineError::Message(error_string));
    }

    Ok(())
}

pub fn bake(matches: &ArgMatches) -> Result<(), PipelineError> {
    println!("Starting bake process");

    let config_name = matches.value_of(BAKE_CONFIG_NAME).unwrap();

    let input_dir = PathBuf::from(matches.value_of(BAKE_INPUT_DIR).unwrap());
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

    let raw_config = fs::read_to_string(&config_path)?;
    let lines = raw_config
        .split('\n')
        .map(|line| line.trim().replace("\r", ""))
        .filter(|line| !line.starts_with('#') && !line.is_empty());

    let output_dir = PathBuf::from(matches.value_of(BAKE_OUTPUT_DIR).unwrap());
    if !output_dir.exists() {
        fs::create_dir_all(&output_dir)?;
    }

    println!("Setting output dir to {}", output_dir.display());

    let pipeline_dir = input_dir.join(".pipeline");
    let cache_file_path = pipeline_dir.join("bake");

    println!("Loading cache file {}", cache_file_path.display());

    let mut cache = match get_bake_cache(&cache_file_path) {
        Ok(c) => c,
        Err(e) => {
            return Err(PipelineError::Message(format!(
                "Bake cache file error ({}): {:?}",
                &pipeline_dir.to_str().unwrap(),
                e
            )))
        }
    };

    let mut file_list = Vec::new();

    println!("Scanning input directory {}", input_dir.display());

    let is_hidden = |entry: &walkdir::DirEntry| -> bool {
        entry
            .file_name()
            .to_str()
            .map(|s| s != "." && s != ".." && s.starts_with('.'))
            .unwrap_or(false)
    };

    for entry in WalkDir::new(&input_dir)
        .into_iter()
        .filter_entry(|e| !is_hidden(e))
    {
        if let Ok(ent) = entry {
            if !ent.file_type().is_file() {
                continue;
            }
            let entry_path = ent.into_path();
            match fs::metadata(&entry_path) {
                Ok(_) => {
                    let relative_path = entry_path.strip_prefix(&input_dir).unwrap().to_path_buf();
                    file_list.push(relative_path.clone());
                }
                Err(e) => {
                    println!(
                        "Error reading metadata for file {}: {}",
                        entry_path.display(),
                        e
                    );
                    continue;
                }
            }
        }
    }

    file_list.sort_unstable();

    let mut glob_builder = GlobSetBuilder::new();
    let mut commands: Vec<Vec<String>> = Vec::new();

    println!("Parsing command list");

    for (line_number, line) in lines.enumerate() {
        let line_number = line_number + 1;

        let args: Vec<String> = line.split_whitespace().map(|s| s.to_string()).collect();
        if args.len() < 2 {
            continue;
        }

        let file_glob = &args[1];
        match Glob::new(&file_glob) {
            Ok(g) => {
                glob_builder.add(g);
                commands.push(args);
            }
            Err(e) => {
                println!("Skipping line {}: Error: {}", line_number, e);
            }
        }
    }

    let globs = glob_builder.build()?;

    // Remove stale keys from cache
    let cached_filepaths: Vec<PathBuf> = cache.keys().map(|p| p.to_path_buf()).collect();
    for cached_filepath in cached_filepaths {
        let full_filepath = input_dir.join(&cached_filepath);
        if !full_filepath.exists()
            || !globs.is_match(&cached_filepath)
            || file_list.binary_search(&cached_filepath).is_err()
        {
            let _ = cache.remove(&cached_filepath);
        }
    }

    println!("Executing commands.");

    let mut output_filepaths = Vec::new();

    'file_loop: for relative_filepath in file_list {
        let input_filepath = input_dir.join(&relative_filepath);

        if !input_filepath.exists() {
            let _ = cache.remove(&relative_filepath);
            continue;
        }

        let rebake = match fs::metadata(&input_filepath) {
            Ok(m) => should_rebake(&relative_filepath, &BakeFileMetadata::from(&m), &mut cache),
            Err(e) => {
                println!(
                    "Error reading file metadata {}: {}",
                    input_filepath.display(),
                    e
                );
                false
            }
        };

        let command_indices = globs.matches(&relative_filepath);
        for command_index in command_indices.iter() {
            let command_index = *command_index;
            let args = &commands[command_index];

            let command = args[0].to_lowercase();
            let output_filepath = match command.as_str() {
                "copy" => output_dir.join(&relative_filepath),
                "dds" => output_dir.join(&relative_filepath).with_extension("dds"),
                "stb" => output_dir.join(&relative_filepath).with_extension("stb"),
                "stl" => output_dir.join(&relative_filepath).with_extension("stl"),
                "zsc" => output_dir.join(&relative_filepath).with_extension("zsc"),
                _ => PathBuf::new(),
            };

            output_filepaths.push(output_filepath.clone());

            if !rebake && output_filepath.exists() {
                continue 'file_loop;
            }

            let output_filedir = match output_filepath.parent() {
                Some(p) => p,
                None => {
                    eprintln!(
                        "Output filepath {} generated from input file {} has no parent",
                        &output_filepath.display(),
                        &relative_filepath.display()
                    );
                    continue;
                }
            };

            if let Err(e) = fs::create_dir_all(&output_filedir) {
                eprintln!(
                    "Error creating output dir {}: {}",
                    output_filedir.display(),
                    e
                );
                continue;
            }

            // Execute the specific command
            let command_result = match command.as_str() {
                "copy" => {
                    println!("Copying file {}", input_filepath.display());
                    fs::copy(&input_filepath, &output_filepath)
                        .map_err(|e| PipelineError::from(e))
                        .err()
                }
                "dds" => {
                    println!("Converting to dds {}", input_filepath.display());
                    bake_dds(&input_filepath, &output_filepath)
                        .map_err(|e| PipelineError::from(e))
                        .err()
                }
                "stb" => {
                    println!("Converting to stb {}", input_filepath.display());
                    STB::from_csv_path(&input_filepath)
                        .and_then(|mut stb| stb.write_to_path(&output_filepath))
                        .map_err(|e| PipelineError::from(e))
                        .err()
                }
                "stl" => {
                    println!("Converting to stl {}", input_filepath.display());
                    STL::from_csv_path(&input_filepath)
                        .and_then(|mut stl| stl.write_to_path(&output_filepath))
                        .map_err(|e| PipelineError::from(e))
                        .err()
                }
                "zsc" => {
                    println!("Converting to zsc {}", input_filepath.display());
                    ZSC::from_json_path(&input_filepath)
                        .and_then(|mut zsc| zsc.write_to_path(&output_filepath))
                        .map_err(|e| PipelineError::from(e))
                        .err()
                }
                _ => Some(PipelineError::Message(format!(
                    "Unrecognized command {}",
                    command.as_str()
                ))),
            };

            let command_executed = if let Some(e) = command_result {
                eprintln!(
                    "Error executing command `{}` for file {}: {}",
                    command.as_str(),
                    input_filepath.display(),
                    e
                );
                false
            } else {
                true
            };

            if command_executed {
                if let Ok(new_metadata) = fs::metadata(&input_filepath) {
                    let _ = cache.insert(
                        relative_filepath.clone(),
                        BakeFileMetadata::from(&new_metadata),
                    );
                }
            }
        }
    }

    // Clean the output directory of files that don't match what our bake
    // commands created. Ensures our output directory is clean without
    // having to rebake every single file
    let clean_output_dir = matches.is_present(BAKE_CLEAN_NAME);
    if clean_output_dir {
        println!("Cleaning output directory.");
        let mut output_subdirs = Vec::new();

        for entry in WalkDir::new(&output_dir).into_iter() {
            if let Ok(ent) = entry {
                // Symlinks are unsupported
                if ent.file_type().is_symlink() {
                    continue;
                }

                let entry_path = ent.clone().into_path();
                if ent.file_type().is_dir() {
                    output_subdirs.push(entry_path);
                    continue;
                }

                if output_filepaths.contains(&entry_path) {
                    continue;
                }

                println!("Removing file {}", entry_path.display());

                if let Err(e) = fs::remove_file(&entry_path) {
                    println!("Error removing file {}: {}", entry_path.display(), e);
                    continue;
                }
            }
        }

        for output_subdir in output_subdirs {
            if let Ok(de) = fs::read_dir(&output_subdir) {
                if de.count() > 0 {
                    continue;
                }

                println!("Removing empty directory {}", output_subdir.display());
                if let Err(e) = fs::remove_dir(&output_subdir) {
                    println!("Error removing empty dir {}: {}", output_dir.display(), e);
                }
            }
        }
    }

    println!("Finished. Caching results.");

    let _ = save_bake_cache(cache_file_path.as_path(), &cache);

    Ok(())
}
