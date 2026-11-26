"""Renders a TrueType/OpenType font into a C header of anti-aliased glyphs for the demo and launcher panels
(agent/demo/panel_draw.h), so the panels draw smooth text with no font library on the target.

    python host/tools/make_panel_font.py FONT_FILE NAME SOURCE OUT.h [--weight=W]

NAME is the font's name for the header, SOURCE where the font file came from (both recorded in the header). Each
panel `scale` used (2, 3, 5, 6, 8, 10, 12) becomes a size whose capital height is 7 x scale pixels, the height of the
old 5x7 font, so positions on the panels stay as they were. Glyphs are 8-bit coverage, cropped to their ink; the
characters are those the panels draw (letters, digits, + - : . and space; at scale 12, the counter's digits and minus
sign only). --weight sets the Weight axis of a variable font; a font without one is an error. Needs Pillow. The
output is deterministic for a given font file and Pillow/FreeType version, which the header records along with the
font file's SHA-256 and copyright. The glyphs are a Modified Version of the font in the sense of the SIL Open Font
License, so the header says it is under that licence (agent/demo/OFL.txt), not the repository's MIT licence.

agent/demo/panel_font_roboto.h was made with:
    python host/tools/make_panel_font.py Roboto[wdth,wght].ttf "Roboto 3.011 (weight 600)" \\
        "/usr/share/fonts/Roboto[wdth,wght].ttf of the QNX 8.0 quick-start image 20260902 for the Raspberry Pi 5" \\
        agent/demo/panel_font_roboto.h --weight=600
"""
import hashlib
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, features

CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-:."
SCALES = (2, 3, 5, 6, 8, 10, 12)
ONLY = {12: " 0123456789-"}   # scale 12 is the demo panel's counter only: other characters have an advance, no ink
TRACK = 0.04   # extra space after each letter, as a fraction of the capital height (mixed case needs little)


def name_record(path, nid):
    """A Windows-platform name record of the font (0: copyright), or ''."""
    b = Path(path).read_bytes()
    for i in range(struct.unpack(">H", b[4:6])[0]):
        tag, _, off, ln = struct.unpack(">4sIII", b[12 + 16 * i:28 + 16 * i])
        if tag == b"name":
            t = b[off:off + ln]
            count, store = struct.unpack(">HH", t[2:6])
            for k in range(count):
                pid, _, _, n, ln2, o = struct.unpack(">6H", t[6 + 12 * k:18 + 12 * k])
                if pid == 3 and n == nid:
                    return t[store + o:store + o + ln2].decode("utf-16-be")
    return ""


def load(path, px, weight):
    f = ImageFont.truetype(path, px)
    if weight is not None:
        try:
            axes = f.get_variation_axes()
        except OSError:
            axes = []
        if not any(a.get("name") in (b"Weight", "Weight") for a in axes):
            sys.exit(f"{path}: --weight given, but the font has no Weight axis")
        f.set_variation_by_axes([weight if a.get("name") in (b"Weight", "Weight") else a["default"] for a in axes])
    return f


def cap_height(path, px, weight):
    return -load(path, px, weight).getbbox("H", anchor="ls")[1]


def font_for(path, cap, weight):
    """The largest pixel size whose capital height is at most `cap` (and the next if that is closer)."""
    lo, hi = 1, cap * 3
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if cap_height(path, mid, weight) <= cap:
            lo = mid
        else:
            hi = mid - 1
    best = min((lo, lo + 1), key=lambda s: abs(cap_height(path, s, weight) - cap))
    return load(path, best, weight), best


def main(a):
    weight = None
    for x in [x for x in a if x.startswith("--weight=")]:
        weight = int(x.split("=", 1)[1])
        a.remove(x)
    if len(a) != 4:
        sys.exit(__doc__)
    path, name, source, out = a
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    copyright = name_record(path, 0)
    alpha, sizes = bytearray(), []
    for sc in SCALES:
        cap = 7 * sc
        f, px = font_for(path, cap, weight)
        track = round(TRACK * cap)
        glyphs = []
        for ch in CHARS:
            adv = round(f.getlength(ch)) + track
            box = f.getbbox(ch, anchor="ls")  # relative to the pen on the baseline; spans pen to advance
            w, h = box[2] - box[0], box[3] - box[1]
            ink = None
            if ch != " " and w > 0 and h > 0 and ch in ONLY.get(sc, ch):
                im = Image.new("L", (w, h), 0)
                ImageDraw.Draw(im).text((-box[0], -box[1]), ch, font=f, fill=255, anchor="ls")
                ink = im.getbbox()  # the inked part only
            if not ink:
                glyphs.append((adv, 0, 0, 0, 0, len(alpha)))
                continue
            im = im.crop(ink)
            # x from the pen, y from the top of the capitals (cap pixels above the baseline)
            glyphs.append((adv, box[0] + ink[0], box[1] + ink[1] + cap, im.width, im.height, len(alpha)))
            alpha += im.tobytes()
        sizes.append((sc, px, track, glyphs))
    lines = [
        f"/* {Path(out).name}: {name} rendered for agent/demo/panel_draw.h by host/tools/make_panel_font.py.",
        " * Generated: do not edit.",
        f" * Font: {source},",
        f" * SHA-256 {digest}; weight {weight if weight is not None else 'as in the file'};",
        f" * Pillow {Image.__version__}, FreeType {features.version('freetype2')}.",
        f" * {copyright}" if copyright else " * (the font file names no copyright)",
        " * These glyphs are rendered from the font and, as a Modified Version of it, this file is licensed under the",
        " * SIL Open Font License, Version 1.1 (agent/demo/OFL.txt), not under the MIT licence of the rest of the",
        " * repository. */",
        f'#define PF_NAME "{name}"',
        f'static const char PF_CHARS[] = "{CHARS}";',
        f"static const unsigned char PF_ALPHA[{len(alpha)}] = {{",
    ]
    for i in range(0, len(alpha), 32):
        lines.append("\t" + ",".join(str(b) for b in alpha[i:i + 32]) + ",")
    lines.append("};")
    for sc, px, track, glyphs in sizes:
        lines.append(f"static const struct pf_glyph PF_G{sc}[] = {{ /* {px} px, capitals {7 * sc} px, +{track} px */")
        for ch, (adv, x0, y0, w, h, off) in zip(CHARS, glyphs):
            lines.append(f"\t{{ {adv}, {x0}, {y0}, {w}, {h}, {off} }}, /* {ch!r} */")
        lines.append("};")
    lines.append("static const struct pf_size PF_SIZES[] = {")
    lines += [f"\t{{ {sc}, PF_G{sc} }}," for sc, *_ in sizes]
    lines.append("};")
    Path(out).write_text("\n".join(lines) + "\n", newline="\n")
    print(f"{out}: {len(alpha)} bytes of glyphs, sizes " + ", ".join(f"{sc}:{px}px" for sc, px, *_ in sizes))


if __name__ == "__main__":
    main(sys.argv[1:])
