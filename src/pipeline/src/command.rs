use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use globset::{Glob, GlobSetBuilder};

use crate::error::PipelineError;

pub struct Command {
    pub name: String,
    pub target: PathBuf,
    pub args: Vec<String>,
}

pub fn parse_command_file(path: &Path) -> Result<Vec<Command>, PipelineError> {
    let raw_config = fs::read_to_string(&path)?;
    let lines = raw_config
        .split('\n')
        .map(|line| line.trim().replace("\r", ""))
        .filter(|line| !line.starts_with('#') && !line.is_empty());

    let mut commands = Vec::new();
    for line in lines {
        let tokens: Vec<String> = line.split_whitespace().map(|s| s.to_string()).collect();
        if tokens.len() < 2 {
            continue;
        }

        commands.push(Command {
            name: tokens[0].to_lowercase(),
            target: PathBuf::from(&tokens[1]),
            args: tokens[2..].to_vec(),
        });
    }

    Ok(commands)
}

pub fn collect_command_globs(
    commands: &[Command],
) -> Result<HashMap<String, GlobSetBuilder>, globset::Error> {
    let mut globs = HashMap::new();

    for command in commands {
        if !globs.contains_key(&command.name) {
            globs.insert(command.name.clone(), GlobSetBuilder::new());
        }

        let glob = Glob::new(&command.target.to_string_lossy())?;
        globs.get_mut(&command.name).unwrap().add(glob);
    }

    Ok(globs)
}
