#!/usr/bin/env python3
"""Renames the port's old Undertale-only "ut" names to generic ones.

The openFPGA port started out running only Undertale, so its own code, files
and settings were named ut_*, Ut*, ut* and UT_*. Most of that is not about
Undertale at all, so the port's names now follow its other files instead:

    ut_menu.c, utMenuRun, UtMenu, UT_MENU_PAD  ->  of_menu.c, ofMenuRun, OfMenu, OF_MENU_PAD
    UT_RECORD=... (desktop environment)        ->  BS_RECORD=...
    [undertale] (log prefix)                   ->  [butterscotch]
    .claude/skills/undertale-pocket-*          ->  .claude/skills/butterscotch-pocket-*

What stays as it was: the save, music and texture pack magics ("UTV1",
"UTM1", "UTX2"; renaming them would orphan every card), upstream
Butterscotch's own mentions of Undertale, and vendor/.

It only renames, so it can be run on any branch that still carries the old
names (a branch made before the rename, after it merges main):

    python3 src/openfpga/tools/rename-ut.py [--sdk <openfpgaSDK checkout>] [--dry-run]

It stops before changing anything if a new name would collide with one the
openfpgaOS SDK already defines. Running it twice changes nothing the second
time. A line marked "rename-ut: keep", and the line after it, are left alone.
"""

import argparse
import os
import re
import subprocess
import sys

# Desktop settings read from the environment. Any other "UT_..." string
# literal the code reads is picked up as well; these are listed so a branch
# that already moved some of them still renames what its docs say.
KNOWN_ENV = {
    "AUDIO_DUMP", "AUDIO_LOG", "CODEHASH", "DEBUG", "DISASM", "DUMP_DIR",
    "DUMP_EVERY", "DUMP_FRAME", "DUMP_PATH", "DUMP_STATE", "EXIT_FRAME",
    "GOTO", "MENU", "MIRROR_FAINT", "NOFLIP", "NO_MIRROR", "NO_NATIVE",
    "NO_TILELAYER", "OVERLAY", "PLAYBACK", "PROFILE", "RECORD", "SCRIPT",
    "SEED", "SET", "SKIP", "SMOOTH", "UNCAPPED", "RUN_DIR",
}

# A line holding this marker, and the line after it, are left as they are
# (the fallbacks that still read the old names).
KEEP = "rename-ut: keep"

# Game-specific names that get a game's name, not the generic prefix.
SPECIAL = {
    "UT_GAME_DELTARUNE": "GAME_DELTARUNE",
}

SKIP_DIRS = ("vendor/", "compat/")
SELF = "src/openfpga/tools/rename-ut.py"

# A UT_ name starts a word, or follows a compiler -D/-U.
UT_TOKEN = re.compile(r"(?:(?<=-D)|(?<=-U)|(?<![A-Za-z0-9_]))UT_([A-Za-z0-9_]+)")


def git(root, *args):
    return subprocess.run(["git", "-C", root, *args], check=True, capture_output=True, text=True).stdout


def tracked_files(root):
    return [f for f in git(root, "ls-files", "-z").split("\0") if f]


def is_text(path):
    try:
        with open(path, "rb") as f:
            return b"\0" not in f.read(8192)
    except OSError:
        return False


def file_moves(files):
    """Old path -> new path for files and skill folders named after the old prefix."""
    moves = {}
    for f in files:
        d, base = os.path.split(f)
        if base.startswith("ut_") and (f.startswith("src/openfpga/") or f.startswith("tests/")):
            base = "of_" + base[3:]
        elif base == "ut-frames.sh" and f.startswith(".claude/skills/"):
            base = "bs-frames.sh"
        d = re.sub(r"^\.claude/skills/undertale-pocket-([a-z]+)", r".claude/skills/butterscotch-pocket-\1", d)
        new = os.path.join(d, base)
        if new != f:
            moves[f] = new
    return moves


def env_names(root, files):
    names = set(KNOWN_ENV)
    for f in files:
        if f.startswith(SKIP_DIRS) or not f.startswith("src/openfpga/") or not f.endswith((".c", ".h")):
            continue
        with open(os.path.join(root, f), encoding="utf-8", errors="surrogateescape") as h:
            names.update(re.findall(r'getenv\("UT_([A-Z0-9_]+)"\)', h.read()))
    return names


def rewrite(text, env):
    lines = text.splitlines(keepends=True)
    out, keep = [], False
    for line in lines:
        if KEEP in line:
            out.append(line)
            keep = True
        elif keep:
            out.append(line)
            keep = False
        else:
            out.append(rewrite_line(line, env))
    text = "".join(out)
    # Aliases such as "#define UT_SCREEN_W OF_SCREEN_W" are now no-ops.
    return re.sub(r"^[ \t]*#define[ \t]+(OF_[A-Z0-9_]+)[ \t]+\1[ \t]*\n", "", text, flags=re.M)


def rewrite_line(text, env):
    def ut(m):
        whole = "UT_" + m.group(1)
        if whole in SPECIAL:
            return SPECIAL[whole]
        if m.group(1) in env:
            return "BS_" + m.group(1)
        return "OF_" + m.group(1)

    text = UT_TOKEN.sub(ut, text)
    text = re.sub(r"\bUt([A-Z][A-Za-z0-9_]*)", r"Of\1", text)
    text = re.sub(r"\but([A-Z][A-Za-z0-9_]*)", r"of\1", text)
    text = re.sub(r"\but_([a-z0-9])", r"of_\1", text)
    text = text.replace("[undertale]", "[butterscotch]")
    text = re.sub(r"\bundertale-pocket-(build|perf|verify)\b", r"butterscotch-pocket-\1", text)
    return text.replace("ut-frames.sh", "bs-frames.sh")


def sdk_names(sdk):
    """OF_ macros and of* identifiers the SDK's headers already define."""
    names, headers = set(), set()
    inc = os.path.join(sdk, "src", "sdk", "include")
    if not os.path.isdir(inc):
        return None, None
    for base, _, entries in os.walk(inc):
        for e in entries:
            headers.add(e)
            with open(os.path.join(base, e), encoding="utf-8", errors="surrogateescape") as h:
                names.update(re.findall(r"\b(?:OF_[A-Z0-9_]+|of[A-Z]\w*|Of[A-Z]\w*)\b", h.read()))
    return names, headers


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", help="openfpgaSDK checkout to check new names against (default: next to the repository)")
    ap.add_argument("--dry-run", action="store_true", help="list what would change, change nothing")
    args = ap.parse_args()

    root = git(os.path.dirname(os.path.abspath(__file__)), "rev-parse", "--show-toplevel").strip()
    files = tracked_files(root)
    env = env_names(root, files)
    moves = file_moves(files)

    # Rewrite in memory first, so a collision stops the run before anything is written.
    changed = {}
    for f in files:
        if f == SELF or f.startswith(SKIP_DIRS) or not is_text(os.path.join(root, f)):
            continue
        with open(os.path.join(root, f), encoding="utf-8", errors="surrogateescape") as h:
            old = h.read()
        new = rewrite(old, env)
        if new != old:
            changed[f] = (old, new)

    sdk = args.sdk or os.path.join(os.path.dirname(root), "openfpgaSDK")
    sdk_ids, sdk_headers = sdk_names(sdk)
    if sdk_ids is None:
        print("rename-ut: no SDK at %s, so new names are not checked against it (--sdk <path>)" % sdk, file=sys.stderr)
    else:
        introduced = set()
        for old, new in changed.values():
            ours = re.findall(r"\b(?:OF_[A-Z0-9_]+|of[A-Z]\w*|Of[A-Z]\w*)\b", new)
            before = set(re.findall(r"\b(?:OF_[A-Z0-9_]+|of[A-Z]\w*|Of[A-Z]\w*)\b", old))
            introduced.update(n for n in ours if n not in before)
        clashes = sorted(introduced & sdk_ids)
        clashes += sorted(os.path.basename(n) for n in moves.values() if os.path.basename(n) in sdk_headers)
        if clashes:
            print("rename-ut: these new names are already the SDK's; rename them by hand first:", file=sys.stderr)
            for c in clashes:
                print("  " + c, file=sys.stderr)
            return 1
    for old, new in moves.items():
        if new not in moves and os.path.exists(os.path.join(root, new)):
            print("rename-ut: %s would replace %s, which exists" % (old, new), file=sys.stderr)
            return 1

    for old, new in sorted(moves.items()):
        print("move  %s -> %s" % (old, new))
    for f in sorted(changed):
        print("edit  %s" % moves.get(f, f))
    if args.dry_run or (not moves and not changed):
        if not moves and not changed:
            print("rename-ut: nothing left to rename")
        return 0

    for f, (_, new) in changed.items():
        with open(os.path.join(root, f), "w", encoding="utf-8", errors="surrogateescape") as h:
            h.write(new)
    for old, new in sorted(moves.items()):
        os.makedirs(os.path.join(root, os.path.dirname(new)), exist_ok=True)
        git(root, "mv", old, new)
    return 0


if __name__ == "__main__":
    sys.exit(main())
