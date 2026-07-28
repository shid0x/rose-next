param (
    [ValidateSet("release", "debug")]
    [string]$config = "release"
)

$root = (Join-Path $PSScriptRoot ..)
$discord_dll = (Join-Path $root "thirdparty" "discord-2.5.6" "lib" "x86" "discord_game_sdk.dll")
# D3DX9 is linked against d3dx9_43.dll (June 2010 redist) rather than the old static lib,
# because legacy D3DX allocates ID3DXFont's glyph cache in D3DPOOL_MANAGED, which a
# D3D9Ex device rejects -- no text renders. See doc/d3d9ex-migration.md.
$d3dx9_dll = (Join-Path $root "thirdparty" "directx9" "bin" "x86" "d3dx9_43.dll")
$bin_dir = (Join-Path $root "bin" $config)

$bin_relative = (Resolve-Path $bin_dir -Relative)

foreach ($dll in @($discord_dll, $d3dx9_dll)) {
    $dll_relative = (Resolve-Path -Path $dll -Relative)
    Write-Host "Copying $dll_relative => $bin_relative"
    xcopy "$dll" "$bin_dir" /S /Y /Q | find /v "File(s) copied"
}