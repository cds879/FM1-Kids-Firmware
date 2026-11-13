#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Adapt a built workbench page into an FM1 Kids install page.

    python3 tools/pages/customize.py IN.html OUT.html "FM1 Kids 0.1"

M-VAVE's template (sdk/Update.html.in) is hash-pinned by the SDK, so the
built page is adjusted instead: it names the firmware, opens Device update
when the address ends in #install, and puts plain install steps (including
the force-flash step needed when coming from other firmware) and connection
help at the top of Device update. Every change must match exactly once.
"""
import html
import json
import pathlib
import sys

GUIDE = """
<div id="kids-guide" style="border:1px solid #a9efd7;border-radius:10px;padding:16px 18px;margin:0 0 20px;background:#16302a;color:#d9f3e8;line-height:1.6">
<p style="margin:0 0 8px;color:#e9fff6;font-size:17px"><strong>Install @LABEL@</strong></p>
<ol style="margin:0 0 10px;padding-left:22px;color:#d9f3e8">
<li>Switch the FM-1 on and connect it straight to the computer with a USB-C <em>data</em> cable. Close other MIDI apps and other tabs with this page.</li>
<li>Press <strong>Connect device</strong> below. Allow MIDI devices when Chrome asks.</li>
<li>Coming from other firmware (stock V15, FM1 Open, Felucca, ...)? The page says other firmware was detected:
press <strong>Enable force flash and reconnect</strong>.</li>
<li>Tick <strong>I have backed up ...</strong>, press <strong>Force flash</strong> (or <strong>Start update</strong>) and confirm.</li>
<li>Keep the FM-1 on and connected until the page says the update is complete.</li>
</ol>
<p style="margin:0 0 8px;color:#ffe0ac">Don't use <strong>Restore original V15</strong> unless you want M-VAVE's original firmware back.</p>
<details style="color:#cfe6dc"><summary style="cursor:pointer">It won't connect</summary>
<ul style="margin:8px 0 0;padding-left:22px">
<li>Switch the FM-1 off and on, unplug and replug the cable, then press <strong>Connect device</strong> again.</li>
<li>Only one page or app can talk to the FM-1 at a time: close other tabs with a workbench page, DAWs and MIDI tools.</li>
<li>Use a cable that carries data (some USB-C cables only charge), with no hub in between.</li>
<li>If MIDI was blocked: click the icon left of the address bar, allow MIDI devices, and reload.</li>
<li>Unplug other MIDI devices if several are attached.</li>
<li><strong>Connection details</strong> (below the buttons) lists what the browser sees.</li>
</ul></details>
</div>
"""

ROUTE = """<script>
/* FM1 Kids: open Device update when the address ends in #install. */
if (location.hash === '#install') document.getElementById('mode-update')?.click();
</script>
"""


def replace_once(text, old, new):
    count = text.count(old)
    if count != 1:
        sys.exit(f"customize: expected 1 x {old[:70]!r}, found {count}; the workbench template changed")
    return text.replace(old, new)


def main():
    src, dst, label = sys.argv[1], sys.argv[2], sys.argv[3]
    page = pathlib.Path(src).read_text(encoding="utf-8")
    page = replace_once(page, "pkg.kind==='stock'?'原厂 V15（FM-1_015）':'官方 Open 固件'",
                        "pkg.kind==='stock'?'原厂 V15（FM-1_015）':" + json.dumps(label))
    page = replace_once(page, '<section id="tab-update" role="tabpanel" aria-labelledby="mode-update" hidden>',
                        '<section id="tab-update" role="tabpanel" aria-labelledby="mode-update" hidden>' +
                        GUIDE.replace("@LABEL@", html.escape(label)))
    end = page.rstrip().rfind("</html>")
    if end < 0:
        sys.exit("customize: no </html>")
    page = page[:end] + ROUTE + page[end:]
    pathlib.Path(dst).write_text(page, encoding="utf-8")
    print(f"customize: {dst} ({label})")


if __name__ == "__main__":
    main()
