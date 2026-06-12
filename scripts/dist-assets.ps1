param (
    # Directroy to assets
    [Parameter(Mandatory = $true)]
    [ValidateScript( { Test-Path $_ })]
    [string]$assets_dir,

    # Directory for asset build dir
    [Parameter(Mandatory = $true)]
    [string]$build_dir = (Join-Path $PSScriptRoot .. "build/client"),

    [switch]$noclient = $false,
    [switch]$nopack = $false,
    [string]$client_out = (Join-Path $PSScriptRoot .. "dist/client"),
    [switch]$noserver = $false,
    [string]$server_out = (Join-Path $PSScriptRoot .. "dist/server/data")
)

if ($noclient -eq $false) {
    # TODO: Call pipeline bake, pack, etc.
}

if ($noserver -eq $false) {
    # xcopy is sensitive to slashes
    $build_dir = $build_dir -replace '/', '\'
    $server_out = $server_out -replace '/', '\'
    Write-Host "Copy asset files for server to $server_out"

    xcopy "$build_dir\3ddata\npc\*.chr" "$server_out\3ddata\npc\" /S /Y /D
    xcopy "$build_dir\3ddata\npc\*.zsc" "$server_out\3ddata\npc\" /S /Y /D
    xcopy "$build_dir\3ddata\npc\*.zmo" "$server_out\3ddata\npc\" /S /Y /D
    xcopy "$build_dir\3ddata\mdata\*.zmo" "$server_out\3ddata\mdata\" /S /Y /D
    xcopy "$build_dir\3ddata\pat\*.zmo" "$server_out\3ddata\pat\" /S /Y /D
    xcopy "$build_dir\3ddata\questdata\*.qsd" "$server_out\3ddata\questdata\" /S /Y /D
    xcopy "$build_dir\3ddata\questdata\*.stb" "$server_out\3ddata\questdata\" /S /Y /D
    xcopy "$build_dir\3ddata\motion\*.zmo" "$server_out\3ddata\motion\" /S /Y /D
    xcopy "$build_dir\3ddata\maps\*.him" "$server_out\3ddata\maps\" /S /Y /D
    xcopy "$build_dir\3ddata\maps\*.mov" "$server_out\3ddata\maps\" /S /Y /D
    xcopy "$build_dir\3ddata\maps\*.ifo" "$server_out\3ddata\maps\" /S /Y /D
    xcopy "$build_dir\3ddata\maps\*.zon" "$server_out\3ddata\maps\" /S /Y /D
    xcopy "$build_dir\3ddata\effect\*.zmo" "$server_out\3ddata\effect\" /S /Y /D
    xcopy "$build_dir\3ddata\ai\*.aip" "$server_out\3ddata\ai\" /S /Y /D
    xcopy "$build_dir\3ddata\ai\*.stb" "$server_out\3ddata\ai\" /S /Y /D
    xcopy "$build_dir\3ddata\stb\*.stb" "$server_out\3ddata\stb\" /S /Y /D
    xcopy "$build_dir\3ddata\stb\*.stl" "$server_out\language\" /S /Y /D
    xcopy "$build_dir\3ddata\stb\list_quest_s.stl" "$server_out\3ddata\questdata\" /S /Y /D
}