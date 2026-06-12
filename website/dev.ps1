param (
    [ValidateSet("run", "build", "shell")]
    [string]$command = "run"
)

# TODO: Read these from somewhere?
$website_db = "rose-next-website"
$account_db = "rose-next-account"
$game_db = "rose-next"
$email_user = "ralph@rednim.com"
$email_password = "wshmqvs25zujcs5u"

$env_vars = @(
    "DEBUG=true",
    "WEBSITE_DATABASE_NAME=$website_db",
    "ACCOUNT_DATABASE_NAME=$account_db",
    "GAME_DATABASE_NAME=$game_db",
    "EMAIL_SMTP_USERNAME=$email_user",
    "EMAIL_SMTP_PASSWORD=$email_password"
)

$env_params = @()
foreach ($var in $env_vars) {
    $env_params += "--env"
    $env_params += $var
}

if ($command -eq "build") {
    docker build --force-rm --rm -t rose-next-website $PSScriptRoot
    docker image prune -f
}
elseif ($command -eq "run") {
    docker run --rm -it -p 8000:8000 `
        -v $PSScriptRoot`:/app `
        $env_params `
        --name rose-next-website `
        rose-next-website
} 
elseif ($command -eq "shell") {
    docker container exec -w /app/rosenext $env_params -it rose-next-website /bin/bash
}