#![windows_subsystem = "windows"]

use std::env;
use std::fs;
use std::fs::File;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process;

extern crate native_windows_derive as nwd;
extern crate native_windows_gui as nwg;

use nwd::NwgUi;
use nwg::stretch::geometry::Size;
use nwg::stretch::style::{Dimension, FlexDirection};
use nwg::NativeUi;
use winapi::shared::windef::RECT;
use winapi::um::winuser::{GetClientRect, GetDesktopWindow};

use launcher::error::LauncherError;
use launcher::manifest::Manifest;
use launcher::util::get_exe_dir;

const DEFAULT_URL: &str = "https://rosenext.com/public/latest";
const DEFAULT_MANIFEST_NAME: &str = "manifest.json";
const DEFAULT_VERSION_FILE_NAME: &str = "version";
const DEFAULT_CLIENT_EXE_NAME: &str = "rosenext";

#[derive(Default, NwgUi)]
pub struct LauncherApp {
    #[nwg_control(size: (400, 500), position: (0, 0), title: "ROSE Next Launcher", flags: "WINDOW|VISIBLE")]
    #[nwg_events( OnWindowClose: [LauncherApp::say_goodbye] )]
    window: nwg::Window,

    #[nwg_layout(parent: window, flex_direction: FlexDirection::Column)]
    main_layout: nwg::FlexboxLayout,

    #[nwg_control]
    #[nwg_layout_item(
        layout: main_layout,
        size: Size { width: Dimension::Points(380.0), height: Dimension::Points(100.0) },
    )]
    header: nwg::ImageFrame,

    #[nwg_control(text: "Button 1")]
    #[nwg_layout_item(
        layout: main_layout,
        size: Size { width: Dimension::Percent(0.25), height: Dimension::Percent(0.25) },
    )]
    button1: nwg::Button,

    #[nwg_control(text: "Button 2")]
    #[nwg_layout_item(
        layout: main_layout,
        size: Size { width: Dimension::Percent(1.0), height: Dimension::Percent(0.25) },
        //flex_grow: 2.0,
    )]
    button2: nwg::Button,

    #[nwg_control()]
    #[nwg_layout_item(
        layout: main_layout,
        size: Size { width: Dimension::Auto, height: Dimension::Points(20.0)},
        max_size: Size { width: Dimension::Undefined, height: Dimension::Points(20.0) },
    )]
    progress: nwg::ProgressBar,
    //#[nwg_control(text: "Hello")]
    //status_bar: nwg::StatusBar,
}

impl LauncherApp {
    fn say_goodbye(&self) {
        nwg::simple_message("Goodbye", "Goodbye");
        nwg::stop_thread_dispatch();
    }
}

struct LauncherArgs {
    // URL of remote archive
    url: reqwest::Url,
    // Relative path for location of manifest file at URL
    manifest: PathBuf,
    // Path to local file containing the current version number
    version_file: PathBuf,
    // Path to client exe to launch after update
    client: PathBuf,
    // Forces an update regardless of version checks
    force_update: bool,
    // Skips updating the launcher
    skip_launcher_update: bool,
}

/// Given the current launcher return a path for the "old launcher" created
/// when the launcher needed to update itself
fn old_launcher_path(path: &Path) -> PathBuf {
    PathBuf::from(path).with_extension("old")
}

/// Removes the old launcher file that was left over during the update process if it still exists
fn remove_old_launcher(current_launcher_path: &Path) -> Result<(), LauncherError> {
    let old_launcher_path = old_launcher_path(&current_launcher_path);

    if old_launcher_path.exists() {
        fs::remove_file(&old_launcher_path)?;
    }
    Ok(())
}

/// Downloads the manifest file from the specified URL
async fn download_manifest(url: &reqwest::Url) -> Result<Manifest, LauncherError> {
    let res: Manifest = reqwest::get(url.as_str())
        .await
        .map_err(|e| {
            LauncherError::ManifestError(format!("Failed to get manifest: {}", e.to_string()))
        })?
        .json()
        .await
        .map_err(|e| {
            LauncherError::ManifestError(format!("Invalid manifest: {}", e.to_string()))
        })?;

    Ok(res)
}

/// Compares the remote manifest version to the local version file to determine
/// if an update is required to the client files.
fn is_update_required(
    remote_manifest: &Manifest,
    version_file_path: &Path,
) -> Result<bool, LauncherError> {
    if !version_file_path.exists() {
        return Err(LauncherError::VersionFileError(format!(
            "Version file does not exist at {}",
            version_file_path.display()
        )));
    }

    let bytes = fs::read(version_file_path).map_err(|e| {
        LauncherError::VersionFileError(format!("Error getting current version: {}", e))
    })?;

    let local_version: u32 = String::from_utf8_lossy(&bytes).parse().map_err(|e| {
        LauncherError::VersionFileError(format!("Error parsing current version: {}", e))
    })?;

    Ok(local_version < remote_manifest.version)
}

/// Checks if the launcher needs to be updated by comparing its hash
fn is_launcher_update_required(
    manifest: &Manifest,
    launcher_path: &Path,
) -> Result<bool, LauncherError> {
    let mut exe_file = File::open(&launcher_path)?;

    let mut exe_bytes = Vec::new();
    let _bytes_read = exe_file.read_to_end(&mut exe_bytes)?;

    let local_hash = blake3::hash(&exe_bytes).to_hex();
    Ok(local_hash.as_str() != manifest.launcher.checksum.as_str())
}

/// Downloads a file from the specified URL to the destination path
async fn download_file(url: &reqwest::Url, dest: &Path) -> Result<(), LauncherError> {
    let mut res = reqwest::get(url.as_str()).await?;
    let mut file = File::create(dest)?;
    while let Some(chunk) = res.chunk().await? {
        file.write_all(&chunk)?;
    }
    Ok(())
}

async fn update(args: &LauncherArgs) -> std::result::Result<(), LauncherError> {
    let launcher_path = env::current_exe()?;
    remove_old_launcher(&launcher_path)?;

    let manifest_url = args
        .url
        .join(&args.manifest.to_string_lossy())
        .expect("Failed to parse default manifest url");

    let manifest = download_manifest(&manifest_url).await?;

    // Always perform an update even if there are errors with the version
    // file unless we can successfully confirm an update is not required.
    // TODO: Log the error
    let need_update = match is_update_required(&manifest, &args.version_file) {
        Ok(v) => v,
        Err(_) => true,
    };

    if args.force_update || need_update {
        let cur_exe = env::current_exe()?;
        if !args.skip_launcher_update && is_launcher_update_required(&manifest, &cur_exe)? {
            fs::rename(&launcher_path, old_launcher_path(&launcher_path))?;

            let launcher_url = args
                .url
                .join(
                    manifest
                        .launcher
                        .path
                        .into_os_string()
                        .to_string_lossy()
                        .to_mut()
                        .as_str(),
                )
                .map_err(|e| {
                    LauncherError::LauncherUpdateError(format!("Error getting launcher URL: {}", e))
                })?;

            download_file(&launcher_url, &launcher_path).await?;

            // TODO: Start new launcher with `--skip-launcher-update` flag + `--no-launch` flag
            // TODO: Quit update
        }
        // TODO: Client update (CALL BITAR BABY)
    }
    Ok(())
}

#[tokio::main]
async fn main() {
    let exe_dir = get_exe_dir();

    let default_url = reqwest::Url::parse(DEFAULT_URL).expect("Failed to parse default url");
    let default_local_manifest_path = PathBuf::from(DEFAULT_MANIFEST_NAME);
    let default_version_file_path = exe_dir.join(DEFAULT_VERSION_FILE_NAME);
    let default_client_path = exe_dir
        .join(DEFAULT_CLIENT_EXE_NAME)
        .with_extension(env::consts::EXE_EXTENSION);

    let mut args = pico_args::Arguments::from_env();
    let args = LauncherArgs {
        url: args.value_from_str("--url").unwrap_or(default_url),
        manifest: args
            .value_from_str("--manifest")
            .unwrap_or(default_local_manifest_path),
        version_file: args
            .value_from_str("--version-file")
            .unwrap_or(default_version_file_path),
        client: args
            .value_from_str("--client")
            .unwrap_or(default_client_path),
        force_update: args.contains("--force-update"),
        skip_launcher_update: args.contains("--skip-launcher-update"),
    };

    if cfg!(windows) {
        nwg::init().expect("Failed to init Native Windows GUI");
        nwg::Font::set_global_family("Segoe UI").expect("Failed to set default font");
        let app = LauncherApp::build_ui(Default::default()).expect("Failed to build UI");

        // Center in scree
        let mut rect = RECT::default();
        let res = unsafe {
            GetClientRect(GetDesktopWindow(), &mut rect);
        };
        let (width, height) = app.window.size();
        let x = (rect.right - width as i32) / 2;
        let y = (rect.bottom - height as i32) / 2;
        app.window.set_position(x, y);

        // Add header image
        let header_raw = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/res/header.bmp"));
        let mut header_bitmap = nwg::Bitmap::default();
        nwg::Bitmap::builder()
            .source_bin(Some(header_raw))
            .strict(true)
            .build(&mut header_bitmap)
            .expect("Failed to build header bitmap");
        app.header.set_bitmap(Some(&header_bitmap));
        nwg::dispatch_thread_events();
    } else {
        if let Err(e) = update(&args).await {
            eprintln!("{}", e);
        }
        // TODO: Don't run this unless update passes?
        process::Command::new(args.client).spawn().expect("FAILO");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::io::Write;

    use actix_rt;
    use actix_web::dev::Body;
    use actix_web::{test, web, App, HttpResponse};
    use tempfile::NamedTempFile;

    #[test]
    fn test_remove_old_launcher() {
        let launcher = tempfile::Builder::new()
            .suffix(env::consts::EXE_EXTENSION)
            .tempfile()
            .unwrap();

        let old_launcher_path = old_launcher_path(launcher.path());
        assert_eq!(
            old_launcher_path.extension().unwrap().to_str().unwrap(),
            "old"
        );

        assert_eq!(launcher.path().parent(), old_launcher_path.parent());
        assert_eq!(launcher.path().file_stem(), old_launcher_path.file_stem());

        let _ = fs::File::create(&old_launcher_path).unwrap();
        assert!(old_launcher_path.exists());

        remove_old_launcher(&old_launcher_path).unwrap();
        assert!(!old_launcher_path.exists());
    }

    #[actix_rt::test]
    async fn test_manifest() {
        let manifest_rep = || -> HttpResponse {
            let mut m = Manifest::default();
            m.version = 999;
            HttpResponse::Ok().json(m)
        };

        let server =
            test::start(move || App::new().route("/manifest.json", web::get().to(manifest_rep)));

        let manifest_url =
            reqwest::Url::parse(&format!("http://{}/manifest.json", server.addr())).unwrap();
        let manifest = download_manifest(&manifest_url).await.unwrap();

        assert_eq!(manifest.version, 999);
    }

    #[test]
    fn test_is_update_required() {
        let mut manifest = Manifest::default();
        manifest.version = 100;

        // -- Test is update required
        let mut version_file_lower = NamedTempFile::new().unwrap();
        version_file_lower.write_all(b"1").unwrap();

        let mut version_file_equal = NamedTempFile::new().unwrap();
        version_file_equal.write_all(b"100").unwrap();

        let mut version_file_higher = NamedTempFile::new().unwrap();
        version_file_higher.write_all(b"500").unwrap();

        assert!(is_update_required(&manifest, &version_file_lower.path()).unwrap());
        assert!(!is_update_required(&manifest, &version_file_equal.path()).unwrap());
        assert!(!is_update_required(&manifest, &version_file_higher.path()).unwrap());
    }

    #[test]
    fn test_is_launcher_update_required() {
        let mut manifest = Manifest::default();
        let temp = tempfile::NamedTempFile::new().unwrap();

        let bytes = [15; 1000];
        temp.as_file().write_all(&bytes).unwrap();

        manifest.launcher.checksum = blake3::hash(&bytes).to_hex().as_str().to_owned();
        let need_update = is_launcher_update_required(&manifest, temp.path()).unwrap();
        assert_eq!(need_update, false);

        manifest.launcher.checksum = hex::encode([0; 32]);
        let need_update = is_launcher_update_required(&manifest, temp.path()).unwrap();
        assert_eq!(need_update, true);
    }

    #[actix_rt::test]
    async fn test_download_file() {
        let test_data = "test string";
        let mut remote_file = tempfile::NamedTempFile::new().unwrap();
        remote_file.write_all(test_data.as_bytes()).unwrap();

        let test_file_rep = || -> HttpResponse {
            let test_data = "test string";
            HttpResponse::Ok().body(Body::from_slice(test_data.as_bytes()))
        };

        let server =
            test::start(move || App::new().route("/test_file", web::get().to(test_file_rep)));

        let test_file_url =
            reqwest::Url::parse(&format!("http://{}/test_file", server.addr())).unwrap();

        let local_file = NamedTempFile::new().unwrap();
        let local_file_path = local_file.path().to_path_buf();
        local_file.close().unwrap();

        download_file(&test_file_url, &local_file_path)
            .await
            .unwrap();

        let mut local_file = fs::File::open(local_file_path).unwrap();
        let mut local_string = String::new();

        local_file.read_to_string(&mut local_string).unwrap();
        assert_eq!(test_data, local_string);
    }
}
