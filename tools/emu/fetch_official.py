#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Fetch M-VAVE's official FM1 Open firmware, for comparison in the emulator.

    python3 tools/emu/fetch_official.py OUT.fwsc [URL]

Looks in the newest release of github.com/m-vave/fm1_open for a .fwsc asset,
a workbench .html (which embeds the package as `const FM1_PACKAGE = {...}`, see
tools/Build-Web.ps1) or a .zip holding either; then in the hosted workbench
page. With URL, reads only that page. Prints "VERSION SOURCE" on success; exits
1 with diagnostics otherwise, so CI can carry on without a reference.
Set GITHUB_TOKEN to avoid API rate limits.
"""
import base64
import hashlib
import io
import json
import os
import re
import sys
import urllib.request
import zipfile

RELEASES = "https://api.github.com/repos/m-vave/fm1_open/releases"
WORKBENCH = "https://open.m-vave.com/fm1-workbench"


def get(url, api=False):
    headers = {"User-Agent": "FM1-Kids-Firmware CI"}
    if api:
        headers["Accept"] = "application/vnd.github+json"
    if api and os.environ.get("GITHUB_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
    return urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=120).read()


def from_html(page):
    """The package embedded in a workbench page, or None."""
    marker = "const FM1_PACKAGE = "
    at = page.find(marker)
    if at < 0:
        return None
    pkg, _ = json.JSONDecoder().raw_decode(page, at + len(marker))
    if not pkg or "data" not in pkg:
        return None
    data = base64.b64decode(pkg["data"])
    if pkg.get("sha256") and hashlib.sha256(data).hexdigest() != pkg["sha256"]:
        sys.exit("fetch_official: firmware SHA-256 does not match the page")
    return data, pkg.get("release_version", "?")


def from_release():
    try:
        releases = json.loads(get(RELEASES, api=True))
    except Exception as e:                                   # network or rate limit: try the page
        print(f"fetch_official: cannot list releases: {e}", file=sys.stderr)
        return None
    for rel in releases:
        names = [a["name"] for a in rel.get("assets", [])]
        print(f"fetch_official: release {rel.get('tag_name')}: {', '.join(names) or 'no assets'}", file=sys.stderr)
        for a in rel.get("assets", []):
            name, url = a["name"], a["browser_download_url"]
            if name.lower().endswith(".fwsc") and "015" not in name:
                return get(url), rel["tag_name"]
            if name.lower().endswith(".html"):
                found = from_html(get(url).decode("utf-8", "replace"))
                if found:
                    return found[0], found[1] or rel["tag_name"]
            if name.lower().endswith(".zip"):
                z = zipfile.ZipFile(io.BytesIO(get(url)))
                for member in z.namelist():
                    low = member.lower()
                    if low.endswith(".fwsc") and "fm-1_015" not in low:
                        return z.read(member), rel["tag_name"]
                    if low.endswith(".html"):
                        found = from_html(z.read(member).decode("utf-8", "replace"))
                        if found:
                            return found
    return None


def from_page(url):
    page = get(url).decode("utf-8", "replace")
    found = from_html(page)
    if found:
        return found
    hints = sorted(set(re.findall(r"""["'`]([^"'`\s]{0,160}(?:\.fwsc|\.json|\.zip|firmware|package|release)[^"'`\s]{0,80})["'`]""", page, re.I)))
    print(f"fetch_official: no FM1_PACKAGE in {url} ({len(page)} bytes). Candidate references:", file=sys.stderr)
    for h in hints[:60]:
        print("  " + h, file=sys.stderr)
    return None


def main():
    out = sys.argv[1]
    if len(sys.argv) > 2:
        found, source = from_page(sys.argv[2]), "page"
    else:
        found, source = from_release(), "GitHub release"
        if not found:
            found, source = from_page(WORKBENCH), "workbench"
    if not found:
        sys.exit(1)
    data, version = found
    open(out, "wb").write(data)
    print(f"{version} ({source})")


if __name__ == "__main__":
    main()
