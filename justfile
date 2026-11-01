set shell := ["pwsh.exe", "-c"]

CONFIG := env_var_or_default("CONFIG", "release")

ROOT := env_var_or_default("ROOT", justfile_directory())
SCRIPTS := ROOT + "/scripts"
# rose-patch's folder: patch.toml, the signing key, the served www/ (outside the repo)
PATCH := env_var_or_default("PATCH_DIR", ROOT + "/../patch-server")


# Build all code and assets
build-all config=CONFIG: (build config) (build-assets config)

# Build code projects
build config=CONFIG:
    {{SCRIPTS}}/build.ps1 -config {{config}}

# Build game assets
build-assets config=CONFIG:
    {{SCRIPTS}}/build-assets.ps1 -config {{config}}

# Setup dev environment by linking to assets dir
dev-setup: (build-assets "release")
    {{SCRIPTS}}/dev-setup.ps1

# Start the client
client config=CONFIG:
    {{SCRIPTS}}/dev-client.ps1 -config {{config}}

# Start the client and auto-connect looking up the connection info in env
client-auto config=CONFIG key="":
    {{SCRIPTS}}/dev-client.ps1 -config {{config}} -key "{{key}}"

# Alias to start auto client in release
cld key="": (client-auto "debug" key)

# Alias to start auto client in release
clr key="": (client-auto "release" key)

# Start all servers
server-all config=CONFIG:
    just loginserver {{config}}
    just worldserver {{config}}
    just gameserver {{config}}

# Start loginserver
loginserver config=CONFIG:
    pwsh -File {{SCRIPTS}}/dev-server.ps1 -config {{CONFIG}} --server "loginserver"

# Start worldserver
worldserver config=CONFIG:
    pwsh -File {{SCRIPTS}}/dev-server.ps1 -config {{CONFIG}} --server "worldserver"

# Start gameserver
gameserver config=CONFIG:
    pwsh -File {{SCRIPTS}}/dev-server.ps1 -config {{CONFIG}} --server "gameserver"

alias ls := loginserver
alias ws := worldserver
alias gs := gameserver

# Create a game account (access level optional, defaults to 0)
create-account email password access="0":
    pwsh -File {{SCRIPTS}}/create-account.ps1 -Email "{{email}}" -Password "{{password}}" -AccessLevel {{access}}

# Drop the OS file cache so the next client run reads from disk (needs elevation)
purge-cache:
    pwsh -File {{SCRIPTS}}/purge-page-cache.ps1

# Open Rose Dev Studio, the STB/STL editor (optionally with files to open)
studio *files:
    pythonw {{ROOT}}/src/tools/dev-studio/run.pyw {{files}}

# Publish the launch folder as the next version players' launchers download (optional news text)
patch-publish news="":
    {{ROOT}}/bin/release/rose-patch.exe publish --config "{{PATCH}}/patch.toml" {{ if news == "" { "" } else { "--message \"" + news + "\"" } }}

# Serve the published client to players' launchers (keep it running)
patch-serve:
    {{ROOT}}/bin/release/rose-patch.exe serve --config "{{PATCH}}/patch.toml"

# Set the launcher's news text ("" clears it)
patch-news text:
    {{ROOT}}/bin/release/rose-patch.exe message --config "{{PATCH}}/patch.toml" "{{text}}"

# What the patch server is publishing now
patch-status:
    {{ROOT}}/bin/release/rose-patch.exe status --config "{{PATCH}}/patch.toml"

# Create distribution
# TODO: Refactor dist script