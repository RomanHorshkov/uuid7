#!/usr/bin/env python3
"""links_gate.py <html dir> <links.json> <doxygen warnings> — the links gate of ecss/make.sh: every reference the reader meets in
the site must be a link. It reads the HTML of every page made from the documents (and the component and trace pages) and reports
each reference that is not inside a link: a section (<9.7>), a document name or id, a requirement, component, verification case,
review, RID, change request, open item or baseline, an ECSS clause, a tool, an evidence file, a source file, an API symbol, a test,
a figure or a table. Definitions are not references: headings, the anchored cell of a register row, a figure's or a table's own
number. Code blocks are not read. A link doxygen could not resolve is a broken anchor and fails too.
Exit 1 with the list (page: kind 'text' in '...context...'), else 0 with the count of links read."""
import html
import json
import os
import re
import sys

STD = (r"ECSS-E-ST-40C Rev\.1|ECSS-E-ST-40C|ECSS-E-ST-40|E-ST-40|E-40|ECSS-Q-ST-80C Rev\.2|ECSS-Q-ST-80C|ECSS-Q-ST-80|Q-ST-80|Q-80|"
       r"ECSS-M-ST-40C|ECSS-M-ST-40")


def words(names):
    return "|".join(re.escape(n) for n in sorted(set(names), key=len, reverse=True)) or "@@none@@"


def main():
    hdir, reg_path, warn_path = sys.argv[1:4]
    reg = json.load(open(reg_path, encoding="utf-8"))
    clauses = set(reg["clauses"])
    text_rules = [
        ("section", re.compile(r"<\d+(?:\.\d+)*>")),
        ("ECSS clause", re.compile(r"(?<![\w-])(?:" + STD + r")(?:\s+(?:clauses?\s+)?(?:\d+(?:\.\d+)+[a-z]?|Annex(?:es)? [A-Z][\w.-]*|"
                                                           r"Table [A-Z]-\d+|Figure [A-Z]-\d+))?(?![\w-])")),
        ("document", re.compile(r"(?<![\w-])(?:" + words(reg["abbr"]) + r")(?![\w-])")),
        ("review", re.compile(r"(?<![\w-])(?:" + words(reg["reviews"]) + r")(?![\w-])")),
        ("ECSS file", re.compile(r"(?<![\w-])(?:" + words(reg["files"]) + r")(?![\w-])")),
        ("tool", re.compile(r"(?<![\w/.-])(?:" + words(reg["tools"]) + r")(?![\w/-]|\.\w)")),
        ("figure or table", re.compile(r"\b(?:Figure|Table) \d+\b(?![-.]\w)")),
    ]
    any_rules = [
        ("identifier", re.compile(r"\bUUID7-[A-Z]+-[A-Z0-9][A-Za-z0-9_]*(?:-[A-Za-z0-9_]+)*|\bOI-\d{2}\b")),
        ("evidence file", re.compile(r"(?<![\w/.-])(?:build/ecss/)?evidence/[\w./-]*\w")),
        ("source file", re.compile(r"(?<![\w/.-])(?:" + words(reg["sources"]) + r")(?![\w/-]|\.\w)")),
        ("API symbol", re.compile(r"(?<![\w.-])(?:" + words(reg["symbols"]) + r")\b(?!\.(?:puml|svg|h|c)\b)")),
        ("test", re.compile(r"(?<![\w.-])(?:" + words(reg["tests"]) + r")\b(?!\.(?:puml|svg)\b)")),
    ]
    bare_clause = re.compile(r"(?<![\w.;&<>-])\d\.\d+(?:\.\d+)+[a-z]?(?![\w.>])")
    findings, links, pages = [], 0, 0
    for name in reg["html"]:
        path = os.path.join(hdir, name)
        if not os.path.exists(path):
            findings.append((name, "page", "missing", ""))
            continue
        pages += 1
        doc = open(path, encoding="utf-8").read()
        m = re.search(r'<div class="contents">(.*)</div><!-- contents -->', doc, re.S)
        body = m.group(1) if m else doc
        links += len(re.findall(r"<a\b[^>]*\bhref=", body))
        body = re.sub(r'<div class="fragment">.*?</div><!-- fragment -->', " ", body, flags=re.S)
        body = re.sub(r"<pre\b.*?</pre>", " ", body, flags=re.S)
        body = re.sub(r"<h([1-6])\b.*?</h\1>", " ", body, flags=re.S)
        body = re.sub(r'<span class="ecss-def">.*?</span>', " ", body, flags=re.S)
        body = re.sub(r"<a\b[^>]*\bhref=[^>]*>.*?</a>", " ", body, flags=re.S)
        body = re.sub(r'<div class="(?:memproto|memdoc-head)">.*?</div>', " ", body, flags=re.S)
        code = re.findall(r'<span class="tt">(.*?)</span>|<code>(.*?)</code>', body, flags=re.S)
        code_text = html.unescape(re.sub(r"<[^>]+>", " ", " ".join(a or b for a, b in code)))
        body = re.sub(r'<span class="tt">.*?</span>|<code>.*?</code>', " ", body, flags=re.S)
        text = html.unescape(re.sub(r"<[^>]+>", " ", body))
        text = re.sub(r"[ \t]+", " ", text)
        # a pattern of identifiers (UUID7-TC-IT-*) is not one of them
        text, code_text = (re.sub(r"\bUUID7-[A-Za-z0-9_-]*\*", " ", t) for t in (text, code_text))

        def ctx(src, mm):
            return src[max(0, mm.start() - 50):mm.end() + 30].replace("\n", " ").strip()
        for kind, rx in text_rules:
            for mm in rx.finditer(text):
                findings.append((name, kind, mm.group(0), ctx(text, mm)))
        for kind, rx in any_rules:
            for src in (text, code_text):
                for mm in rx.finditer(src):
                    findings.append((name, kind, mm.group(0), ctx(src, mm)))
        for mm in bare_clause.finditer(text):
            c = mm.group(0)
            if c in clauses or any(k.startswith(c) and len(k) > len(c) and (k[len(c)].isalpha() or k[len(c)] in ". ") for k in clauses):
                findings.append((name, "ECSS clause", c, ctx(text, mm)))
    for line in open(warn_path, encoding="utf-8") if os.path.exists(warn_path) else []:
        if "could not be resolved" in line or "unable to resolve reference" in line:
            findings.append((os.path.basename(line.split(":")[0]), "broken anchor", line.strip(), ""))
    for p, t in reg.get("unresolved", []):
        findings.append((p, "unresolved reference", t, "no target in the package"))
    if findings:
        for f in findings:
            print("  %s: %s '%s'%s" % (f[0], f[1], f[2], (" in '…%s…'" % f[3]) if f[3] else ""))
        print("links: RED, %d bare or broken reference(s) in %d pages (%d links read)" % (len(findings), pages, links))
        return 1
    print("links: GREEN, %d links in %d pages, no bare reference" % (links, pages))
    return 0


if __name__ == "__main__":
    sys.exit(main())
