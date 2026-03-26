#!/usr/bin/env python3

"""Render font glyphs to individual PNG sprite files."""

import argparse
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT_DIRS = [
    # macOS
    "/System/Library/Fonts",
    "/Library/Fonts",
    os.path.expanduser("~/Library/Fonts"),
    # Linux
    "/usr/share/fonts",
    "/usr/local/share/fonts",
    os.path.expanduser("~/.local/share/fonts"),
]

CHARS = {
    "0": "0",
    "1": "1",
    "2": "2",
    "3": "3",
    "4": "4",
    "5": "5",
    "6": "6",
    "7": "7",
    "8": "8",
    "9": "9",
    "dot": ".",
    "minus": "-",
}


def find_font(name):
    """Resolve a font file path or system font name."""
    if os.path.isfile(name):
        return name

    # Search system font directories for matching filename
    for d in FONT_DIRS:
        for path in glob.glob(os.path.join(d, "**"), recursive=True):
            if os.path.isfile(path) and name.lower() in os.path.basename(path).lower():
                return path

    print(f"error: font not found: {name}", file=sys.stderr)
    sys.exit(1)


def render(font_path, size, out_dir, prefix=""):
    font_path = find_font(font_path)
    font = ImageFont.truetype(font_path, size)
    os.makedirs(out_dir, exist_ok=True)

    # Measure all glyphs to find common vertical extent
    bboxes = {}
    min_top = None
    max_bottom = None
    for name, ch in CHARS.items():
        bbox = font.getbbox(ch)
        bboxes[name] = bbox
        if min_top is None or bbox[1] < min_top:
            min_top = bbox[1]
        if max_bottom is None or bbox[3] > max_bottom:
            max_bottom = bbox[3]

    total_h = max_bottom - min_top

    for name, ch in CHARS.items():
        bbox = bboxes[name]
        w = bbox[2] - bbox[0]

        img = Image.new("1", (w, total_h), 1)
        draw = ImageDraw.Draw(img)
        draw.text((-bbox[0], -min_top), ch, fill=0, font=font)

        path = os.path.join(out_dir, f"{prefix}{name}.png")
        img.save(path)
        print(f"{path}  {w}x{total_h}")


def main():
    parser = argparse.ArgumentParser(description="Render font glyphs to PNGs.")
    parser.add_argument("font", help="path to TTF/OTF font file")
    parser.add_argument("size", type=int, help="font size in points")
    parser.add_argument("outdir", help="output directory for PNGs")
    parser.add_argument("--prefix", default="", help="filename prefix (e.g. 'cs_')")
    args = parser.parse_args()

    render(args.font, args.size, args.outdir, args.prefix)


if __name__ == "__main__":
    main()
