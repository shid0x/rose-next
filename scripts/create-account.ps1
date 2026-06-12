<#
.SYNOPSIS
    Create a Rose Next game account directly in the PostgreSQL database.

.DESCRIPTION
    Generates a 16-char salt and the double-SHA256 password hash that the
    login server expects (stored = sha256(sha256(plaintext) + salt)), then
    inserts a row into the `account` table via psql. The DB connection is read
    from the `connection_string` in a server.toml file.

    Password scheme mirrors scripts/generate-password.py and the server check
    in src/sho_loginserver/src/cls_sqlthread.cpp.

.EXAMPLE
    ./scripts/create-account.ps1 -Email user -Password secret

.EXAMPLE
    ./scripts/create-account.ps1 -Email gm -Password secret -AccessLevel 100
#>
param (
    [Parameter(Mandatory = $true)]
    [string]$Email,

    [Parameter(Mandatory = $true)]
    [string]$Password,

    [int]$AccessLevel = 0,

    # Path to the server.toml whose connection_string points at the target DB.
    [string]$ServerToml = (Join-Path (Get-Item $PSScriptRoot).Parent "Exes" "server.toml")
)

$ErrorActionPreference = "Stop"

# --- Validate inputs against the account table column widths -----------------
# email varchar(30), password char(64), salt char(16)
if ($Email.Length -eq 0 -or $Email.Length -gt 30) {
    throw "Email must be 1-30 characters (got $($Email.Length))."
}
if ($Password.Length -eq 0) {
    throw "Password must not be empty."
}

# --- Read the DB connection string from server.toml --------------------------
if (-not (Test-Path $ServerToml)) {
    throw "server.toml not found at '$ServerToml'. Pass -ServerToml <path>."
}

$tomlText = Get-Content -Path $ServerToml -Raw
$connMatch = [regex]::Match($tomlText, 'connection_string\s*=\s*"([^"]+)"')
if (-not $connMatch.Success) {
    throw "No connection_string found in '$ServerToml'."
}
$connString = $connMatch.Groups[1].Value

# postgres://user:password@host[:port]/database
$uriMatch = [regex]::Match($connString,
    '^postgres(?:ql)?://(?<user>[^:@/]+)(?::(?<pass>[^@/]*))?@(?<host>[^:/]+)(?::(?<port>\d+))?/(?<db>[^?]+)')
if (-not $uriMatch.Success) {
    throw "Could not parse connection_string '$connString'."
}
$pgUser = $uriMatch.Groups["user"].Value
$pgPass = $uriMatch.Groups["pass"].Value
$pgHost = $uriMatch.Groups["host"].Value
$pgPort = if ($uriMatch.Groups["port"].Success) { $uriMatch.Groups["port"].Value } else { "5432" }
$pgDb = $uriMatch.Groups["db"].Value

# --- Locate psql.exe ---------------------------------------------------------
# Prefer $env:PGBIN, then any installed "C:\Program Files\PostgreSQL\<ver>\bin"
# (highest version first), then PATH.
$psql = $null
if (Test-Path "env:PGBIN") {
    $candidate = Join-Path $env:PGBIN "psql.exe"
    if (Test-Path $candidate) { $psql = $candidate }
}
if (-not $psql) {
    $pgBase = "C:/Program Files/PostgreSQL"
    if (Test-Path $pgBase) {
        $found = Get-ChildItem -Path $pgBase -Directory -ErrorAction SilentlyContinue |
            Sort-Object { [int]($_.Name -replace '\D', '0') } -Descending |
            ForEach-Object { Join-Path $_.FullName "bin/psql.exe" } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1
        if ($found) { $psql = $found }
    }
}
if (-not $psql) {
    $onPath = Get-Command psql -ErrorAction SilentlyContinue
    if ($onPath) { $psql = $onPath.Source }
}
if (-not $psql) {
    throw "psql.exe not found. Set `$env:PGBIN to your PostgreSQL bin directory."
}

# --- Hash the password -------------------------------------------------------
function Get-Sha256Hex([string]$value) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($value)
        $hash = $sha.ComputeHash($bytes)
        return ([System.BitConverter]::ToString($hash) -replace '-', '').ToLower()
    } finally {
        $sha.Dispose()
    }
}

$letters = "abcdefghijklmnopqrstuvwxyz"
$salt = -join (1..16 | ForEach-Object { $letters[(Get-Random -Maximum $letters.Length)] })

# Login server compares sha256(client_password + salt); the client sends
# sha256(plaintext) as the password, so the stored value is the double hash.
$innerHash = Get-Sha256Hex $Password
$storedPassword = Get-Sha256Hex ($innerHash + $salt)

# --- Insert via psql ---------------------------------------------------------
$env:PGHOST = $pgHost
$env:PGPORT = $pgPort
$env:PGUSER = $pgUser
$env:PGPASSWORD = $pgPass
$env:PGDATABASE = $pgDb

# Build the statement directly: the hash is hex and the salt is lowercase a-z,
# so only the email can contain a quote. Double any single quote to escape it.
# (psql does not perform :'var' interpolation in -c mode.)
$emailLiteral = "'" + ($Email -replace "'", "''") + "'"
$passwordLiteral = "'$storedPassword'"
$saltLiteral = "'$salt'"
$insert = "INSERT INTO account (email, password, salt, access_level) VALUES ($emailLiteral, $passwordLiteral, $saltLiteral, $AccessLevel);"

Write-Host "Creating account '$Email' (access_level $AccessLevel) on $pgHost`:$pgPort/$pgDb ..."

$output = & $psql `
    --no-psqlrc `
    --quiet `
    -v "ON_ERROR_STOP=1" `
    -c $insert 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host ($output | Out-String)
    if ("$output" -match "duplicate key|already exists|unique") {
        throw "Account '$Email' already exists."
    }
    throw "Failed to create account (psql exit code $LASTEXITCODE)."
}

Write-Host "Account '$Email' created." -ForegroundColor Green
