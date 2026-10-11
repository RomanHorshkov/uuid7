#!/usr/bin/env python3
"""package.py <out> — the release package of uuid7 (ecss/README.md, "Release package") from what make.sh --docs-only built in
build/ecss: every document of build/ecss/md through pandoc to PDF (WeasyPrint), DOCX and ODT, the doxygen site as html/, a package
index (index.html and index.pdf/.docx/.odt) and SHA256SUMS over every file. Run inside the documentation root (ecss/docs_root.sh)
by ecss/release.sh, from the repository root.

The Markdown is the doxygen dialect the site reads; dialect() rewrites what doxygen alone understands (\\ref, \\anchor, \\verbatim,
the HTML of figures, captions and lists) into pandoc's, every reference into a link: to the same document, to the other document
of the same format, or to the site for the API reference and the evidence.

Environment: ECSS_RELEASE (the baseline's name), ECSS_RELEASE_TAG (empty: untagged), SOURCE_DATE_EPOCH,
KYR_BUILD_ROOT_ID (the documentation root's identity). Only the standard library, pandoc, rsvg-convert and weasyprint.
"""
import datetime
import glob
import hashlib
import html
import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "build", "ecss")
MD = os.path.join(OUT, "md")
WORK = os.path.join(ROOT, "build", "tmp", "package")
FMT = ("markdown-implicit_figures-raw_tex-tex_math_dollars-citations-subscript-superscript-strikeout-yaml_metadata_block"
       "-example_lists")
EXT = {"pdf": "pdf", "docx": "docx", "odt": "odt", "html": "html"}


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def run(cmd, **kw):
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)
    if p.returncode != 0:
        sys.exit("package.py: %s failed (%d): %s" % (" ".join(cmd[:3]), p.returncode, (p.stderr or p.stdout).strip()[-2000:]))
    return p.stdout


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


# ---------------------------------------------------------------------------------------------------------------------------------
# The documents and where every reference goes
# ---------------------------------------------------------------------------------------------------------------------------------

def pages():
    """[(rel md path, output name)] in the package's order: the front page, the documents, the lists, the pipeline's README."""
    out = []
    for p in sorted(glob.glob(os.path.join(MD, "**", "*.md"), recursive=True)):
        r = os.path.relpath(p, MD)
        name = {"index.md": "FRONT", "lists/figures.md": "LIST_FIGURES", "lists/tables.md": "LIST_TABLES"}.get(r, os.path.basename(r)[:-3])
        out.append((r, name))
    order = {"FRONT": 0, "README": 3, "LIST_FIGURES": 2, "LIST_TABLES": 2}
    return sorted(out, key=lambda x: (order.get(x[1], 1), x[0]))


def md_anchors(texts):
    """{anchor: (output name, is the page's own label)} of every heading id and \\anchor of the documents."""
    where = {}
    for name, text in texts.items():
        first = True
        for m in re.finditer(r"^#+ .*?\{#([\w-]+)\}\s*$|(?<!\\)\\anchor\s+([\w-]+)", text, re.M):
            a = m.group(1) or m.group(2)
            where.setdefault(a, (name, first and bool(m.group(1))))
            first = False
    return where


def tag_targets(path):
    """{name: (html file, anchor)} from doxygen's tag file: pages, files, groups, their members and anchors."""
    out = {}
    if not os.path.exists(path):
        return out

    def html_of(f):
        return f if not f or f.endswith(".html") else f + ".html"
    for c in ET.parse(path).getroot().iter("compound"):
        f = html_of(c.findtext("filename") or "")
        name = c.findtext("name") or ""
        out.setdefault(name, (f, ""))
        for da in c.findall("docanchor"):
            out.setdefault(da.text, (html_of(da.get("file") or f[:-5]), da.text))
        for m in c.findall("member"):
            out.setdefault(m.findtext("name") or "", (html_of(m.findtext("anchorfile") or ""), m.findtext("anchor") or ""))
    return out


class Links:
    def __init__(self, texts):
        self.md = md_anchors(texts)
        self.dox = tag_targets(os.path.join(OUT, "uuid7.tag"))
        self.site_pages = {}
        for name in texts:
            label = next((a for a, (n, own) in self.md.items() if n == name and own), None)
            self.site_pages[name] = self.dox.get(label, ("index.html", ""))[0] if label else "index.html"
        self.unresolved = set()

    def url(self, target, here, fmt):
        t = target[:-2] if target.endswith("()") else target
        if t in self.md:
            name, own = self.md[t]
            if fmt == "html":   # the site: the page doxygen made of that document
                f, a = self.dox.get(t, (self.site_pages.get(name, "index.html"), t))
                return "html/%s%s" % (f, ("#" + a) if a else "")
            if name == here:
                return "#" + t
            return "%s.%s%s" % (name, EXT[fmt], "" if own else "#" + t)
        if t in self.dox and self.dox[t][0]:
            f, a = self.dox[t]
            return "%shtml/%s%s" % ("" if fmt == "html" else "../", f, ("#" + a) if a else "")
        if os.path.exists(os.path.join(OUT, "html", t + ".html")):   # a page doxygen keeps out of its tag file (\xrefitem lists)
            return "%shtml/%s.html" % ("" if fmt == "html" else "../", t)
        self.unresolved.add(target)
        return None


# ---------------------------------------------------------------------------------------------------------------------------------
# The doxygen dialect into pandoc's
# ---------------------------------------------------------------------------------------------------------------------------------

def link_text(t):
    return t.replace("\\", "\\\\").replace("[", "\\[").replace("]", "\\]").replace("<", "&lt;").replace(">", "&gt;")


def inline(seg, here, fmt, L):
    """One stretch of text outside code spans."""
    def ref(m):
        target, text = m.group(1), m.group(2)
        u = L.url(target, here, fmt)
        shown = link_text(text if text is not None else target)
        return "[%s](%s)" % (shown, u) if u else shown
    seg = re.sub(r'<tt>(\\ref\s+\S+\s+)"([^"`]*)"</tt>', r'\1"`\2`"', seg)   # a linked name in code: a link whose text is code
    seg = re.sub(r'(?<!\\)\\ref\s+([\w.:-]+(?:\(\))?)(?:\s+"([^"]*)")?', ref, seg)
    seg = re.sub(r"(?<!\\)\\anchor\s+([\w-]+)", r"[]{#\1}", seg)
    seg = re.sub(r'<span class="ecss-def">(.*?)</span>', r"[**\1**]{.ecss-def}", seg)
    seg = re.sub(r"<b>(.*?)</b>", r"**\1**", seg)
    seg = re.sub(r"<tt>([^`<]*?)</tt>", lambda m: "`%s`" % html.unescape(m.group(1)), seg)

    def a(m):
        href, text = m.group(1), m.group(2)
        if not re.match(r"[a-z]+:", href):
            href = ("html/" if fmt == "html" else "../html/") + href
        return "[%s](%s)" % (text, href)
    return re.sub(r'<a href="([^"]+)"[^>]*>(.*?)</a>', a, seg)


def code_split(line):
    """The line in stretches: (False, text) and (True, `code span`)."""
    parts, pos = [], 0
    for m in re.finditer(r"(`+)(.+?)\1", line):
        parts.append((False, line[pos:m.start()]))
        parts.append((True, m.group(0)))
        pos = m.end()
    parts.append((False, line[pos:]))
    return parts


def figure(block, here, fmt, L, images):
    """<div class="ecss-figure">\\anchor id, the image (or the PlantUML source), the caption div, </div>: a pandoc div."""
    m = re.search(r"\\anchor\s+([\w-]+)", block)
    ident = m.group(1) if m else ""
    img = re.search(r'<img class="ecss-img" src="diagrams/([\w-]+)\.svg" alt="([^"]*)"', block)
    cap = re.search(r'<div class="ecss-caption">(.*)</div>', block)
    body = []
    if img:
        name, alt = img.group(1), html.unescape(img.group(2))
        src, attrs = images(name, fmt)
        body.append("![%s](%s)%s" % (link_text(alt), src, attrs))
    else:   # not rendered: the source as the site shows it
        inner = re.sub(r'^<div class="ecss-figure">\\anchor\s+[\w-]+\n|\n?<div class="ecss-caption">.*</div>\n</div>$', "", block, flags=re.S)
        body.append(inner)
    out = [":::: {#%s .ecss-figure}" % ident if ident else ":::: ecss-figure", ""] + body + [""]
    if cap:
        out += ["::: ecss-caption", line_inline(cap.group(1), here, fmt, L), ":::", ""]
    return "\n".join(out + ["::::"])


def line_inline(line, here, fmt, L):
    return "".join(s if code else inline(s, here, fmt, L) for code, s in code_split(line))


def dialect(text, here, fmt, L, images):
    """The doxygen Markdown of one document as pandoc Markdown for one format."""
    out, fence = [], None
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if fence:
            if line.strip().startswith(fence) or (fence == "\\verbatim" and line.strip() == "\\endverbatim"):
                out.append("```" if fence == "\\verbatim" else line)
                fence = None
            else:
                out.append(line)
            i += 1
            continue
        s = line.strip()
        if s.startswith("```") or s.startswith("~~~"):
            fence = s[:3]
            out.append(line)
        elif s == "\\verbatim":
            fence = "\\verbatim"
            out.append("```")
        elif s.startswith('<div class="ecss-figure">'):
            j = i
            depth = 0
            while j < len(lines):
                depth += lines[j].count("<div") - lines[j].count("</div>")
                if depth <= 0:
                    break
                j += 1
            out.append(figure("\n".join(lines[i:j + 1]), here, fmt, L, images))
            i = j
        elif s.startswith('<div class="ecss-caption') and s.endswith("</div>"):
            m = re.match(r'<div class="([^"]*)">(?:\\anchor\s+([\w-]+))?(.*)</div>$', s)
            classes = " ".join("." + c for c in m.group(1).split())
            out += ["", "::: {%s%s}" % (("#" + m.group(2) + " ") if m.group(2) else "", classes),
                    line_inline(m.group(3), here, fmt, L), ":::", ""]
        elif s.startswith("<details"):
            j = i
            while j < len(lines) and "</details>" not in lines[j]:
                j += 1
            block = "\n".join(lines[i:j + 1])
            summ = re.search(r"<summary>(.*?)</summary>", block)
            body = re.sub(r"<details[^>]*>|<summary>.*?</summary>|</details>", "", block)
            body = re.sub(r"<b>(.*?)</b>\s*<ul>", r"\n\n<b>\1</b>\n\n<ul>", body)
            items = []
            for part in re.split(r"(<li>.*?</li>|<b>.*?</b>)", body, flags=re.S):
                if part.startswith("<li>"):
                    items.append("- " + line_inline(part[4:-5], here, fmt, L))
                elif part.startswith("<b>"):
                    items += ["", line_inline(part, here, fmt, L), ""]
            out += ["", "::: ecss-lists", "**%s**" % line_inline(summ.group(1), here, fmt, L) if summ else ""] + items + [":::", ""]
            i = j
        elif s in ("<ul>", "</ul>"):
            out.append("")
        elif s.startswith("<li>") and s.endswith("</li>"):
            out.append("- " + line_inline(s[4:-5], here, fmt, L))
        else:
            out.append(line_inline(line, here, fmt, L))
        i += 1
    return "\n".join(out)


def visible(row):
    return len(re.sub(r"\]\([^)]*\)|\{[#.][^}]*\}|[\[\]*`]", "", row))


# ---------------------------------------------------------------------------------------------------------------------------------
# The formats
# ---------------------------------------------------------------------------------------------------------------------------------

PRINT_CSS = """@page { size: A4; margin: 18mm 15mm 20mm 15mm; }
html { font: 9.5pt/1.35 "Liberation Sans", "DejaVu Sans", sans-serif; color: #111; }
body { margin: 0; }
h1 { font-size: 17pt; margin: 0 0 8pt; }
h2 { font-size: 13pt; margin: 14pt 0 5pt; break-after: avoid; }
h3 { font-size: 11pt; margin: 11pt 0 4pt; break-after: avoid; }
h4, h5, h6 { font-size: 10pt; margin: 9pt 0 3pt; break-after: avoid; }
p, li { overflow-wrap: anywhere; }
a { color: #0b4f8a; text-decoration: none; }
code, pre { font-family: "Liberation Mono", "DejaVu Sans Mono", monospace; font-size: 8pt; }
pre { white-space: pre-wrap; overflow-wrap: anywhere; background: #f4f4f4; padding: 4pt 6pt; border: 0.5pt solid #ddd; }
table { border-collapse: collapse; width: 100%%; margin: 4pt 0 10pt; font-size: 8pt; line-height: 1.25; }
th, td { border: 0.5pt solid #999; padding: 2pt 3pt; vertical-align: top; text-align: left; overflow-wrap: break-word; }
td code, td a { overflow-wrap: anywhere; }
th { background: #e8e8e8; }
thead { display: table-header-group; }
tr { break-inside: avoid; }
td code, th code { font-size: 7.5pt; }
.ecss-caption { font-size: 8.5pt; color: #222; margin: 4pt 0; break-after: avoid; }
.ecss-figure { break-inside: avoid; text-align: center; margin: 8pt 0; }
.ecss-figure .ecss-caption { text-align: left; }
img { max-width: 100%%; max-height: 225mm; height: auto; }
.ecss-lists { font-size: 8.5pt; border-left: 2pt solid #ccc; padding-left: 6pt; margin: 6pt 0 12pt; }
.ecss-lists ul { margin: 2pt 0; }
"""

SCREEN_CSS = """body { font: 15px/1.45 "Liberation Sans", "DejaVu Sans", sans-serif; max-width: 1100px; margin: 0 auto; padding: 16px;
  color: #111; background: #fff; }
table { border-collapse: collapse; width: 100%; font-size: 13px; }
th, td { border: 1px solid #bbb; padding: 3px 6px; text-align: left; vertical-align: top; overflow-wrap: anywhere; }
th { background: #eee; }
code { font-size: 12.5px; }
a { color: #0b4f8a; }
"""


class Builder:
    def __init__(self, out):
        self.out = out
        self.release = os.environ.get("ECSS_RELEASE", "")
        self.tag = os.environ.get("ECSS_RELEASE_TAG", "")
        self.epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
        self.date = datetime.datetime.fromtimestamp(self.epoch, datetime.timezone.utc)
        self.commit = run(["git", "rev-parse", "HEAD"]).strip()
        self.version = read(os.path.join(ROOT, "VERSION")).strip()
        shutil.rmtree(WORK, ignore_errors=True)
        os.makedirs(WORK)
        self.pngs = {}

    # -- images: SVG for the PDF, PNG (rsvg-convert) for the editable formats, at most the text width
    def images(self, name, fmt):
        svg = os.path.join(OUT, "svg", name + ".svg")
        m = re.search(r'<svg[^>]*\bwidth="([\d.]+)px"', read(svg)[:2000]) if os.path.exists(svg) else None
        wide = bool(m) and float(m.group(1)) > 640
        if fmt in ("pdf", "html"):
            # PlantUML fixes the drawing's size in the root's style: without it the viewBox scales to the width the page gives
            fit = os.path.join(WORK, "svg", name + ".svg")
            if not os.path.exists(fit):
                t = read(svg)
                t = re.sub(r'(<svg\b[^>]*?)\s(?:style|preserveAspectRatio)="[^"]*"', r"\1", t, count=1)
                t = re.sub(r'(<svg\b[^>]*?)\s(?:style|preserveAspectRatio)="[^"]*"', r"\1", t, count=1)
                write(fit, t)
            return fit, ""
        png = os.path.join(WORK, "png", name + ".png")
        if name not in self.pngs:
            os.makedirs(os.path.dirname(png), exist_ok=True)
            run(["rsvg-convert", "--zoom", "2", "--format", "png", "--output", png, svg])
            self.pngs[name] = png
        w = float(m.group(1)) if m else 640
        return png, "{width=%s}" % ("100%" if wide else "%.1fin" % (w / 96.0))

    def pandoc(self, md_text, fmt, target, title):
        src = os.path.join(WORK, "src", os.path.basename(target) + ".md")
        write(src, md_text)
        # the document's own first heading is its title: no title block of pandoc's, the HTML title only
        common = ["pandoc", "--from", FMT, "--columns", "100000", "--metadata", "pagetitle=" + title, "--metadata", "lang=en",
                  "--resource-path", ROOT]
        if fmt == "pdf":
            page = os.path.join(WORK, "html", os.path.basename(target)[:-4] + ".html")
            os.makedirs(os.path.dirname(page), exist_ok=True)
            run(common + ["--to", "html5", "--standalone", "--css", os.path.join(WORK, "print.css"),
                          "--include-in-header", os.path.join(WORK, "meta.html"), "--output", page, src])
            self.pdf(page, target)
        elif fmt == "html":
            run(common + ["--to", "html5", "--standalone", "--embed-resources", "--css", os.path.join(WORK, "screen.css"),
                          "--include-in-header", os.path.join(WORK, "meta.html"), "--output", target, src])
        else:
            run(common + ["--to", fmt, "--output", target, src])

    def pdf(self, page, target):
        from weasyprint import HTML
        doc = HTML(filename=page).render()
        doc.write_pdf(target, pdf_identifier=hashlib.md5(os.path.basename(target).encode() + self.commit.encode()).hexdigest().encode())

    def references(self):
        write(os.path.join(WORK, "print.css"), PRINT_CSS % {})
        write(os.path.join(WORK, "screen.css"), SCREEN_CSS)
        stamp = self.date.strftime("%Y-%m-%dT%H:%M:%SZ")
        write(os.path.join(WORK, "meta.html"), '<meta name="dcterms.created" content="%s">\n<meta name="dcterms.modified" content="%s">\n'
              % (stamp, stamp))

    def build(self):
        texts = {name: read(os.path.join(MD, r)) for r, name in pages()}
        L = Links(texts)
        self.references()
        titles = {}
        for name, text in texts.items():
            m = re.search(r"^#\s+(.*?)\s*(?:\{#[\w-]+\})?\s*$", text, re.M)
            titles[name] = re.sub(r"\\ref\s+\S+\s+\"([^\"]*)\"|\\ref\s+(\S+)", lambda x: x.group(1) or x.group(2), m.group(1)) if m else name
        for fmt in ("pdf", "docx", "odt"):
            d = os.path.join(self.out, fmt)
            os.makedirs(d, exist_ok=True)
            for name, text in texts.items():
                self.pandoc(dialect(text, name, fmt, L, self.images), fmt, os.path.join(d, "%s.%s" % (name, fmt)), titles[name])
        shutil.copytree(os.path.join(OUT, "html"), os.path.join(self.out, "html"))
        title = "uuid7 %s release package %s" % (self.version, self.release)
        for fmt in ("pdf", "docx", "odt"):
            self.pandoc(self.index(texts, titles, L, fmt), fmt, os.path.join(self.out, fmt, "index." + fmt), title)
        self.pandoc(self.index(texts, titles, L, "html"), "html", os.path.join(self.out, "index.html"), title)
        return L

    def index(self, texts, titles, L, fmt):
        """The package index in one format: index.html at the package's top, index.<fmt> beside that format's documents."""
        def to(kind, f):
            return ("%s/%s" % (kind, f)) if fmt == "html" else f if kind == fmt else "../%s/%s" % (kind, f)
        s = json.loads(read(os.path.join(OUT, "summary.json")))
        tc = dict(l.split(": ", 1) for l in read(os.path.join(OUT, "evidence", "toolchain.txt")).splitlines() if ": " in l)
        man = os.path.join(ROOT, "ecss", "baseline", self.release, "MANIFEST")
        fields = dict(l.split(": ", 1) for l in read(man).splitlines() if ": " in l and not l.startswith(("#", "product: ")))
        ver = lambda cmd: (run(cmd).strip().splitlines() or ["?"])[0]
        java = subprocess.run(["java", "-version"], capture_output=True, text=True).stderr.splitlines()[0]
        jar = glob.glob(os.path.join(ROOT, "build", "tools", "plantuml*.jar"))
        rows = [("Product", "uuid7 %s (libuuid7, libuuid7-dev)" % self.version),
                ("Release", self.release),
                ("Tag", ("`%s`" % self.tag) if self.tag else "untagged %s (a rehearsal: the release is the tag `%s` on this commit)" % (self.commit[:12], self.release)),
                ("Commit", "`%s`" % self.commit),
                ("Baseline", "%s, frozen by %s, approved by %s, of commit `%s` (`ecss/baseline/%s/MANIFEST`, sha256 `%s`)" % (
                    self.release, fields.get("frozen-by", "?"), fields.get("approved-by", "?"), fields.get("commit", "?"), self.release,
                    sha256(man))),
                ("Evidence", "the pipeline of commit `%s`, %s" % (s.get("evidence_commit", "?"), md(s.get("environment", "?")))),
                ("Built", "%s (SOURCE_DATE_EPOCH %d, the commit's time)" % (self.date.strftime("%Y-%m-%d %H:%M UTC"), self.epoch)),
                ("Documentation root", "`%s` (ecss/docs_root.sh: kyr-build's build root, snapshot %s)" % (
                    os.environ.get("KYR_BUILD_ROOT_ID", "not a root"), os.environ.get("ECSS_DOCS_SNAPSHOT", "?"))),
                ("Release root", "`%s` (where the evidence ran)" % tc.get("environment", "?").split()[-1]),
                ("Tools", "pandoc %s, WeasyPrint %s, doxygen %s, graphviz %s, %s, PlantUML `%s` sha256 `%s`, rsvg-convert %s" % (
                    ver(["pandoc", "--version"]).split()[-1], weasy_version(), ver(["doxygen", "--version"]),
                    (re.findall(r"version (\S+)", subprocess.run(["dot", "-V"], capture_output=True, text=True).stderr) or ["?"])[0],
                    java.replace('"', ""), os.path.basename(jar[0]) if jar else "none", sha256(jar[0]) if jar else "-",
                    ver(["rsvg-convert", "--version"]).split()[-1]))]
        flags = s.get("config_flags", [])
        L_ = ["# uuid7 %s release package %s {#package}" % (self.version, self.release), "",
              "The ECSS qualification package of uuid7 as released: each document in PDF, DOCX and ODT, the site with the API reference "
              "and the evidence ([html/index.html](%s)), and `SHA256SUMS` over every file. Built by `ecss/release.sh` "
              "from a clean clone of the commit inside the documentation root, from the baseline frozen in the repository; nothing here "
              "is edited by hand." % to("html", "index.html"), "",
              "## Identification", "", "| Item | Value |", "|---|---|"] + ["| %s | %s |" % r for r in rows] + [
              "", "## Claim", "", md(s.get("claim", "")), "",
              "Requirements %d (%d SRS, %d ICD), verified %d; source of truth: %s; configuration flags: %s." % (
                  s["requirements"], s["srs"], s["icd"], s["verified"], s["mode"],
                  "; ".join(md(f) for f in flags) if flags else "none"), "",
              "## Documents", "", "| Document | PDF | DOCX | ODT | Site |", "|---|---|---|---|---|"]
        for name in texts:
            site = L.site_pages.get(name, "index.html")
            L_.append("| %s | [%s.pdf](%s) | [%s.docx](%s) | [%s.odt](%s) | [%s](%s) |" % (
                md(titles[name]), name, to("pdf", name + ".pdf"), name, to("docx", name + ".docx"), name, to("odt", name + ".odt"),
                site, to("html", site)))
        return "\n".join(L_ + ["", "`SHA256SUMS` lists the sha256 of every file of the package but itself.", ""])


def md(t):
    return (t or "").replace("|", "\\|").replace("<", "&lt;").replace(">", "&gt;")


def weasy_version():
    import weasyprint
    return weasyprint.__version__


def sums(out):
    files = sorted(os.path.relpath(os.path.join(d, f), out) for d, _, fs in os.walk(out) for f in fs)
    lines = ["%s  %s" % (sha256(os.path.join(out, f)), f) for f in files if f != "SHA256SUMS"]
    write(os.path.join(out, "SHA256SUMS"), "\n".join(lines) + "\n")
    return len(lines)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out = os.path.abspath(sys.argv[1])
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    b = Builder(out)
    L = b.build()
    n = sums(out)
    print("package.py: %s: %d files; references left bare: %d" % (os.path.relpath(out, ROOT), n, len(L.unresolved)))
    for u in sorted(L.unresolved):
        print("  bare reference: " + u)
    return 1 if L.unresolved else 0


if __name__ == "__main__":
    sys.exit(main())
