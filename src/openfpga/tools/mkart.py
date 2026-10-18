#!/usr/bin/env python3
"""
mkart.py — draws the core's menu images for the Analogue Pocket.

    mkart.py <dist directory> [preview directory]

Writes Platforms/_images/butterscotch.bin (521x165, the banner shown in the
openFPGA menu) and Cores/<core>/icon.bin (36x36). With a preview directory
it also writes PNGs of both as the Pocket shows them.

The artwork is drawn here from scratch (a block-letter wordmark and a
heart), so nothing from any game or from Butterscotch is redistributed.

File format, taken from the SDK's own images: two bytes per pixel, the first
holding 255 for the dark background down to 0 for full white, the second
zero; the picture is stored rotated a quarter turn anticlockwise.
"""

import sys
from pathlib import Path

from PIL import Image, ImageFilter

# Original heavy glyphs on a 9x9 grid, with stepped outer corners.
LETTERS = {
    "B": [".#######.", "#########", "###...###", "########.", "########.", "###...###", "###...###", "#########", "########."],
    "U": ["###...###"] * 7 + ["#########", ".#######."],
    "T": ["#########"] * 3 + ["...###..."] * 6,
    "E": ["#########"] * 2 + ["###......"] * 2 + ["########."] + ["###......"] * 2 + ["#########"] * 2,
    # The solid bowl gives the gray heart a white surround, as in the reference.
    "R": ["########.", "#########", "#########", "#########", "########.", "######...", "###.###..", "###..###.", "###...###"],
    "S": [".########", "#########"] + ["###......"] * 2 + ["#########"] + ["......###"] * 2 + ["#########", "########."],
    "C": [".########", "#########"] + ["###......"] * 5 + ["#########", ".########"],
    "O": [".#######.", "#########"] + ["###...###"] * 5 + ["#########", ".#######."],
    "H": ["###...###"] * 3 + ["#########"] * 3 + ["###...###"] * 3,
    "P": ["########.", "#########", "###...###", "###...###", "#########", "########.", "###......", "###......", "###......"],
    "K": ["###...###", "###..###.", "###.###..", "######...", "#####....", "######...", "###.###..", "###..###.", "###...###"],
}

WORDMARK_HEART = ["##.##", "#####", ".###.", "..#.."]
HEART_GRAY = 160

HEART = [
    "..##...##..",
    ".####.####.",
    "###########",
    "###########",
    "###########",
    ".#########.",
    "..#######..",
    "...#####...",
    "....###....",
    ".....#.....",
]


def stamp(image, rows, left, top, scale, value=255):
    """Draw a bitmap of '#' cells, each cell scale x scale pixels."""
    for y, row in enumerate(rows):
        for x, cell in enumerate(row):
            if cell != "#":
                continue
            for dy in range(scale):
                for dx in range(scale):
                    image.putpixel((left + x * scale + dx, top + y * scale + dy), value)


def word_width(word, scale):
    return (len(word) * 10 - 1) * scale


def draw_word(image, word, top, scale, heart=False):
    left = (image.width - word_width(word, scale)) // 2
    for i, letter in enumerate(word):
        letter_left = left + i * 10 * scale
        stamp(image, LETTERS[letter], letter_left, top, scale)
        if heart and letter == "R":
            stamp(image, WORDMARK_HEART, letter_left + 3 * scale,
                  top + scale, scale // 2, HEART_GRAY)


def banner():
    image = Image.new("L", (521, 165), 0)
    # Render large, add weight, then downsample while preserving square glyphs.
    # The Pocket's luminance format preserves the intermediate gray pixels.
    word = "BUTTERSCOTCH"
    scale = 8
    wordmark = Image.new("L", (word_width(word, scale), 9 * scale), 0)
    draw_word(wordmark, word, 0, scale)
    wordmark = wordmark.filter(ImageFilter.MaxFilter(3))
    heart_scale = scale // 2
    heart_left = (9 * scale - len(WORDMARK_HEART[0]) * heart_scale) // 2
    heart_top = (5 * scale - len(WORDMARK_HEART) * heart_scale) // 2 + 2
    stamp(wordmark, WORDMARK_HEART, word.index("R") * 10 * scale + heart_left,
          heart_top, heart_scale, HEART_GRAY)
    # Keep the 9x9 glyphs square when fitting the wordmark to the banner.
    width = 500
    height = round(wordmark.height * width / wordmark.width)
    wordmark = wordmark.resize((width, height), Image.Resampling.LANCZOS)

    small, gap = 2, 16
    top = (image.height - (wordmark.height + gap + 9 * small)) // 2
    image.paste(wordmark, ((image.width - wordmark.width) // 2, top))
    draw_word(image, "POCKET", top + wordmark.height + gap, small)
    # A subpixel reconstruction filter leaves visible gray coverage at edges,
    # including grid-aligned strokes that resizing alone keeps nearly binary.
    return image.filter(ImageFilter.GaussianBlur(0.65))


def icon():
    image = Image.new("L", (36, 36), 0)
    scale = 3
    stamp(image, HEART, (36 - len(HEART[0]) * scale) // 2, (36 - len(HEART) * scale) // 2, scale)
    return image


def write_bin(image, path):
    """image is upright, white (255) on black (0), as the Pocket displays it."""
    stored = image.rotate(90, expand=True)
    data = bytearray()
    for value in stored.tobytes():
        data += bytes((255 - value, 0))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    dist = Path(sys.argv[1])
    cores = [p for p in (dist / "Cores").iterdir() if p.is_dir()]
    if len(cores) != 1:
        sys.exit(f"mkart: expected one core under {dist / 'Cores'}")

    images = {"banner": (banner(), dist / "Platforms/_images/butterscotch.bin"), "icon": (icon(), cores[0] / "icon.bin")}
    for name, (image, path) in images.items():
        write_bin(image, path)
        print(f"{path} ({image.width}x{image.height})")
        if len(sys.argv) == 3:
            preview = Path(sys.argv[2])
            preview.mkdir(parents=True, exist_ok=True)
            image.save(preview / f"{name}.png")


if __name__ == "__main__":
    main()
