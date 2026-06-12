"""Generate launchers

A utility program to generate development helper scripts.

Args:
    out - Directory to output the scripts (default "..\..")
    src - The directory to the rose-next source tree (default = "..")
    game - The directory to the rose-next game data (default = "..\game")
"""
import argparse
import os
import sys
from pathlib import Path

def generate_scripts():
    script_path = Path(os.path.abspath(__file__))
    src_dir = script_path.parent.parent
    out_dir = script_path.parent.parent.parent
    game_dir = script_path.parent.parent / "game"

    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir")
    parser.add_argument("--src-dir")
    parser.add_argument("--game-dir")
    args = parser.parse_args()
    
    if args.out_dir:
        out_dir = out_dir
    if args.src_dir:
        src_dir = src_dir
    if args.game_dir:
        game_dir = game_dir
    
    server_targets  = [
        ("debug-1-login.cmd", "debug", "sho_loginserver.exe"),
        ("debug-2-world.cmd", "debug", "sho_worldserver.exe"),
        ("debug-3-game.cmd", "debug", "sho_gameserver.exe"),
        ("release-1-login.cmd", "release", "sho_loginserver.exe"),
        ("release-2-world.cmd", "release", "sho_worldserver.exe"),
        ("release-3-game.cmd", "release", "sho_gameserver.exe")
    ]

    for target in server_targets:
        out_file = Path(out_dir, target[0])
        exe = Path(src_dir, "bin", target[1], target[2])
        conf = Path(out_dir, "server", "server.toml")

        print(f"Generating {out_file}")
        with open(out_file, "w+") as f:
            f.write(f"{exe} --config {conf}")

    client_targets = [("debug-client.cmd", "debug"), ("release-client.cmd", "release")]
    for target in client_targets:
        out_file = Path(out_dir , target[0])
        exe = Path(src_dir, "bin", target[1], "rosenext.exe")

        print(f"Generating {out_file}")
        with open(out_file, "w+") as f:
            f.write(f"cd {game_dir}\n")
            f.write(f"{exe}")

    input("Press Enter to continue...")

if __name__ == "__main__":
    generate_scripts()