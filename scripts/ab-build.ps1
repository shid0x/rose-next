<#
.SYNOPSIS
    Stage several client builds side by side in the launch folder and flip between them.

.DESCRIPTION
    For A/B testing a change against a baseline in the real game: build one side, stage it
    under a name, build the other, stage that, then flip between them without rebuilding.

    THE CONSTRAINT THAT SHAPES THIS SCRIPT: rosenext.exe imports znzin.dll BY NAME. The two
    files cannot be renamed to rosenext2.exe / znzin2.dll and launched -- rosenext2.exe
    would still load "znzin.dll", i.e. whichever engine happened to be in place. They must
    be swapped as a matched PAIR, which is why builds are stored in subfolders and copied
    over the canonical names rather than renamed.

    Mixing halves is a real failure, not a theoretical one: if one side adds engine exports
    the client calls, that client will not start against the other side's DLL.

        <game>\builds\<name>\   rosenext.exe + znzin.dll + BUILD_INFO.txt
                    |
                    v
        <game>\rosenext.exe + znzin.dll     <- active pair, what the game runs

.PARAMETER GameDir
    Launch folder. Defaults to $env:ROSE_GAME_DIR, else C:\Users\Thomas\Desktop\ROSEProject.

.EXAMPLE
    # Typical session: stage a baseline, then the change under test.
    git checkout master;       just build release
    .\scripts\ab-build.ps1 stage baseline
    git checkout my-branch;    just build release
    .\scripts\ab-build.ps1 stage candidate

    .\scripts\ab-build.ps1 list
    .\scripts\ab-build.ps1 use baseline
    .\scripts\ab-build.ps1 toggle        # flip (needs exactly two staged)
    .\scripts\ab-build.ps1 clean         # remove all staged builds + marker

.NOTES
    ALSO LABEL THE BUILD ON SCREEN. Two builds with identical instrumentation are
    indistinguishable in a screenshot, and a mis-flip silently produces a wrong
    measurement. Add a temporary debug-HUD line to each side, e.g.

        "Build: BASELINE"   /   "Build: CANDIDATE"

    so every screenshot is self-labelling. Remove it when the comparison is over.

    AND MEASURE WITH VSYNC OFF ([VIDEO] VSYNC=0 in rose-next.ini). With vsync on,
    presentation quantizes to 60/30/20 fps, so a steady "38 fps" is a mix of 60 Hz and
    30 Hz frames rather than a frame time, and differences below one vsync interval are
    invisible. This hid three consecutive results during the znzin investigation.
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('stage', 'use', 'toggle', 'list', 'clean')]
    [string]$Command = 'list',

    [Parameter(Position = 1)]
    [string]$Name,

    [string]$GameDir,
    [string]$BinDir
)

$ErrorActionPreference = 'Stop'

if (-not $GameDir) {
    $GameDir = if ($env:ROSE_GAME_DIR) { $env:ROSE_GAME_DIR } else { 'C:\Users\Thomas\Desktop\ROSEProject' }
}
if (-not $BinDir) {
    $BinDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'bin\release'
}

$BuildsDir = Join-Path $GameDir 'builds'
$Marker    = Join-Path $GameDir 'ACTIVE_BUILD.txt'

# The pair that must move together. Everything else in the launch folder is shared.
$Files = @('rosenext.exe', 'znzin.dll')

if (-not (Test-Path $GameDir)) { throw "Launch folder not found: $GameDir" }

function Get-Active {
    if (Test-Path $Marker) { (Get-Content $Marker -First 1).Trim() } else { $null }
}

function Get-Staged {
    if (-not (Test-Path $BuildsDir)) { return @() }
    @(Get-ChildItem $BuildsDir -Directory | Select-Object -ExpandProperty Name)
}

function Show-List {
    $active = Get-Active
    $staged = Get-Staged
    Write-Host ''
    if (-not $staged) {
        Write-Host "  No builds staged in $BuildsDir" -ForegroundColor DarkGray
        Write-Host "  Stage one with:  .\scripts\ab-build.ps1 stage <name>" -ForegroundColor DarkGray
    } else {
        Write-Host "  Launch folder: $GameDir" -ForegroundColor DarkGray
        foreach ($n in $staged) {
            $info = Join-Path (Join-Path $BuildsDir $n) 'BUILD_INFO.txt'
            $desc = if (Test-Path $info) { (Get-Content $info -First 1).Trim() } else { '' }
            $mark = if ($n -eq $active) { '*' } else { ' ' }
            Write-Host ("   {0} {1,-14} {2}" -f $mark, $n, $desc)
        }
    }
    Write-Host ''
}

function Copy-Pair([string]$from, [string]$to) {
    foreach ($f in $Files) {
        $src = Join-Path $from $f
        if (-not (Test-Path $src)) { throw "Missing $f in $from" }
        Copy-Item $src (Join-Path $to $f) -Force
    }
}

function Assert-NotRunning {
    $p = Get-Process -Name 'rosenext' -ErrorAction SilentlyContinue
    if ($p) {
        # Copying over a loaded exe fails partway and can leave a half-swapped pair --
        # the one state that produces silently wrong results.
        throw "rosenext.exe is running (PID $($p.Id -join ', ')). Close the client first."
    }
}

switch ($Command) {

    'stage' {
        if (-not $Name) { throw "Usage: ab-build.ps1 stage <name>" }
        if (-not (Test-Path $BinDir)) { throw "Build output not found: $BinDir" }
        $dest = Join-Path $BuildsDir $Name
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Pair $BinDir $dest

        $desc = try {
            $repo = Split-Path $PSScriptRoot -Parent
            "$(git -C $repo rev-parse --abbrev-ref HEAD) @ $(git -C $repo rev-parse --short HEAD)"
        } catch { "staged $(Get-Date -Format 'yyyy-MM-dd HH:mm')" }
        $desc | Set-Content (Join-Path $dest 'BUILD_INFO.txt')

        Write-Host ''
        Write-Host "  Staged '$Name'  " -NoNewline; Write-Host $desc -ForegroundColor Cyan
        Write-Host ''
    }

    'use' {
        if (-not $Name) { throw "Usage: ab-build.ps1 use <name>" }
        $src = Join-Path $BuildsDir $Name
        if (-not (Test-Path $src)) { throw "Build '$Name' is not staged. Try: ab-build.ps1 list" }
        Assert-NotRunning
        Copy-Pair $src $GameDir
        $Name | Set-Content $Marker
        Write-Host ''
        Write-Host "  Active: " -NoNewline; Write-Host $Name.ToUpper() -ForegroundColor Green
        Write-Host ''
    }

    'toggle' {
        $staged = Get-Staged
        if ($staged.Count -ne 2) {
            throw "toggle needs exactly two staged builds (found $($staged.Count)). Use: ab-build.ps1 use <name>"
        }
        $active = Get-Active
        $target = if ($active -eq $staged[0]) { $staged[1] } else { $staged[0] }
        & $PSCommandPath -Command use -Name $target -GameDir $GameDir -BinDir $BinDir
    }

    'list' { Show-List }

    'clean' {
        Assert-NotRunning
        if (Test-Path $BuildsDir) { Remove-Item $BuildsDir -Recurse -Force }
        if (Test-Path $Marker)    { Remove-Item $Marker -Force }
        Write-Host ''
        Write-Host "  Removed staged builds and marker from $GameDir" -ForegroundColor Yellow
        Write-Host "  The active rosenext.exe / znzin.dll were left in place." -ForegroundColor DarkGray
        Write-Host ''
    }
}
