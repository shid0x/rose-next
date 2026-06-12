param (
    [string]$pg_database = "squash_migrations",
    [string]$pg_host = "localhost",
    [int]$pg_port = 5432,
    [string]$pg_user = "postgres",
    [string]$pg_password = "postgres",
    [string]$out = (Join-Path $PSScriptRoot .. database),
    [string]$out_name = "rose-next"
)

$pg_root = "C:/Program Files/PostgreSQL/12/bin"
if (Test-Path "env:PGBIN") {
    $pg_root = $env:PGBIN
}
$pg_createdb = Join-Path $pg_root createdb.exe
$pg_dropdb = Join-Path $pg_root dropdb.exe
$pg_dump = Join-Path $pg_root pg_dump.exe
$psql = Join-Path $pg_root  psql.exe
$pg_flags = @(
    "--schema-only",
    "--clean",
    "--if-exists",
    "--no-privileges",
    "--no-owner",
    "--no-tablespaces",
    "--quote-all-identifiers"
)

$env:PGDATABASE = $pg_database
$env:PGHOST = $pg_host
$env:PGPORT = $pg_port
$env:PGUSER = $pg_user
$env:PGPASSWORD = $pg_password

$rose_next_root = Join-Path $PSScriptRoot ..
$db_root = Join-Path $rose_next_root database

# game database
$migrations_root = Join-Path $db_root migrations
$migrations = Get-ChildItem -Path $migrations_root -Directory | Sort-Object
$squashed_out = Join-Path $out ($out_name + ".sql")

Write-Host "Executing migrations"

& $pg_createdb $pg_database
Foreach ($migration in $migrations) {
    & $psql -f (Join-Path $migration up.sql)
}

Write-Host "Dumping database to $squashed_out"

& $pg_dump @pg_flags -f $squashed_out $pg_database
& $pg_dropdb $pg_database
