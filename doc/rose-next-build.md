# Rose Next Classic - Build Notes

## Build System
- Mix of Rust (i686) and C++ (VS2019, x86)
- Build script: `scripts/build.ps1` — builds thirdparty.sln, then Rust, then rose-next.sln
- Justfile available: `just build release`
- Cargo config at `src/.cargo/config` sets `target-dir="../bin"`
- Rust workspace in `src/Cargo.toml` with members: common-lib, pipeline

## Build Fixes Applied
1. `src/common-lib/build.rs`: Changed `env::var("DEBUG").is_ok()` to `env::var("PROFILE")` for flatc path detection
2. Rustup override: `stable-i686-pc-windows-msvc` set for `src/` directory
3. Added `ntdll.lib` to linker deps in client, loginserver, worldserver, gameserver vcxproj files

## Key Paths
- Release output: `bin/release/`
- Executables: rosenext.exe, sho_gameserver.exe, sho_loginserver.exe, sho_worldserver.exe, pipeline.exe
- Thirdparty output: `bin/release/thirdparty/`
