#!/usr/bin/env python3
"""Make docs/OCULIX_MASTER_TABLE.pdf from the master table section of docs/OCULIX_VNC_MATRIX.md.

    python host/tools/master_table_pdf.py

Takes the text from "## Master table" up to "## The table", turns its opening into a standalone title that points
the reader at the matrix for the details, converts it with pandoc and prints it with a headless Edge or Chrome
(A3 landscape, so the wide tables fit). Stops if the section's opening has been reworded, so that a changed
sentence cannot end up half-rewritten in the PDF. Needs pandoc and Edge or Chrome.
"""
import datetime, pathlib, re, shutil, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC, OUT = ROOT / "docs/OCULIX_VNC_MATRIX.md", ROOT / "docs/OCULIX_MASTER_TABLE.pdf"
BROWSERS = [r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
            r"C:\Program Files\Google\Chrome\Application\chrome.exe", "msedge", "google-chrome", "chromium"]
HEAD = "## Master table: what was measured, per host, target and configuration"
POINTER = re.compile(r"The sections below hold\s+the details, numbers and sources\. A printable copy of this section is "
                     r"\[OCULIX_MASTER_TABLE\.pdf\]\(OCULIX_MASTER_TABLE\.pdf\)\s*\([^)]*\)\.")
CSS = """
@page { size: A3 landscape; margin: 12mm; }
body { font-family: "Segoe UI", Arial, sans-serif; font-size: 10.5pt; color: #111; }
h1 { font-size: 18pt; margin: 0 0 4pt; } h2 { font-size: 14pt; margin-top: 10pt; } h3 { font-size: 12.5pt; margin-top: 14pt; }
table { border-collapse: collapse; width: 100%; margin: 6pt 0 10pt; page-break-inside: auto; }
tr { page-break-inside: avoid; }
th, td { border: 1px solid #999; padding: 3pt 4pt; vertical-align: top; text-align: left; }
th { background: #dde4ee; } tr:nth-child(even) td { background: #f5f7fa; }
td:first-child { font-weight: 600; white-space: nowrap; } td:nth-child(2) { white-space: nowrap; }
code { font-family: Consolas, monospace; font-size: 9.5pt; background: #eef; padding: 0 1pt; }
strong { color: #8a1c00; }
"""

text = SRC.read_text(encoding="utf-8")
section = text[text.index("## Master table"):text.index("## The table")]
if section.count(HEAD) != 1 or len(POINTER.findall(section)) != 1:
    sys.exit("the master table's opening has changed: update HEAD and POINTER in master_table_pdf.py to match")
section = section.replace(HEAD, "# OculiX 4.0.0 over VNC: master table\n\n"
                                "## What was measured, per host, target and configuration")
section = POINTER.sub("The details, numbers and sources are in `docs/OCULIX_VNC_MATRIX.md` in the OpticNerve "
                      f"repository (this copy was made on {datetime.date.today().isoformat()}).", section)

browser = next((b for b in BROWSERS if shutil.which(b) or pathlib.Path(b).is_file()), None)
if not browser or not shutil.which("pandoc"):
    sys.exit("needs pandoc and Edge or Chrome")
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "master.md").write_text(section, encoding="utf-8")
    (d / "style.css").write_text(CSS, encoding="utf-8")
    subprocess.run(["pandoc", "master.md", "-f", "gfm", "-t", "html5", "-s", "--metadata", "pagetitle=OculiX master table",
                    "-c", "style.css", "--embed-resources", "-o", "master.html"], cwd=d, check=True)
    # Edge only writes the PDF when its own output goes to a pipe, so capture it.
    r = subprocess.run([browser, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                        f"--print-to-pdf={d / 'master.pdf'}", (d / "master.html").as_uri()],
                       capture_output=True, text=True)
    pdf = d / "master.pdf"
    if not pdf.is_file() or pdf.stat().st_size < 50_000:
        sys.exit("no PDF written:\n" + r.stderr[-2000:])
    shutil.copyfile(pdf, OUT)
print(f"{OUT.relative_to(ROOT)}: {OUT.stat().st_size} bytes")
