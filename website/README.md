# ROSE Next Website

## Setup
- Install docker
- TODO: Setup database connections, see `dev.ps1` and `settings.py` for default vals
- Run `dev.ps1 build`
- Run `dev.ps1 run`

# Notes
docker network create -d bridge --subnet 192.168.1.0/24 --gateway 10.10.10.10 rose-next-website-network