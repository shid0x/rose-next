param (
  [ValidateSet("release", "debug")]
  [string]$config = "release"
)

function Write-Header {
  param($header, $lineSymbol = "-")

  Write-Host $("+" + ($lineSymbol * 78) + "+")
  Write-Host $("| " + $header + $(" " * (76 - $header.length)) + " |")
  Write-Host $("+" + ($lineSymbol * 78) + "+")
}

Write-Header "Building ROSE Next" "="

# -- Setup vswhere.exe
$vs_where = "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if (-not (Test-Path -Path $vs_where)) {
  Write-Error "Could not find vswhere. Is visual studio installer installed? Expected location: $vs_where"
  exit 1
}
Write-Host "Found vswhere: $vs_where"

# -- Find Visual Studio Installation path
$vs_version = "[16.3,17.0)"
$vs_install = & $vs_where -version $vs_version -property installationPath
if (-not (Test-Path -Path $vs_install)) {
  Write-Error "Could not find Visual Studio that matches version range $vs_version"
  exit 1
}
Write-Host "Found Visual Studio: $vs_install"

# -- Start Visual Studio Shell
Import-Module (Join-Path $vs_install "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
$null = Enter-VsDevShell -VsInstallPath $vs_install -SkipAutomaticLocation
Write-Host "Started Visual Studio shell"

$rose_next_root = (Get-Item $PSScriptRoot).Parent

# -- Build thirdparty
Write-Header "Building thirdparty"

# We set the warning level to 0 to disable warnings in thirdparty projects
# We set the console line output to only show us errors
& MSBuild.exe "-p:Configuration=$config;WarningLevel=0;Platform=x86" -maxCpuCount -v:minimal $rose_next_root\thirdparty.sln
if (!$?) {
  exit 1
}

# -- Build Rust projects
Write-Header "Building Rust projects"

$rust_toolchain = "stable-i686-pc-windows-msvc"
$rust_target = "i686-pc-windows-msvc"
& rustup toolchain install $rust_toolchain
& rustup target add $rust_target

$cargo_args = @("--manifest-path", "$rose_next_root/src/Cargo.toml")
if ($config -eq "release") {
  $cargo_args += "--release"
}

& cargo +$rust_toolchain build @cargo_args

# -- Build C++ projects
& MSBuild.exe "-p:Configuration=$config;Platform=x86" -maxCpuCount -v:minimal $rose_next_root\rose-next.sln
if (!$?) {
  exit 1
}