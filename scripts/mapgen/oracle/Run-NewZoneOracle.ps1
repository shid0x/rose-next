# Compile and run the editor File > New oracle (see NewZoneOracle.cs).
# References the deployed editor, data\Map Editor.exe -- the binary the user
# actually runs -- so the oracle tests that build, not a fresh compile.
#
# -LoadDir <zone folder> instead runs the oracle's "load" mode: every map file
# in that folder is read with the editor's own readers.
param(
    [string]$OutDir,
    [string]$LoadDir,
    [int]$SizeX = 2,
    [int]$SizeY = 2,
    [string]$TileFile = '3DDATA\MAPS\JUNON\JG01\JG01.ZON',
    [string]$ZoneFile = 'ORACLE.ZON'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$data = Join-Path $repo 'data'
$work = Join-Path $repo 'build\mapgen\oracle-bin'
New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item (Join-Path $data 'Map Editor.exe') $work -Force
$xna = "$env:WINDIR\assembly\GAC_32"
$rsp = Join-Path $work 'oracle.rsp'
@"
/nologo
/target:exe
/platform:x86
/out:"$work\NewZoneOracle.exe"
/r:"$work\Map Editor.exe"
/r:"$xna\Microsoft.Xna.Framework\3.1.0.0__6d5c3888ef60e27d\Microsoft.Xna.Framework.dll"
/r:System.Core.dll
"$PSScriptRoot\NewZoneOracle.cs"
"@ | Set-Content $rsp
& "$env:WINDIR\Microsoft.NET\Framework\v3.5\csc.exe" "@$rsp"
if ($LASTEXITCODE -ne 0) { throw 'oracle compilation failed' }
if ($LoadDir) {
    Push-Location $data   # the editor's readers resolve nothing relative here, but match its working dir
    try { & "$work\NewZoneOracle.exe" load $LoadDir } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw 'editor readers failed on some files' }
    return
}
if (-not $OutDir) { throw 'give -OutDir or -LoadDir' }
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
& "$work\NewZoneOracle.exe" $data $OutDir $SizeX $SizeY $TileFile $ZoneFile
if ($LASTEXITCODE -ne 0) { throw 'oracle run failed' }
