#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# After Felucca's web/make_site.py, by Leo Kuroshita (@kurogedelic), Hügelton Instruments.
"""Make the installer site: a static folder to serve as it is (GitHub Pages, any web server).

  OUT/index.html, fm1ota.js, fm1pkg.js, mdlink.js   the installer page
  OUT/firmware/moondaw-VERSION.fwsc                 the package
  OUT/firmware/manifest.json                        what the page installs: version, identity, size, sha256
  OUT/firmware/LICENSE, LICENSES/                   GPL-3.0 and the licences of what the package holds
                                                    (three JieLi SDK files are Apache-2.0)

  web/installer/make_site.py build/moondaw.fwsc VERSION OUT_DIR [--source URL]

The package is served from the same site as the page: the page reads it with
fetch(), which a release asset on another host (GitHub Releases) does not allow.
It must be one made by tools/fm1pkg_make.py (Felucca's loader, no vendor files)
whose identity is MOONDAW's (FM-1_933 and the version). --source is the URL of the
source code, linked
from the page (GPL-3.0).
"""
import argparse
import hashlib
import html
import json
import re
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
PAGE = ("index.html", "fm1ota.js", "fm1pkg.js", "mdlink.js")


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", type=Path)
    ap.add_argument("version")
    ap.add_argument("out", type=Path)
    ap.add_argument("--source", help="URL of MOONDAW's source code")
    a = ap.parse_args()
    raw = a.package.read_bytes()
    product = product_of(raw)
    if not product.startswith("FM-1_933"):
        raise SystemExit(f"{a.package}: identity {product!r}, MOONDAW's begins with 'FM-1_933'")
    if b"FELUCCA-LOADER-1" not in raw:          # never publish a package with vendor files in it
        raise SystemExit(f"{a.package}: no Felucca loader in it; the site ships only fm1pkg_make.py packages")
    if not re.fullmatch(r"[A-Za-z0-9.+-]+", a.version):
        raise SystemExit(f"version {a.version!r}: letters, digits, '.', '+', '-' only")
    out, fw = a.out, a.out / "firmware"
    fw.mkdir(parents=True, exist_ok=True)
    for f in PAGE:
        shutil.copy(HERE / f, out / f)
    for old in fw.glob("moondaw-*.fwsc"):       # one package: the current one
        old.unlink()
    name = f"moondaw-{a.version}.fwsc"
    shutil.copy(a.package, fw / name)
    manifest = {"name": "MOONDAW", "version": a.version, "product": product, "file": name,
                "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
    if a.source:
        manifest["source"] = a.source
    (fw / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    lic = fw / "LICENSES"
    lic.mkdir(exist_ok=True)
    names = sorted(f.name for f in (ROOT / "LICENSES").glob("*.txt"))
    for n in names:
        shutil.copy(ROOT / "LICENSES" / n, lic / n)
    shutil.copy(ROOT / "LICENSE", fw / "LICENSE")
    (lic / "index.html").write_text(            # static hosts list no folders
        '<!doctype html><meta charset="utf-8"><title>MOONDAW licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{html.escape(n)}">{html.escape(n)}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n', encoding="utf-8")
    print(f"site: {out}: index.html, firmware/{name} ({len(raw)} B, {product}), {len(names)} licences")


if __name__ == "__main__":
    main()
