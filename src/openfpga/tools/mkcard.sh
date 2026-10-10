#!/bin/bash
#
# mkcard.sh — assemble the Pocket SD tree: one Butterscotch core with an
# entry per game. A game is included when its program has been built and
# its data file is there; the entries of the others are left out, so the
# Pocket never lists a game that cannot start.
#
# Both games are the same program reading "data.win", "textures.bin" and
# "music.bin": on the card Deltarune's files carry their own names, and its
# entry (dist/.../Deltarune.json) puts them in the same data slots.
#
# Usage: mkcard.sh <SDK image.sh> <root holding runtime/ and build/> <dist dir> <port dir>
#
set -e
IMAGE_SH="$1"; ROOT="$2"; DIST="$3"; PORT="$4"
NAME=butterscotch
OUT="$ROOT/build/pocket/$NAME"

# <game> <instance file> <folder with its data> <data> <textures> <music>: names on the card
GAMES=(
    "undertale|Undertale.json|.|data.win|textures.bin|music.bin"
    "deltarune|Deltarune.json|games/deltarune|deltarune.win|dr_textures.bin|dr_music.bin"
)

first=""
for entry in "${GAMES[@]}"; do
    IFS='|' read -r game _ dir _ _ _ <<< "$entry"
    [ -f "$ROOT/.obj/$game/app.elf" ] && [ -f "$PORT/$dir/data.win" ] && { first="$ROOT/.obj/$game/app.elf"; break; }
done
[ -n "$first" ] || { echo "mkcard: no game is built with its data.win in place"; exit 1; }

# The SDK script lays down the core, the bitstream and the OS; it names the
# program after the tree, which is not a name any entry uses.
"$IMAGE_SH" "$NAME" "$first" "$ROOT" "$DIST" > /dev/null
COMMON="$OUT/Assets/$NAME/common"
rm -f "$COMMON/$NAME.elf"

included=""
for entry in "${GAMES[@]}"; do
    IFS='|' read -r game instance dir data textures music <<< "$entry"
    elf="$ROOT/.obj/$game/app.elf"
    src="$PORT/$dir"
    # The program cannot draw without its texture pack. The music pack is
    # only built when the .ogg files are there, so a game may go without it.
    if [ -f "$elf" ] && [ -f "$src/data.win" ] && [ ! -f "$src/textures.bin" ]; then
        echo "mkcard: $game left out, $src/textures.bin is missing"
    fi
    if [ -f "$elf" ] && [ -f "$src/data.win" ] && [ -f "$src/textures.bin" ]; then
        cp "$elf" "$COMMON/$game.elf"
        cp "$src/data.win" "$COMMON/$data"
        cp "$src/textures.bin" "$COMMON/$textures"
        if [ -f "$src/music.bin" ]; then
            cp "$src/music.bin" "$COMMON/$music"
        else
            echo "mkcard: $game has no $src/music.bin, it will have no sound pack"
        fi
        included="$included $game"
    else
        rm -f "$OUT/Assets/$NAME"/*/"$instance" "$COMMON/${game}_os.ini"
    fi
done
echo "Ready: build/pocket/$NAME/ with$included"
