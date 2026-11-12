#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Extract M-VAVE's official FM1 Open firmware from their workbench page.

    python3 tools/emu/fetch_official.py OUT.fwsc [URL]

The workbench page embeds the firmware package as `const FM1_PACKAGE = {...}`
(see tools/Build-Web.ps1). Prints the release version on success; exits 1 with
a message if the page has no package, so CI can carry on without a reference.
"""
import base64
import hashlib
import json
import sys
import urllib.request

URL = "https://open.m-vave.com/fm1-workbench"


def main():
    out = sys.argv[1]
    url = sys.argv[2] if len(sys.argv) > 2 else URL
    req = urllib.request.Request(url, headers={"User-Agent": "FM1-Kids-Firmware CI"})
    page = urllib.request.urlopen(req, timeout=60).read().decode("utf-8", "replace")
    marker = "const FM1_PACKAGE = "
    at = page.find(marker)
    if at < 0:
        sys.exit(f"fetch_official: no FM1_PACKAGE in {url} ({len(page)} bytes); the page layout changed")
    pkg, _ = json.JSONDecoder().raw_decode(page, at + len(marker))
    if not pkg or "data" not in pkg:
        sys.exit("fetch_official: FM1_PACKAGE holds no firmware")
    data = base64.b64decode(pkg["data"])
    if pkg.get("sha256") and hashlib.sha256(data).hexdigest() != pkg["sha256"]:
        sys.exit("fetch_official: firmware SHA-256 does not match the page")
    open(out, "wb").write(data)
    print(f"{pkg.get('release_version', '?')} {pkg.get('product', '?')} {len(data)} bytes")


if __name__ == "__main__":
    main()
