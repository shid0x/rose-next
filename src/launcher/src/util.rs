use std::env;
use std::path::PathBuf;

pub fn get_exe_dir() -> PathBuf {
    env::current_exe()
        .unwrap_or_default()
        .parent()
        .expect("Failed to get exe directory")
        .to_path_buf()
}
