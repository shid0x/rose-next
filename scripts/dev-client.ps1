# Starts the client, using auto-connect if it can find auto-connect information
# for the given name in the environment variables
# 
# Example:
# 
# RALPH_SERVER = "127.0.0.1"
# RALPH_USERNAME = "ralph"
# RALPH_PASSWORD = "ralph"
# RALPH_SERVER_ID = 1
# RALPH_CHANNEL_ID = 1
# RALPH_CHARACTER = "Ralph"
#
# dev-client.ps1 -config debug -key ralph
param (
    [ValidateSet("release", "debug")]
    [string]$config = "release",

    [string]$key = ""
)

# Get an environment variable or return the default
function env_or_default {
    param (
        [string]$key,
        [string]$default
    )
    return (Test-Path "env:$key") ? (Get-Item "env:$key").Value : $default
}

$rose_next_root = (Get-Item $PSScriptRoot).Parent
$rose_next_exe = (Join-Path $rose_next_root "bin" $config "rosenext.exe")
$dev_client_dir = (Join-Path $rose_next_root "dev" "client" $config)

$key = $key.ToUpper()
$ip = env_or_default -key ($key + "_SERVER") -default "127.0.0.1"
$username = env_or_default -key ($key + "_USERNAME") -default ""
$password = env_or_default -key ($key + "_PASSWORD") -default ""
$server_id = env_or_default -key ($key + "_SERVER_ID") -default 1
$channel_id = env_or_default -key ($key + "_CHANNEL_ID") -default 1
$character = env_or_default -key ($key + "_CHARACTER") -default ""

$client_args = @("--server", $ip)
if ($username && $password && $character) {
    $client_args += @(
        "--username", $username,
        "--password", $password,
        "--auto-connect-server", $server_id,
        "--auto-connect-channel", $channel_id,
        "--auto-connect-character", $character
    )
}

Start-Process -FilePath $rose_next_exe -ArgumentList $client_args -WorkingDirectory $dev_client_dir