param (
    [string]$in = (Join-Path (Join-Path $PSScriptRoot "..") "data"),
    [string]$out = (Join-Path (Join-Path $PSScriptRoot "..") "Exes"),
    [string]$pipeline = (Join-Path (Join-Path (Join-Path $PSScriptRoot "..") "Exes") "pipeline.exe")
)

$ErrorActionPreference = "Stop"

$input_dir = (Resolve-Path $in).Path

if (!(Test-Path $pipeline -PathType Leaf)) {
    throw "pipeline.exe not found at $pipeline"
}

if (!(Test-Path $input_dir -PathType Container)) {
    throw "Input directory not found: $in"
}

if (!(Test-Path $out)) {
    New-Item -ItemType Directory -Path $out | Out-Null
}

$output_dir = (Resolve-Path $out).Path
$manifest_path = Join-Path $input_dir "pack.manifest"
$stdout_log = Join-Path $env:TEMP "rose-pack.stdout.log"
$stderr_log = Join-Path $env:TEMP "rose-pack.stderr.log"

try {
    Set-Content -Path $manifest_path -Value "# Temporary manifest for direct VFS packing from data/" -NoNewline

    Write-Host "Packing VFS from $input_dir to $output_dir"
    if (Test-Path $stdout_log) {
        Remove-Item -LiteralPath $stdout_log
    }

    if (Test-Path $stderr_log) {
        Remove-Item -LiteralPath $stderr_log
    }

    $process = Start-Process `
        -FilePath $pipeline `
        -ArgumentList @("pack", "-c", "pack.manifest", $input_dir, $output_dir) `
        -WorkingDirectory $input_dir `
        -RedirectStandardOutput $stdout_log `
        -RedirectStandardError $stderr_log `
        -Wait `
        -PassThru

    if ($process.ExitCode -ne 0) {
        if (Test-Path $stdout_log) {
            Get-Content -LiteralPath $stdout_log
        }

        if (Test-Path $stderr_log) {
            Get-Content -LiteralPath $stderr_log
        }

        throw "pipeline pack failed with exit code $($process.ExitCode)"
    }

    Write-Host "Created $(Join-Path $output_dir 'data.idx')"
    Write-Host "Created $(Join-Path $output_dir 'rose.vfs')"
}
finally {
    Remove-Item -LiteralPath $manifest_path -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stdout_log -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderr_log -ErrorAction SilentlyContinue
}
