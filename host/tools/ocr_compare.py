#!/usr/bin/env python3
"""Check OCR'd screen text against the expected text for a software release.

    python host/tools/ocr_compare.py EXPECTED.txt OCR.txt [MIN_RATIO]

Every expected line with words in it (box-drawing and block characters of the logo are removed first) must appear in the OCR text: the
best-matching OCR line must reach MIN_RATIO (default 0.85, difflib ratio after collapsing whitespace). Prints each
expected line with its score and exits 1 if any line is missing.
"""
import difflib, re, sys


def norm(s):
    s = re.sub(r"[─-▟|]", " ", s)
    return re.sub(r"\s+", " ", s).strip()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    expected = [norm(l) for l in open(sys.argv[1], encoding="utf-8")]
    expected = [l for l in expected if re.search(r"[A-Za-z]{3}", l)]
    ocr = [norm(l) for l in open(sys.argv[2], encoding="utf-8") if norm(l)]
    minimum = float(sys.argv[3]) if len(sys.argv) > 3 else 0.85
    missing = 0
    for line in expected:
        best = max((difflib.SequenceMatcher(None, line, o).ratio() for o in ocr), default=0)
        ok = best >= minimum
        missing += not ok
        print(f"{'ok  ' if ok else 'MISS'} {best:.2f}  {line}")
    print(f"{len(expected) - missing} of {len(expected)} expected lines found (min ratio {minimum})")
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
