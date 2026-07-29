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

## DirectX
- **D3D9 core headers come from the Windows 10 SDK.** `d3d9.h` / `d3d9types.h` / `d3d9caps.h`
  were removed from `thirdparty/directx9/include/` so the SDK copies (which declare the
  9Ex interfaces) are picked up. MSVC searches every `/I` path before the system include
  dirs, so the vendored copies could not be beaten by reordering. A Windows 10 SDK is
  therefore required — `WindowsTargetPlatformVersion` was already `10.0`.
- **D3DX9 is the June 2010 redistributable**, linked as an import lib against
  `d3dx9_43.dll` (the old vendored `d3dx9.lib` was a 5.68 MB *static* lib predating 9Ex).
  The DLL lives at `thirdparty/directx9/bin/x86/d3dx9_43.dll`.
- `scripts/post-build.ps1` copies `d3dx9_43.dll` (and `discord_game_sdk.dll`) into
  `bin/<config>`; `scripts/dist.ps1` bundles them into the client distribution.
  **A run folder assembled by hand needs `d3dx9_43.dll` or the client will not start.**

See [d3d9ex-migration.md](d3d9ex-migration.md) for why.

## Key Paths
- Release output: `bin/release/`
- Executables: rosenext.exe, sho_gameserver.exe, sho_loginserver.exe, sho_worldserver.exe, pipeline.exe
- Thirdparty output: `bin/release/thirdparty/`
- Client runtime DLLs in `bin/release/`: znzin.dll, triggervfs.dll, discord_game_sdk.dll, d3dx9_43.dll
