#!/bin/bash
#
# mkcompare.sh — add benchmark instances and v0.9 comparison cores to the
# assembled Pocket tree, so one SD card can run the same benchmark on:
#
#   Butterscotch       this SDK's runtime (v0.7), os25 bitstream: Undertale's and Deltarune's benchmarks
#   Butterscotch09os25   v0.9 runtime, os25 bitstream
#   Butterscotch09os20   v0.9 runtime, os20 bitstream (dual-issue CPU)
#
# All three share Assets/butterscotch/common (data.win, textures.bin,
# music.bin); the v0.9 cores use their own os09.bin and undertale09.elf.
#
# Usage: mkcompare.sh <build tree> <v0.9 runtime/pocket dir> <v0.9 app.elf> [Deltarune -O3 app.elf] [Deltarune LTO app.elf]
#
set -e

OUT="$1"; RT09="$2"; ELF09="$3"; ELF_O3="${4:-}"; ELF_LTO="${5:-}"
[ -d "$OUT/Cores/zenibako.Butterscotch" ] || { echo "mkcompare: $OUT is not an assembled tree"; exit 1; }
[ -f "$RT09/os.bin" ] || { echo "mkcompare: no os.bin in $RT09"; exit 1; }
[ -f "$ELF09" ] || { echo "mkcompare: $ELF09 not found"; exit 1; }

COMMON="$OUT/Assets/butterscotch/common"
BASE_CORE="$OUT/Cores/zenibako.Butterscotch"
BASE_INSTANCE="$OUT/Assets/butterscotch/zenibako.Butterscotch/Undertale.json"

write_ini() { # <file> <elf> <args> <variant>
    printf '[os]\nELF=%s\nARGS=%s\nVARIANT=%s\n' "$2" "$3" "$4" > "$COMMON/$1"
}

# Instance JSON derived from the base one with different OS, config and ELF files.
write_instance() { # <dest> <os.bin name> <ini name> <elf name>
    sed -e "s/\"os\.bin\"/\"$2\"/" \
        -e "s/\"undertale_os\.ini\"/\"$3\"/" \
        -e "s/\"undertale\.elf\"/\"$4\"/" \
        "$BASE_INSTANCE" > "$1"
}

# Benchmark instance for the base (v0.7) core.
write_ini undertale_bench.ini undertale.elf --bench os25
write_instance "$OUT/Assets/butterscotch/zenibako.Butterscotch/Benchmark.json" os.bin undertale_bench.ini undertale.elf
write_ini undertale_b320.ini undertale.elf --bench-smooth os25
write_instance "$OUT/Assets/butterscotch/zenibako.Butterscotch/Benchmark 320 smooth.json" os.bin undertale_b320.ini undertale.elf

# The same for Deltarune, when the tree has it: its own benchmark, in each of L's two settings.
DR_INSTANCE="$OUT/Assets/butterscotch/zenibako.Butterscotch/Deltarune.json"
if [ -f "$DR_INSTANCE" ] && [ -f "$COMMON/deltarune.elf" ]; then
    write_ini deltarune_bench.ini deltarune.elf --bench os25
    sed -e 's/"deltarune_os\.ini"/"deltarune_bench.ini"/' "$DR_INSTANCE" \
        > "$OUT/Assets/butterscotch/zenibako.Butterscotch/Deltarune Benchmark.json"
    # Speed again with game-script times in the log: slower, and there to name the scripts, not to be timed.
    write_ini deltarune_bscr.ini deltarune.elf "--bench-smooth --scripts" os25
    sed -e 's/"deltarune_os\.ini"/"deltarune_bscr.ini"/' "$DR_INSTANCE" \
        > "$OUT/Assets/butterscotch/zenibako.Butterscotch/Deltarune Benchmark scripts.json"
    write_ini deltarune_bspd.ini deltarune.elf --bench-smooth os25
    sed -e 's/"deltarune_os\.ini"/"deltarune_bspd.ini"/' "$DR_INSTANCE" \
        > "$OUT/Assets/butterscotch/zenibako.Butterscotch/Deltarune Benchmark speed.json"
    # Speed with nearly every frame skipped: what the game costs when nothing is drawn.
    write_ini deltarune_bnod.ini deltarune.elf "--bench-smooth --draw-every 100000" os25
    sed -e 's/"deltarune_os\.ini"/"deltarune_bnod.ini"/' "$DR_INSTANCE" \
        > "$OUT/Assets/butterscotch/zenibako.Butterscotch/Deltarune Benchmark no drawing.json"
    # The same program built with other optimisation flags, to see what the compiler alone can do.
    for build in "O3:$ELF_O3" "LTO:$ELF_LTO"; do
        tag="${build%%:*}"; elf="${build#*:}"
        [ -n "$elf" ] && [ -f "$elf" ] || continue
        lower=$(printf '%s' "$tag" | tr 'A-Z' 'a-z')
        cp "$elf" "$COMMON/deltarune_$lower.elf"
        write_ini "deltarune_b$lower.ini" "deltarune_$lower.elf" --bench-smooth os25
        sed -e "s/\"deltarune_os\.ini\"/\"deltarune_b$lower.ini\"/" -e "s/\"deltarune\.elf\"/\"deltarune_$lower.elf\"/" "$DR_INSTANCE" \
            > "$OUT/Assets/butterscotch/zenibako.Butterscotch/Deltarune Benchmark speed $tag.json"
    done
fi

cp "$RT09/os.bin" "$COMMON/os09.bin"
cp "$ELF09" "$COMMON/undertale09.elf"

for variant in os25 os20; do
    name="Butterscotch09$variant"
    core="$OUT/Cores/zenibako.$name"
    rm -rf "$core"
    mkdir -p "$core" "$OUT/Assets/butterscotch/zenibako.$name"
    cp "$BASE_CORE"/*.json "$core/"
    cp "$RT09/$variant.rbf_r" "$RT09/loader.bin" "$core/"
    sed -i -e "s/\"shortname\": \"Butterscotch\"/\"shortname\": \"$name\"/" \
           -e "s/\"description\": \"[^\"]*\"/\"description\": \"Butterscotch, openfpgaOS v0.9 $variant\"/" \
           -e "s/\"filename\": \"[a-z0-9]*\.rbf_r\"/\"filename\": \"$variant.rbf_r\"/" \
           "$core/core.json"

    write_ini "ut09_$variant.ini" undertale09.elf "" "$variant"
    write_ini "ut09_${variant}_bench.ini" undertale09.elf --bench "$variant"
    write_instance "$OUT/Assets/butterscotch/zenibako.$name/Undertale.json" os09.bin "ut09_$variant.ini" undertale09.elf
    write_instance "$OUT/Assets/butterscotch/zenibako.$name/Benchmark.json" os09.bin "ut09_${variant}_bench.ini" undertale09.elf
    # The same benchmark held at 320x240 throughout, in case a bitstream's
    # 640x480 mode is the thing that fails.
    write_ini "ut09_${variant}_b320.ini" undertale09.elf --bench-lowres "$variant"
    write_instance "$OUT/Assets/butterscotch/zenibako.$name/Benchmark 320.json" os09.bin "ut09_${variant}_b320.ini" undertale09.elf
done

echo "Comparison cores added: Butterscotch (v0.7), Butterscotch09os25, Butterscotch09os20"
