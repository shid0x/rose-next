# Sets up the local dev environment for client and server. A dev/ directory is
# created at the root directory which links to the built assets

function symlink_children {
    param (
        [ValidateScript( { Test-Path $_ })]
        [string]$src,

        [string]$dst
    )

    $children = Get-ChildItem -Path $src
    Foreach ($child in $children) {
        $leaf = Split-Path -Path $child -Leaf
        $out = (Join-Path $dst $leaf)
        if (Test-Path -Path $child -PathType Container) {
            New-Item -ItemType Junction -Path $out -Target $child
        }
        else {
            New-Item -ItemType HardLink -Path $out -Target $child
        }
    }
}

$rose_next_root = (Get-Item $PSScriptRoot).Parent
$asset_dir_debug = (Join-Path $rose_next_root "bin" "assets" "debug")
$asset_dir_release = (Join-Path $rose_next_root "bin" "assets" "debug")

# Client links
$client_dir_debug = (Join-Path $rose_next_root "dev" "client" "debug")
$client_dir_release = (Join-Path $rose_next_root "dev" "client", "release")
Write-Host "Setting up dev clients:"

Write-Host "Linking $asset_dir_debug => $client_dir_debug"
symlink_children -src $asset_dir_debug -dst $client_dir_debug

Write-Host "Linking $asset_dir_release => $client_dir_release"
symlink_children -src $asset_dir_release -dst $client_dir_release


# Server links
$server_dir = (Join-Path $rose_next_root "dev" "server")
$asset_3ddata = (Join-Path $asset_dir_debug "3ddata")
$server_3ddata = (Join-Path $server_dir "data" "3ddata")

Write-Host "Setting up dev server: Linking $asset_dir_debug => $server_dir"
New-Item -ItemType Junction -Path $server_3ddata -Target $asset_3ddata

$setting_example = (Join-Path $rose_next_root "doc" "server.toml.example")
$setting_server = (Join-Path $server_dir "server.toml.example")
Copy-Item -Path $setting_example -Destination $setting_server