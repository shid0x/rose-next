import os
import subprocess
import sys
from glob import glob
from pathlib import Path


def read_clang_ignores(path):
    with open(path, "r") as f:
        lines = f.readlines()

    ignores = []
    for line in lines:
        line = line.strip()

        # Ignore comments
        if line.startswith("#"):
            continue

        # Ignore blank lines
        if not line:
            continue

        ignores += glob(os.path.normcase(line), recursive=True)

    return ignores


""" Uses clang-format to format the code base """
if __name__ == "__main__":
    settings = {"verbose": False}

    for arg in sys.argv:
        if arg == "-v":
            settings["verbose"] = True

    script_dir = Path(os.path.abspath(__file__)).parent
    root_dir = script_dir.parent
    src_dir = Path(os.path.join(root_dir, "src"))

    os.chdir(root_dir)

    ignore_file = os.path.join(root_dir, ".clang-format-ignore")
    ignores = read_clang_ignores(ignore_file)

    headers_glob = os.path.normcase(os.path.join(src_dir, "**", "*.h"))
    sources_glob = os.path.normcase(os.path.join(src_dir, "**", "*.cpp"))

    headers = glob(headers_glob, recursive=True)
    sources = glob(sources_glob, recursive=True)
    targets = headers + sources

    print("Formatting project", flush=True)
    subprocess.run(["clang-format", "--version"], check=True)

    for target in targets:
        target = target.lower()

        skip_target = False
        for ignore in ignores:
            ignore = ignore.lower()
            if ignore in target:
                skip_target = True

        if skip_target:
            if settings["verbose"]:
                print(f"Ignoring: {target}")
            continue

        if settings["verbose"]:
            print(f"Formatting: {target}")

        command = ["clang-format", "-i", "--style=file", target]
        subprocess.run(command, check=True)
