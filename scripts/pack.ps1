param (
    [string]$in = (Join-Path (Join-Path $PSScriptRoot "..") "data"),
    [string]$out = (Join-Path (Join-Path $PSScriptRoot "..") "Exes"),
    # Prefer the freshly built packer over the copy in Exes/. This default used to
    # be Exes/pipeline.exe unconditionally, and that copy silently went four
    # months stale: every bake ran a packer built before the 2 GB rollover existed,
    # which is why rose.vfs sailed past the split threshold with no rose_2.vfs and
    # the guard looked broken when it was simply not present in the binary. A tool
    # that is quietly out of date is worse than a missing one.
    [string]$pipeline = ""
)

$ErrorActionPreference = "Stop"

if (-not $pipeline) {
    $root = (Join-Path $PSScriptRoot "..")
    $built = (Join-Path (Join-Path (Join-Path $root "bin") "release") "pipeline.exe")
    $vendored = (Join-Path (Join-Path $root "Exes") "pipeline.exe")
    if (Test-Path $built -PathType Leaf) {
        $pipeline = $built
    } else {
        $pipeline = $vendored
        Write-Warning "Using $vendored -- build the pipeline (just build release) for the current packer."
    }
}

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
    # Development files that live in data/ on purpose but are not game data:
    # the map editor runs from data/ (its exe, pdb, ini, log, irrKlang dll,
    # XNA Content\ and ESTB\ tileset tables), scripts keep their sidecars
    # beside the tables they changed (*.json, read back by --restore), and the
    # quest editor its sources. The client reads none of them. pack.rs honours
    # "ignore <glob>" lines (globset: matched against the path relative to
    # data/, forward slashes, case-sensitive; `*` crosses folders; a glob cannot
    # contain a space, hence Map?Editor). The manifest excludes itself.
    $excludes = @(
        "pack.manifest",
        "Map?Editor.*", "*.dll", "*.pdb", "*.log", "*.md",
        "Content/**", "ESTB/**",
        "MinimapBackups/**",                 # the editor's Tools > Make minimap keeps replaced minimaps here
        "*.json", "*.py", "*.tmp",
        "3DDATA/MAPS/JUNON/AGIT01/TEMP/**"   # stray retail tile files the client never probes
    )
    $manifest = @("# Temporary manifest for direct VFS packing from data/ (scripts/pack.ps1)") +
                ($excludes | ForEach-Object { "ignore $_" })
    Set-Content -Path $manifest_path -Value ($manifest -join "`n") -NoNewline

    Write-Host "Packing VFS from $input_dir to $output_dir"
    $packer_built = (Get-Item $pipeline).LastWriteTime
    Write-Host "  packer: $pipeline  (built $packer_built)"

    # Is the packer older than its own source? Baking assets does not need a
    # rebuild -- only a change under src/pipeline does -- but nobody should have
    # to remember that, and "the tool is quietly out of date" is exactly how the
    # 2 GB rollover went four months without ever running. Check, do not rely on
    # discipline.
    $packer_src = (Join-Path (Join-Path $PSScriptRoot "..") "src\pipeline")
    if (Test-Path $packer_src) {
        $newest = Get-ChildItem $packer_src -Recurse -File -Include *.rs, *.toml -ErrorAction SilentlyContinue |
                  Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($newest -and $newest.LastWriteTime -gt $packer_built) {
            Write-Warning ("The packer is OLDER than its source: {0} changed {1}." -f $newest.Name, $newest.LastWriteTime)
            Write-Warning "Run 'just build release' (or cargo build --release) before baking."
        }
    }
    # Stray non-game files get baked into the .vfs. pack.rs walks the data tree
    # filtering only *hidden* entries -- no extension filter at all -- so a .bak
    # a script left behind, or a sidecar written next to its STB, ships inside
    # rose.vfs. A .bak is never legitimate here and is a hard error; other
    # strays only warn, because ~30 balance sidecars currently live beside their
    # tables on purpose and blocking every bake over them would be worse than
    # the leak. Check, do not rely on discipline.
    $strays = Get-ChildItem $input_dir -Recurse -File -Force -ErrorAction SilentlyContinue |
              Where-Object { -not $_.Name.StartsWith('.') }
    $baks = @($strays | Where-Object { $_.Extension -eq '.bak' })
    if ($baks.Count -gt 0) {
        foreach ($b in $baks) {
            Write-Host ("  stray backup: {0}" -f $b.FullName.Substring($input_dir.Length + 1))
        }
        throw ("$($baks.Count) .bak file(s) under $input_dir would be baked into the .vfs. " +
               "Move them to build/ and re-run.")
    }
    $other = @($strays | Where-Object { $_.Extension -in '.orig' })
    if ($other.Count -gt 0) {
        Write-Warning ("$($other.Count) non-game file(s) under $input_dir will be baked into the .vfs " +
                       "(tool output, {0:N0} KB). Harmless but shipped; add them to `$excludes." -f (($other | Measure-Object Length -Sum).Sum / 1KB))
    }

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
    Get-ChildItem -Path $output_dir -Filter "rose*.vfs" | ForEach-Object {
        Write-Host ("Created {0} ({1:N2} GB)" -f $_.FullName, ($_.Length / 1e9))
    }

    # Verify every bake, rather than relying on anyone remembering to. The .vfs
    # offset field is 32 bits: past 4 GB entries become unaddressable and the
    # client reads garbage with no error anywhere near the cause.
    $verify = (Join-Path $PSScriptRoot "verify-vfs.py")
    if (Test-Path $verify -PathType Leaf) {
        Write-Host ""
        & python $verify $output_dir
        if ($LASTEXITCODE -ne 0) {
            throw "verify-vfs.py FAILED -- do not deploy this archive."
        }
    } else {
        Write-Warning "verify-vfs.py not found; archive NOT verified."
    }
}
finally {
    Remove-Item -LiteralPath $manifest_path -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stdout_log -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderr_log -ErrorAction SilentlyContinue
}
