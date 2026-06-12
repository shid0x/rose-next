param (
    [ValidateSet("release", "debug")]
    [string]$config = "release"
)

$root = (Join-Path $PSScriptRoot ..)
$discord_dll = (Join-Path $root "thirdparty" "discord-2.5.6" "lib" "x86" "discord_game_sdk.dll")
$bin_dir = (Join-Path $root "bin" $config)

$discord_relative = (Resolve-Path -Path $discord_dll -Relative)
$bin_relative = (Resolve-Path $bin_dir -Relative)

Write-Host "Copying $discord_relative => $bin_relative"
xcopy "$discord_dll" "$bin_dir" /S /Y /Q | find /v "File(s) copied"