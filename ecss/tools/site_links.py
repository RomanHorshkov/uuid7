#!/usr/bin/env python3
"""site_links.py <html dir> <tag file> <links.json> — after doxygen, before the links gate (ecss/make.sh): a documented API symbol or
test that a page names without a link becomes one, to its entry in doxygen's tag file. Doxygen 1.10 and later link the names inside
inline code and member lists themselves; 1.9 (the documentation root's, Ubuntu 24.04) leaves them bare. Only the pages the links gate
reads, only text outside links, headings, column titles, code blocks and prototypes. Prints how many links it added."""
import json
import os
import re
import sys
import xml.etree.ElementTree as ET


def main():
    hdir, tag, reg_path = sys.argv[1:4]
    reg = json.load(open(reg_path, encoding="utf-8"))
    names = set(reg["symbols"]) | set(reg["tests"])
    targets = {}
    if os.path.exists(tag):
        for c in ET.parse(tag).getroot().iter("compound"):
            for m in c.findall("member"):
                n, f, a = m.findtext("name"), m.findtext("anchorfile") or "", m.findtext("anchor") or ""
                if n in names and f and n not in targets:
                    targets[n] = "%s%s#%s" % (f, "" if f.endswith(".html") else ".html", a)
    if not targets:
        print("site_links: no tag file targets")
        return 0
    sym = re.compile(r"(?<![\w.-])(%s)\b(?!\.(?:puml|svg|h|c)\b)" % "|".join(re.escape(n) for n in sorted(targets, key=len, reverse=True)))
    keep = re.compile(r'(<div class="fragment">.*?</div><!-- fragment -->|<div class="memproto">.*?</div>|<pre\b.*?</pre>)', re.S)
    added = 0
    for name in reg["html"]:
        path = os.path.join(hdir, name)
        if not os.path.exists(path):
            continue
        doc = open(path, encoding="utf-8").read()
        m = re.search(r'(<div class="contents">)(.*)(</div><!-- contents -->)', doc, re.S)
        if not m:
            continue
        out = []
        for block in keep.split(m.group(2)):
            if keep.fullmatch(block):
                out.append(block)
                continue
            parts, skip, in_a = re.split(r"(<[^>]+>)", block), 0, 0
            for i, p in enumerate(parts):
                if p.startswith("<"):
                    t = re.match(r"</?([A-Za-z0-9]+)", p)
                    tname, closing = (t.group(1).lower() if t else ""), p.startswith("</")
                    if tname == "a" and not p.endswith("/>"):
                        in_a += -1 if closing else 1
                    elif tname in ("h1", "h2", "h3", "h4", "h5", "h6", "th", "script", "style"):
                        skip += -1 if closing else 1
                    continue
                if in_a or skip or not p.strip():
                    continue

                def link(mm):
                    return '<a class="el" href="%s">%s</a>' % (targets[mm.group(1)], mm.group(1))
                new, n = sym.subn(link, p)
                if n:
                    parts[i], added = new, added + n
            out.append("".join(parts))
        new_doc = doc[:m.start(2)] + "".join(out) + doc[m.end(2):]
        if new_doc != doc:
            with open(path, "w", encoding="utf-8") as f:
                f.write(new_doc)
    print("site_links: %d link(s) added" % added)
    return 0


if __name__ == "__main__":
    sys.exit(main())
