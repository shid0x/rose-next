<#
.SYNOPSIS
    Evict the Windows standby list so the next client run reads from disk, not RAM.

.DESCRIPTION
    WHY

    Cache warming ([VIDEO] CACHE_WARM_MB) pulls a zone's assets into the OS page
    cache during the loading screen so the first in-game access costs no disk
    read. Measuring whether that helps requires a *cold* cache -- and once a run
    has happened, every file it touched is resident, so a second run reads from
    RAM whether or not warming is enabled. An A/B without an eviction step
    returns "no difference" no matter how well the feature works.

    Rebooting achieves this and is what we did first. This does the same thing in
    about a second.

    Windows keeps file data it is no longer actively using on the *standby list*:
    still in RAM, still instantly reusable, counted as "available". Purging it is
    exactly what Sysinternals RAMMap's "Empty Standby List" does, via an
    undocumented NtSetSystemInformation command. This script issues the same
    command directly, so there is nothing to download.

    Two commands are sent, in order:
      MemoryFlushModifiedList (3) -- writes dirty pages out so they *can* be
                                     purged; without it they stay resident.
      MemoryPurgeStandbyList  (4) -- drops the standby list itself.

    HOW TO USE IT

        pwsh scripts/purge-page-cache.ps1          # purge, report the delta
        pwsh scripts/purge-page-cache.ps1 -WhatIf  # just report, change nothing

    Requires elevation (the privilege involved is SeProfileSingleProcessPrivilege,
    which only administrators hold). It re-launches itself elevated if needed.

    VERIFYING IT WORKED

    It prints standby bytes before and after, read from
    Win32_PerfFormattedData_PerfOS_Memory -- CIM property names, not localised
    performance-counter strings, so this reads correctly on a non-English
    Windows. A purge that silently does nothing looks identical to a successful
    one, which is the failure mode this whole project keeps running into, so the
    delta is the point. Expect the standby figure to drop to near zero.

    Close the client first. Purging while it runs evicts pages it is actively
    using, which measures nothing useful and makes the session stutter.

.PARAMETER WhatIf
    Report the standby list size and exit without purging.

.PARAMETER LogFile
    Internal. Set on the elevated re-launch so the child's output can be shown
    by the parent -- an elevated Start-Process cannot have its stdout
    redirected, so without this the child's window closes and any error with it.
#>

[CmdletBinding()]
param(
    [switch]$WhatIf,
    [string]$LogFile
)

$ErrorActionPreference = 'Stop'

if ($LogFile) {
    try { Start-Transcript -Path $LogFile -Force | Out-Null } catch {}
}

function Get-StandbyBytes {
    # These are CIM property names and therefore locale-independent, unlike the
    # "\Memory\Standby Cache Normal Priority Bytes" counter path.
    $m = Get-CimInstance -ClassName Win32_PerfFormattedData_PerfOS_Memory
    [PSCustomObject]@{
        Standby   = [uint64]$m.StandbyCacheCoreBytes +
                    [uint64]$m.StandbyCacheNormalPriorityBytes +
                    [uint64]$m.StandbyCacheReserveBytes
        Available = [uint64]$m.AvailableBytes
        Cache     = [uint64]$m.CacheBytes
    }
}

function Format-GB([uint64]$bytes) {
    '{0,7:N2} GB' -f ($bytes / 1GB)
}

$running = Get-Process -Name rosenext -ErrorAction SilentlyContinue
if ($running) {
    Write-Warning ("rosenext.exe is running (PID {0}). Purging now evicts pages " -f ($running.Id -join ', ') +
                   "it is actively using, which measures nothing useful. Close it first.")
}

$before = Get-StandbyBytes
Write-Host ''
Write-Host 'Before:'
Write-Host ("  standby list   {0}" -f (Format-GB $before.Standby))
Write-Host ("  available      {0}" -f (Format-GB $before.Available))

if ($WhatIf) {
    Write-Host ''
    Write-Host 'WhatIf: nothing purged.'
    return
}

$isAdmin = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()
).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host ''
    Write-Host 'Elevation required -- re-launching as administrator (accept the UAC prompt).'
    $exe = (Get-Process -Id $PID).Path
    $childLog = Join-Path ([IO.Path]::GetTempPath()) ("purge-page-cache-{0}.log" -f [guid]::NewGuid())
    try {
        $p = Start-Process -FilePath $exe `
            -ArgumentList @('-NoProfile', '-File', $PSCommandPath, '-LogFile', $childLog) `
            -Verb RunAs -PassThru -Wait -WindowStyle Hidden
    } catch {
        Write-Host ''
        Write-Error ("Elevation was declined or failed, so nothing was purged. " +
                     "The cache is still WARM -- do not treat the next run as cold. ($_)")
        exit 1
    }
    # An elevated process cannot have its stdout redirected, so it logs to a
    # file. Without this the window closes and takes the error with it, which
    # is indistinguishable from a purge that quietly did nothing.
    if (Test-Path $childLog) {
        Get-Content $childLog |
            Where-Object { $_ -notmatch '^\*{10}' -and $_ -notmatch '^(Transcript|Windows PowerShell transcript|Host Application|Process ID|PSVersion|PSEdition|PSCompatibleVersions|BuildVersion|CLRVersion|WSManStackVersion|PSRemotingProtocolVersion|SerializationVersion|Machine|Username|RunAs User|Config|Start time|End time)' } |
            ForEach-Object { $_ }
        Remove-Item $childLog -Force -ErrorAction SilentlyContinue
    } else {
        Write-Warning "The elevated run produced no log; assume nothing was purged."
    }
    exit $p.ExitCode
}

if (-not ('RoseNext.PageCache' -as [type])) {
    Add-Type -Namespace RoseNext -Name PageCache -MemberDefinition @'
// Pack = 1 is mandatory, not a micro-optimisation. Native TOKEN_PRIVILEGES is
// { DWORD count; LUID luid; DWORD attributes; } where LUID is two DWORDs and so
// only 4-byte aligned -- total 16 bytes, attributes at offset 12. Declaring
// Luid as a long gives it 8-byte alignment under default packing, which pushes
// attributes to offset 16 and grows the struct to 24. AdjustTokenPrivileges
// then reads the attributes field out of padding, finds 0 instead of
// SE_PRIVILEGE_ENABLED, enables nothing, and reports success -- the failure
// surfaces later as ERROR_NOT_ALL_ASSIGNED (1300) on a privilege the token
// demonstrably holds.
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct TokPriv1Luid { public int Count; public long Luid; public int Attr; }

[DllImport("ntdll.dll")]
public static extern int NtSetSystemInformation(int InfoClass, IntPtr Info, int Length);

[DllImport("advapi32.dll", SetLastError = true)]
public static extern bool OpenProcessToken(IntPtr h, int acc, out IntPtr tok);

[DllImport("advapi32.dll", SetLastError = true)]
public static extern bool LookupPrivilegeValue(string host, string name, out long luid);

[DllImport("advapi32.dll", SetLastError = true)]
public static extern bool AdjustTokenPrivileges(IntPtr tok, bool disall,
    ref TokPriv1Luid newst, int len, IntPtr prev, IntPtr rel);

[DllImport("kernel32.dll")]
public static extern IntPtr GetCurrentProcess();

[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool CloseHandle(IntPtr h);

// Enable SeProfileSingleProcessPrivilege, without which the purge below
// returns STATUS_PRIVILEGE_NOT_HELD (0xC0000061).
public static void EnableProfilePrivilege()
{
    const int TOKEN_ADJUST_PRIVILEGES = 0x20, TOKEN_QUERY = 0x8;
    const int SE_PRIVILEGE_ENABLED = 0x2;
    IntPtr tok;
    if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, out tok))
        throw new Exception("OpenProcessToken failed: " +
            Marshal.GetLastWin32Error());
    try
    {
        TokPriv1Luid tp;
        tp.Count = 1;
        tp.Attr = SE_PRIVILEGE_ENABLED;
        if (!LookupPrivilegeValue(null, "SeProfileSingleProcessPrivilege", out tp.Luid))
            throw new Exception("LookupPrivilegeValue failed: " +
                Marshal.GetLastWin32Error());
        if (!AdjustTokenPrivileges(tok, false, ref tp,
                Marshal.SizeOf(typeof(TokPriv1Luid)), IntPtr.Zero, IntPtr.Zero))
            throw new Exception("AdjustTokenPrivileges failed: " +
                Marshal.GetLastWin32Error());
        // AdjustTokenPrivileges reports success even when it granted nothing.
        int err = Marshal.GetLastWin32Error();
        if (err != 0)
            throw new Exception(
                "SeProfileSingleProcessPrivilege was not granted (error " + err +
                "). Run elevated.");
    }
    finally { CloseHandle(tok); }
}

// SystemMemoryListInformation = 0x50.
public static int SendMemoryCommand(int command)
{
    IntPtr p = Marshal.AllocHGlobal(sizeof(int));
    try
    {
        Marshal.WriteInt32(p, command);
        return NtSetSystemInformation(0x50, p, sizeof(int));
    }
    finally { Marshal.FreeHGlobal(p); }
}
'@
    # No -UsingNamespace here: Add-Type -MemberDefinition already emits
    # "using System.Runtime.InteropServices;", and adding it again is CS0105,
    # which Add-Type treats as an error.
}

[RoseNext.PageCache]::EnableProfilePrivilege()

# 3 = MemoryFlushModifiedList, 4 = MemoryPurgeStandbyList.
# Flush first: dirty pages are not purgeable until they have been written out.
$steps = @(
    @{ Name = 'flush modified list'; Command = 3 },
    @{ Name = 'purge standby list';  Command = 4 }
)

Write-Host ''
foreach ($step in $steps) {
    $status = [RoseNext.PageCache]::SendMemoryCommand($step.Command)
    if ($status -ne 0) {
        throw ("{0} failed: NTSTATUS 0x{1:X8}" -f $step.Name, $status)
    }
    Write-Host ("  {0} ... ok" -f $step.Name)
}

Start-Sleep -Milliseconds 400
$after = Get-StandbyBytes

Write-Host ''
Write-Host 'After:'
Write-Host ("  standby list   {0}" -f (Format-GB $after.Standby))
Write-Host ("  available      {0}" -f (Format-GB $after.Available))

$freed = if ($after.Standby -lt $before.Standby) { $before.Standby - $after.Standby } else { [uint64]0 }
Write-Host ''
Write-Host ("Evicted {0} of file cache." -f (Format-GB $freed))

# A purge that changes nothing looks exactly like one that worked, so say so.
if ($freed -lt 100MB -and $before.Standby -gt 200MB) {
    Write-Warning ("The standby list barely moved. The command reported success, " +
                   "so treat the next run as WARM -- do not read it as an A/B result.")
}

if ($LogFile) {
    try { Stop-Transcript | Out-Null } catch {}
}
