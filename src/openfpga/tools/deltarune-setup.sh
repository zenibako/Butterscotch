#!/bin/bash
# Gathers one Deltarune chapter's files into games/deltarune/, where
# `make GAME=deltarune` expects them. Nothing is copied: the folder holds
# links into your own install.
#
#   tools/deltarune-setup.sh <game folder> [chapter, default 1]
#
# <game folder> is the one holding chapter1_mac/ (or chapter1_windows/) and
# mus/: on macOS, DELTARUNE.app/Contents/Resources.
#
# The music folder is shared by all chapters, so only the .ogg files this
# chapter's data file names are linked.
set -e
SRC="$1"; CHAPTER="${2:-1}"
[ -n "$SRC" ] || { echo "usage: $0 <game folder> [chapter]"; exit 2; }
# The links live in games/deltarune/, so a relative path would point nowhere from there.
SRC="$(cd "$SRC" 2>/dev/null && pwd)" || { echo "$1 is not a folder"; exit 1; }
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HERE/games/deltarune"

CH=""
for d in "$SRC/chapter${CHAPTER}_mac" "$SRC/chapter${CHAPTER}_windows"; do
    [ -d "$d" ] && { CH="$d"; break; }
done
[ -n "$CH" ] || { echo "$SRC has no chapter${CHAPTER}_mac or chapter${CHAPTER}_windows folder"; exit 1; }
DATA=""
for f in "$CH/game.ios" "$CH/data.win"; do
    [ -f "$f" ] && { DATA="$f"; break; }
done
[ -n "$DATA" ] || { echo "$CH has no game.ios or data.win"; exit 1; }
[ -f "$CH/lang/lang_en.json" ] || { echo "$CH/lang/lang_en.json is missing"; exit 1; }

rm -rf "$OUT/music" "$OUT/data.win" "$OUT/lang_en.json" "$OUT"/audiogroup*.dat
mkdir -p "$OUT/music"
ln -s "$DATA" "$OUT/data.win"
ln -s "$CH/lang/lang_en.json" "$OUT/lang_en.json"
for f in "$CH"/audiogroup*.dat; do [ -f "$f" ] && ln -s "$f" "$OUT/"; done

count=0
for f in "$CH"/*.ogg "$SRC"/mus/*.ogg; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"
    if LC_ALL=C grep -qiaF -- "$name" "$DATA"; then
        ln -sf "$f" "$OUT/music/$name"
        count=$((count + 1))
    fi
done
echo "games/deltarune: chapter $CHAPTER, $count music files"
