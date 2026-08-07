<#
.SYNOPSIS
    Run the deployed client under cdb and capture a symbolized stack + minidump
    on the first crash.

.DESCRIPTION
    The client has no unhandled-exception filter and no minidump writer, so a
    crash leaves nothing behind: `error.txt` still ends with a clean
    "log: end." and the client log simply stops. This wraps the *deployed*
    rosenext.exe in cdb so the next crash produces a full stack.

    Symbols resolve because bin\<config>\rosenext.pdb / znzin.pdb match the
    deployed binaries byte-for-byte (verify with -VerifyOnly).

    Run the servers first (`just server-all release`), then this, then reproduce
    the crash. On the access violation cdb writes:
        <GameDir>\crash-<timestamp>.log   full log incl. !analyze -v and all thread stacks
        <GameDir>\crash-<timestamp>.dmp   full minidump

    Forces windowed mode for the run: breaking into a debugger from exclusive
    fullscreen can leave the display wedged. The original rose-next.ini is
    restored on exit (including Ctrl+C).

.EXAMPLE
    scripts\debug-client-crash.ps1
    scripts\debug-client-crash.ps1 -AutoConnect -Username me -Password pw -Character Foo
    scripts\debug-client-crash.ps1 -VerifyOnly
#>
[CmdletBinding()]
param(
    [string]$GameDir   = "$env:USERPROFILE\Desktop\ROSEProject",
    [string]$Config    = "release",
    [string]$Server    = "127.0.0.1",
    [switch]$AutoConnect,
    [string]$Username,
    [string]$Password,
    [string]$Character,
    [switch]$KeepFullscreen,
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
$repo    = Split-Path -Parent $PSScriptRoot
$symbols = Join-Path $repo "bin\$Config"
$exe     = Join-Path $GameDir 'rosenext.exe'
$ini     = Join-Path $GameDir 'rose-next.ini'

foreach ($p in @($exe, $symbols)) {
    if (-not (Test-Path $p)) { throw "not found: $p" }
}

# --- symbols must match the deployed binaries or the stack is useless --------
$mismatch = $false
foreach ($pair in @(@('rosenext.exe', 'rosenext.pdb'), @('znzin.dll', 'znzin.pdb'))) {
    $deployed = Join-Path $GameDir $pair[0]
    $built    = Join-Path $symbols  $pair[0]
    $pdb      = Join-Path $symbols  $pair[1]
    if (-not (Test-Path $built) -or -not (Test-Path $pdb)) {
        Write-Warning "$($pair[0]): no build output in $symbols"; $mismatch = $true; continue
    }
    $a = (Get-FileHash $deployed -Algorithm MD5).Hash
    $b = (Get-FileHash $built    -Algorithm MD5).Hash
    if ($a -ne $b) {
        Write-Warning "$($pair[0]) in $GameDir does NOT match $built - stack will be wrong. Redeploy or rebuild."
        $mismatch = $true
    } else {
        Write-Host "  $($pair[0]): matches $($pair[1])" -ForegroundColor Green
    }
}
if ($VerifyOnly) { if ($mismatch) { exit 1 } else { Write-Host "symbols OK"; exit 0 } }
if ($mismatch) { throw "symbol mismatch - fix before debugging" }

$cdb = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe' -EA SilentlyContinue
if (-not $cdb) { throw "x86 cdb.exe not found (install Debugging Tools for Windows)" }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log   = Join-Path $GameDir "crash-$stamp.log"
$dmp   = Join-Path $GameDir "crash-$stamp.dmp"
$script= Join-Path $env:TEMP "rosenext-cdb-$stamp.txt"

# cdb runs these in order; `g` blocks until the target faults, then the rest run.
@"
.sympath+ $symbols
.reload /f
sxe av
g
.echo ================ FAULTING CONTEXT ================
.ecxr
.echo ================ ANALYZE =========================
!analyze -v
.echo ================ ALL THREAD STACKS ===============
~*kv
.echo ================ LOADED MODULES ==================
lm
.dump /ma $dmp
.logclose
qd
"@ | Set-Content -Encoding ASCII $script

# --- windowed for the run; always restore the ini ---------------------------
$iniBackup = $null
if (-not $KeepFullscreen -and (Test-Path $ini)) {
    $iniBackup = "$ini.debugbak"
    Copy-Item $ini $iniBackup -Force
    (Get-Content $ini) -replace '^FULLSCREEN=1', 'FULLSCREEN=0' | Set-Content -Encoding ASCII $ini
    Write-Host "  forced windowed mode (ini backed up)" -ForegroundColor Yellow
}

$args = @('--server', $Server)
if ($AutoConnect) {
    if (-not ($Username -and $Password -and $Character)) {
        throw "-AutoConnect needs -Username, -Password and -Character"
    }
    $args += @('--username', $Username, '--password', $Password,
               '--auto-connect-server', '1', '--auto-connect-channel', '1',
               '--auto-connect-character', $Character)
}

Write-Host "`nlaunching under cdb - reproduce the crash now (walk into map 65)." -ForegroundColor Cyan
Write-Host "  log:  $log"
Write-Host "  dump: $dmp`n"
# cdb has no working-directory switch and the debuggee inherits ours. The client
# resolves rose.vfs / data.idx / 3ddata / rose-next.ini and writes error.txt
# relative to the CWD, so launching from anywhere else makes it bail out early
# with no engine log at all - which looks exactly like "it didn't crash".
Push-Location $GameDir
try {
    & $cdb.FullName -y $symbols -logo $log -cf $script -o -g -G $exe @args
} finally {
    Pop-Location
    if ($iniBackup) { Move-Item $iniBackup $ini -Force; Write-Host "restored rose-next.ini" }
    Remove-Item $script -EA SilentlyContinue
}

if (Test-Path $log) {
    Write-Host "`n---- tail of $log ----" -ForegroundColor Cyan
    Get-Content $log -Tail 60
} else {
    Write-Warning "no log written - did the client exit cleanly?"
}
