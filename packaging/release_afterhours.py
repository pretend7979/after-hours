#!/usr/bin/env python3
"""
Publishes an After Hours Arcade Manager release on GitHub: pushes main, tags v<version>, builds,
packages and creates the GitHub release with the zip. Uses your git and gh logins.

The version is project(... VERSION x.y.z) in CMakeLists.txt; bump it before releasing a new one
(After Hours compares it with the newest release tag to offer updates).

usage:
  python packaging/release_afterhours.py [--commit "message"] [--notes "text"] [--dry-run]

  --commit   commit every change first with this message (an Assisted-by footer is added)
  --fresh-history  start the GitHub repository over: one commit of the current code as main,
             every earlier release and tag deleted (the old local main is kept as
             pre-fresh-main)
  --notes    release notes (default: "After Hours Arcade Manager <version>")
  --dry-run  print what would run, change nothing
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_afterhours import package  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUTPUTS = ROOT / "out"
TOOLCHAIN_BIN = r"C:\msys64\clang64\bin"
GITHUB_REPO = "pretend7979/after-hours"
REMOTE = "origin"
ASSISTED_BY = "Assisted-by: Claude Opus 5.5 (code and packaging)"

ENV = dict(os.environ, PATH=TOOLCHAIN_BIN + os.pathsep + os.environ.get("PATH", ""))
DRY = False


def run(*args, capture=False, check=True, changes=True):
    """Runs a command; with --dry-run, commands that change something are only printed."""
    print("  >", " ".join(str(a) for a in args))
    if DRY and changes:
        return ""
    result = subprocess.run([str(a) for a in args], cwd=ROOT, env=ENV, text=True,
                            capture_output=capture)
    if check and result.returncode != 0:
        if capture:
            print(result.stdout + result.stderr)
        sys.exit(f"Failed: {' '.join(str(a) for a in args)}")
    return result.stdout.strip() if capture else ""


def git(*args, **kwargs):
    return run("git", *args, **kwargs)


def main():
    global DRY
    parser = argparse.ArgumentParser(description="Publish an After Hours Arcade Manager release")
    parser.add_argument("--commit", help="commit all changes first with this message")
    parser.add_argument("--fresh-history", action="store_true",
                        help="replace the GitHub history, releases and tags with one commit")
    parser.add_argument("--notes", help="release notes")
    parser.add_argument("--dry-run", action="store_true")
    options = parser.parse_args()
    DRY = options.dry_run

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(AfterHours VERSION ([0-9.]+)", cmake).group(1)
    tag = f"v{version}"
    notes = options.notes or f"After Hours Arcade Manager {version}"
    print(f"Releasing After Hours Arcade Manager {version}")

    run("gh", "auth", "status", capture=True, changes=False)
    if options.fresh_history:
        # The repository starts over from today's code: one commit, no earlier history. The old
        # main stays reachable locally as pre-fresh-main.
        print("Starting a new history")
        if git("rev-parse", "--verify", "main", capture=True, check=False, changes=False):
            git("branch", "-f", "pre-fresh-main", "main")
        git("checkout", "--orphan", "fresh-main")
        git("add", "-A")
        git("commit", "-m", options.commit or f"After Hours Arcade Manager {version}", "-m",
            ASSISTED_BY)
        git("branch", "-M", "main")
        for old_tag in git("tag", "--list", capture=True, changes=False).split():
            git("tag", "-d", old_tag)
    elif options.commit:
        if git("status", "--porcelain", capture=True, changes=False):
            git("add", "-A")
            git("commit", "-m", options.commit, "-m", ASSISTED_BY)
    elif git("status", "--porcelain", capture=True, changes=False):
        sys.exit("There are uncommitted changes: commit them, or pass --commit \"message\".")
    if not DRY and not git("rev-parse", "--verify", "HEAD", capture=True, check=False,
                           changes=False):
        sys.exit("Nothing is committed yet: pass --commit \"message\".")

    # A fresh history has just dropped the local tags (only printed in a dry run).
    if not options.fresh_history and git("tag", "--list", tag, capture=True, changes=False):
        sys.exit(f"Tag {tag} already exists: bump VERSION in CMakeLists.txt for a new release.")

    print(f"Pushing the source to {GITHUB_REPO} main")
    if options.fresh_history:
        # Every earlier release and tag belongs to the old history.
        for old in run("gh", "release", "list", "--repo", GITHUB_REPO, "--json", "tagName",
                       "--jq", ".[].tagName", capture=True, changes=False).split():
            run("gh", "release", "delete", old, "--repo", GITHUB_REPO, "--yes")
        git("push", "--force", "-u", REMOTE, "HEAD:main")
        refs = [line.split()[-1] for line in
                git("ls-remote", "--tags", REMOTE, capture=True, changes=False).splitlines()]
        refs = [ref for ref in refs if not ref.endswith("^{}")]
        if refs:
            git("push", REMOTE, "--delete", *refs)
    else:
        git("push", "-u", REMOTE, "HEAD:main")
    git("tag", "-a", tag, "-m", f"After Hours Arcade Manager {version}")
    git("push", REMOTE, tag)

    print("Building")
    if not (ROOT / "build" / "CMakeCache.txt").exists():
        sys.exit("No build folder: configure it first (see README.md, with the IGDB keys).")
    run("cmake", "--build", "build")
    print("Packaging")
    if not DRY and not package(version, OUTPUTS):
        sys.exit("Packaging failed")

    archive = OUTPUTS / f"After-Hours-{version}-windows-x64.zip"
    print(f"Publishing the release {tag}")
    run("gh", "release", "create", tag, archive, f"{archive}.sha256.txt", "--repo", GITHUB_REPO,
        "--title", f"After Hours Arcade Manager {version}", "--notes", notes)
    print(f"Done: https://github.com/{GITHUB_REPO}/releases/tag/{tag}")


if __name__ == "__main__":
    main()
