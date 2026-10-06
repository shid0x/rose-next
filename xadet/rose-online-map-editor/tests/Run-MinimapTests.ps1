param(
    [string]$FreshPicture = '',   # optional: an editor-made minimap PNG to measure the DXT5 encoder on
    [string]$MSBuildPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$editor = Join-Path $repo 'xadet\rose-online-map-editor'
$testOutput = Join-Path $repo 'build\minimap-tests'
New-Item -ItemType Directory -Force $testOutput | Out-Null
& $MSBuildPath "$editor\Map Editor.sln" /p:Configuration=Release /p:Platform=x86 "/p:ReferencePath=$repo\data" /m:1 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Editor build failed.' }
Copy-Item "$editor\Map Editor\bin\x86\Release\Map Editor.exe" $testOutput -Force
$xnaRoot = "$env:WINDIR\assembly\GAC_32"
$wpfRoot = Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies\Microsoft\Framework\v3.0'
$responseFile = Join-Path $testOutput 'test.rsp'
@"
/nologo
/target:exe
/platform:x86
/out:"$testOutput\MinimapTests.exe"
/r:"$testOutput\Map Editor.exe"
/r:"$xnaRoot\Microsoft.Xna.Framework\3.1.0.0__6d5c3888ef60e27d\Microsoft.Xna.Framework.dll"
/r:"$xnaRoot\Microsoft.Xna.Framework.Game\3.1.0.0__6d5c3888ef60e27d\Microsoft.Xna.Framework.Game.dll"
/r:System.Core.dll
/r:System.Windows.Forms.dll
/r:System.Drawing.dll
/r:"$wpfRoot\PresentationFramework.dll"
/r:"$wpfRoot\PresentationCore.dll"
/r:"$wpfRoot\WindowsBase.dll"
"$PSScriptRoot\MinimapTests.cs"
"@ | Set-Content $responseFile
& "$env:WINDIR\Microsoft.NET\Framework\v3.5\csc.exe" "@$responseFile"
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
Push-Location $testOutput
try {
    $extra = @(); if ($FreshPicture) { $extra = @($FreshPicture) }
    & "$testOutput\MinimapTests.exe" "$repo\data" "$testOutput" @extra
    if ($LASTEXITCODE -ne 0) { throw 'Minimap tests failed.' }
} finally { Pop-Location }
