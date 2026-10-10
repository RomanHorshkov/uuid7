"""refs.py — the cross-references of the ECSS package, used by ecss.py after the directives are expanded.

structure(): every numbered section gets a label.
link(): every reference in the text becomes a doxygen link: sections (<9.7>, SRS <5.2>), documents and their ids, requirements,
components, verification cases, reviews, RIDs, change requests, open items, baselines, ECSS clauses (the row of the compliance
matrix), tools (the row of the tools register), evidence files. A reference that does not resolve stays bare,
and links_gate.py turns the pipeline RED on it.
"""
import html
import re

SECREF = r"(?:<|&lt;)\d+(?:\.\d+)*(?:>|&gt;)"
SEP = r"(?:,\s+and\s+|,\s+|\s+and\s+|\s+to\s+|\s+or\s+)"
SECLIST = SECREF + r"(?:" + SEP + SECREF + r")*"
STDS = [  # (pattern, key) longest first
    (r"ECSS-E-ST-40C Rev\.1", "e40"), (r"ECSS-E-ST-40C", "e40"), (r"ECSS-E-ST-40", "e40"), (r"E-ST-40", "e40"), (r"E-40", "e40"),
    (r"ECSS-Q-ST-80C Rev\.2", "q80"), (r"ECSS-Q-ST-80C", "q80"), (r"ECSS-Q-ST-80", "q80"), (r"Q-ST-80", "q80"), (r"Q-80", "q80"),
    (r"ECSS-M-ST-40C", "m40"), (r"ECSS-M-ST-40", "m40")]
STD_RE = "|".join(p for p, _ in STDS)
CLAUSE = r"\d+(?:\.\d+)+[a-z]?(?: eo [a-z])?"
PART = r"(?:Annex(?:es)? [A-Z](?:\.\d+)*(?: Table [A-Z]-\d+|\s+" + SECREF + r")?|Table [A-Z]-\d+|Figure [A-Z]-\d+|" + CLAUSE + ")"
PARTLIST = PART + r"(?:" + SEP + PART + r")*"
DEF = re.compile(r"^(#+)\s+(.*?)\s*(?:\{#([\w-]+)\})?\s*$")
FENCE = re.compile(r"^\s*(~~~|```)")
TABLE_SEP = re.compile(r"^\|(?:\s*:?-+:?\s*\|)+\s*$")
PROTECT = re.compile(r"(`[^`\n]*`|\\ref\s+[\w.:-]+(?:\(\))?(?:\s+\"[^\"]*\")?|\\anchor\s+\S+|\\image\s+.*$|"
                     r"<span class=\"ecss-def\">.*?</span>|<a\b[^>]*>.*?</a>|<[A-Za-z/!][^>\n]*>|\{#[\w-]+\}|\[[^\]\n]*\]\([^)\n]*\)|"
                     r"https?://\S+)")


def lab(*parts):
    return re.sub(r"[^A-Za-z0-9_]", "_", "_".join(parts))


def plain(text):
    """Heading or caption text without doxygen commands, labels or markup (for list entries and alt texts)."""
    t = re.sub(r"\\ref\s+\S+\s+\"([^\"]*)\"", r"\1", text)
    t = re.sub(r"\\ref\s+(\S+)", r"\1", t)
    t = re.sub(r"\{#[\w-]+\}|<[^>]+>|`", "", t)
    return t.strip()


class Page:
    def __init__(self, rel, text):
        self.rel = rel
        m = re.search(r"^#\s+(.*?)\s*\{#([\w-]+)\}", text, re.M)
        self.label = m.group(2) if m else "index"
        self.title = plain(m.group(1)) if m else plain((re.search(r"^#\s+(.*)$", text, re.M) or [None, rel])[1])
        self.short = self.label[len("UUID7_DOC_"):] if self.label.startswith("UUID7_DOC_") else self.label.upper()
        self.prefix = self.short.lower()
        self.std = "q80" if rel in ("PAF/SPAP.md", "PAF/SPAMR.md", "PAF/Q80_COMPLIANCE.md") else "e40"   # a bare clause's standard
        self.figures, self.tables = [], []
        self.short_name = self.short
        self.section_std = None


class Index:
    """reg: targets from the model (ecss.py link_registry): ids {id: label}, abbr {abbreviation: label}, reviews {name: label},
    baselines {name: label}, files {name: label}, tools [(name, label)], clauses {std: {clause: label}}, std_pages {std: label},
    std_parts {(std, part): label}, figure_info {diagram: (caption, provenance markdown)}, svg {diagram: bool}, puml {diagram: text}."""

    def __init__(self, reg):
        self.reg = reg
        self.pages = {}
        self.sections = {}       # (page short, number) -> label
        self.unresolved = []     # (page, text) of references that stay bare
        self.count = {"links": 0}
        abbr = sorted(reg["abbr"], key=len, reverse=True)
        self.abbr_re = "|".join(re.escape(a) for a in abbr)
        tools = sorted(reg["tools"], key=lambda t: len(t[0]), reverse=True)
        self.tool_label = dict(tools)
        rev = sorted(reg["reviews"], key=len, reverse=True)
        files = sorted(reg["files"], key=len, reverse=True)
        self.src_re = "|".join(re.escape(x) for x in sorted(reg["sources"], key=len, reverse=True))
        self.sym_re = "|".join(re.escape(x) for x in sorted(reg["symbols"], key=len, reverse=True))
        self.TOKEN = re.compile(
            r"(?P<docsec>\b(?:[A-Z]{2,3}/)?(?P<dabbr>" + self.abbr_re + r")(?:\.md)?(?:'s)?\s+(?P<dsecs>" + SECLIST + r"))"
            r"|(?P<docannex>\b(?P<aabbr>" + self.abbr_re + r")\s+Annex\s+(?P<annex>[A-Z])\b)"
            r"|(?P<sec>" + SECLIST + r")"
            r"|(?P<clause>(?<![\w-])(?P<std>" + STD_RE + r")(?:\s+(?:clauses?\s+)?(?P<parts>" + PARTLIST + r"))?(?![\w-]))"
            r"|(?P<id>\bUUID7-[A-Z]+-[A-Z0-9][A-Za-z0-9_]*(?:-[A-Za-z0-9_]+)*)"
            r"|(?P<oi>\bOI-\d{2}\b)"
            r"|(?P<bl>\bbaseline\s+(?P<blname>" + "|".join(re.escape(b) for b in sorted(reg["baselines"], key=len, reverse=True) or ["@@"]) + r")\b)"
            r"|(?P<abbr>\b(?:[A-Z]{2,3}/)?(?P<abbr1>" + self.abbr_re + r")(?:\.md)?\b)"
            r"|(?P<rev>\b(?:" + "|".join(re.escape(r) for r in rev) + r")\b)"
            r"|(?P<file>\b(?:" + "|".join(re.escape(f) for f in files) + r")\b)"
            r"|(?P<tool>(?<![\w/.-])(?:" + "|".join(re.escape(t) for t, _ in tools) + r")(?![\w/-]|\.\w))"
            r"|(?P<ev>(?<![\w/.-])(?:build/ecss/)?evidence/[\w./-]*[\w/])"
            r"|(?P<src>\"(?:" + self.src_re + r")\"|(?<![\w/.-])(?:" + self.src_re + r")(?![\w/-]|\.\w))"
            r"|(?P<sym>(?<![\w.-])(?:" + self.sym_re + r")\b(?:\(\))?(?!\.(?:puml|svg|h|c)\b))"
            r"|(?P<bare>(?<![\w.;&<>-])\d\.\d+(?:\.\d+)+[a-z]?(?![\w.]|&gt;|>))")

    # -- structure ---------------------------------------------------------------------------------------------------------------

    def add(self, rel, text):
        p = Page(rel, text)
        self.pages[rel] = p
        return p

    def structure(self, rel, text):
        """Section labels; returns the new text."""
        p = self.pages[rel]
        out, fence, heading = [], False, ""
        lines = text.split("\n")
        i = 0
        while i < len(lines):
            line = lines[i]
            if FENCE.match(line):
                fence = not fence
                out.append(line)
                i += 1
                continue
            if fence:
                out.append(line)
                i += 1
                continue
            m = DEF.match(line)
            if m and len(m.group(1)) > 1:
                hashes, title, label = m.groups()
                num = re.match(r"^(\d+(?:\.\d+)*)\s|^Annex\s+([A-Z])\b", title)
                if num:
                    n = num.group(1) or num.group(2)
                    label = label or lab(p.prefix, n)
                    self.sections[(p.short, n)] = label
                heading = plain(re.sub(r"^(\d+(?:\.\d+)*|Annex\s+[A-Z])\s+", "", title))
                out.append("%s %s%s" % (hashes, title, (" {#%s}" % label) if label else ""))
                i += 1
                continue
            out.append(line)
            i += 1
        text = "\n".join(out)
        return text

    # -- links -------------------------------------------------------------------------------------------------------------------

    def sec_label(self, short, num, p, raw):
        """The label of section num of a document; a numbered item below the last numbered heading (<7.1> of a list in <7>)
        falls back to the nearest numbered parent."""
        lb = self.sections.get((short, num))
        while not lb and "." in num:
            num = num.rsplit(".", 1)[0]
            lb = self.sections.get((short, num))
        if not lb:
            self.unresolved.append((p.rel, raw))
        return lb

    def link_sections(self, p, short, secs, first_text=""):
        """'<5.2>, <5.3> and <6>' of document short -> links; first_text prefixes the first link's text."""
        out, pos, first = [], 0, True
        for m in re.finditer(r"(?:<|&lt;)(\d+(?:\.\d+)*)(?:>|&gt;)", secs):
            out.append(secs[pos:m.start()])
            num = m.group(1)
            lb = self.sec_label(short, num, p, (first_text + " " if first_text else "") + m.group(0))
            text = ("%s <%s>" % (first_text, num)) if first else "<%s>" % num
            if lb:
                out.append('\\ref %s "%s"' % (lb, text))
                self.count["links"] += 1
            else:
                out.append(html.escape(text, quote=False))
            first = False
            pos = m.end()
        out.append(secs[pos:])
        return "".join(out)

    def clause_label(self, std, part):
        part = part.strip()
        rows = self.reg["clauses"].get(std, {})
        if re.match(r"^\d", part):
            if part in rows:
                return rows[part]
            base = part.split(" eo ")[0]
            if base in rows:
                return rows[base]
            for c in rows:   # rows are in matrix order: the first row of the clause or of its sub-clauses
                if c.startswith(base) and len(c) > len(base) and (c[len(base)].isalpha() or c[len(base)] in ". "):
                    return rows[c]
            return None
        dm = re.match(r"^Annex ([A-Z])\s+(?:<|&lt;)(\d+(?:\.\d+)*)", part)
        if dm:   # a section of a DRD: the section of the document that follows it
            doc = self.reg["std_parts"].get((std, "Annex " + dm.group(1)))
            short = self.short_of(doc) if doc else None
            return (self.sec_label(short, dm.group(2), self.pages_any(), part) if short else None) or doc
        key = re.sub(r"^Annex(?:es)?\s+", "Annex ", part)
        for k in (key, re.sub(r"\.\d+.*$", "", key), re.sub(r" Table .*$", "", key)):
            if (std, k) in self.reg["std_parts"]:
                return self.reg["std_parts"][(std, k)]
        return self.reg["std_pages"].get(std)

    def link_text(self, p, seg, state):
        def one(m):
            g = m.lastgroup
            s = m.group(0)
            if m.group("docsec"):
                target = self.reg["abbr"][m.group("dabbr")]
                short = self.short_of(target)
                lead = s[:m.start("dsecs") - m.start()].rstrip()
                return self.link_sections(p, short, m.group("dsecs"), lead) if short else s
            if m.group("docannex"):
                short = self.short_of(self.reg["abbr"][m.group("aabbr")])
                lb = self.sec_label(short, m.group("annex"), p, s) if short else None
                return self.ref(lb, s)
            if m.group("sec"):
                return self.link_sections(p, p.short, s)
            if m.group("clause"):
                std = next(k for pat, k in STDS if re.fullmatch(pat, m.group("std")))
                state["std"] = std
                parts = m.group("parts")
                if not parts:
                    return self.ref(self.reg["std_pages"].get(std), s)
                lead = s[:m.start("parts") - m.start()]
                out, pos, first = [], 0, True
                for pm in re.finditer(PART, parts):
                    out.append(parts[pos:pm.start()])
                    t = (lead + pm.group(0)) if first else pm.group(0)
                    out.append(self.ref(self.clause_label(std, pm.group(0)), t, unresolved=(p.rel, t)))
                    first = False
                    pos = pm.end()
                out.append(parts[pos:])
                return "".join(out)
            if m.group("id") or m.group("oi"):
                return self.ref(self.reg["ids"].get(s), s, unresolved=(p.rel, s) if s in self.reg["ids"] or m.group("oi") else None)
            if m.group("bl"):
                return s[:m.start("blname") - m.start()] + self.ref(self.reg["baselines"].get(m.group("blname")), m.group("blname"))
            if m.group("abbr"):
                return self.ref(self.reg["abbr"][m.group("abbr1")], s)
            if m.group("rev"):
                return self.ref(self.reg["reviews"][s], s)
            if m.group("file"):
                return self.ref(self.reg["files"][s], s)
            if m.group("tool"):
                return self.ref(self.tool_label[s], s)
            if m.group("src"):
                if s.startswith('"'):   # "uuid7.h": the quotes inside the link text
                    self.count["links"] += 1
                    return '\\ref %s "\\"%s\\""' % (self.reg["sources"][s.strip('"')], s.strip('"'))
                return self.ref(self.reg["sources"][s], s)
            if m.group("sym"):
                return self.ref(s[:-2] if s.endswith("()") else s, s)
            if m.group("ev"):
                href = re.sub(r"^build/ecss/", "", s)
                self.count["links"] += 1
                return '<a href="%s">%s</a>' % (href, s)
            if m.group("bare"):
                std = state.get("std") or p.std
                lb = self.clause_label(std, s)
                for other in ("e40", "q80"):
                    if not lb or lb == self.reg["std_pages"].get(std):
                        lb, std = self.clause_label(other, s), other
                return self.ref(lb, s) if lb and lb != self.reg["std_pages"].get(std) else s
            return s
        return self.TOKEN.sub(one, seg)

    def ref(self, label, text, unresolved=None):
        if not label:
            if unresolved:
                self.unresolved.append(unresolved)
            return text
        self.count["links"] += 1
        return '\\ref %s "%s"' % (label, text.replace('"', "'"))

    def pages_any(self):
        return next(iter(self.pages.values()))

    def short_of(self, label):
        for p in self.pages.values():
            if p.label == label:
                return p.short
        return None

    def link(self, rel, text):
        p = self.pages[rel]
        out, fence = [], False
        for line in text.split("\n"):
            if FENCE.match(line):
                fence = not fence
                out.append(line)
                continue
            if fence or DEF.match(line) or line.startswith("<!--") or line.startswith("@@"):
                if DEF.match(line):
                    self.heading_std(line, p)
                out.append(line)
                continue
            out.append(self.link_line(p, line))
        return "\n".join(out)

    def heading_std(self, line, p):
        m = re.search(STD_RE, line)
        p.section_std = next(k for pat, k in STDS if re.fullmatch(pat, m.group(0))) if m else None

    def link_line(self, p, line):
        # \ref <document> "Name" <5.2>, <5.3>: one link per section of that document
        def docref(m):
            short = self.short_of(m.group(1))
            if not short:
                return m.group(0)
            return self.link_sections(p, short, m.group(3), m.group(2))
        line = re.sub(r"\\ref\s+(\w+)\s+\"([^\"]*)\"\s+(" + SECLIST + ")", docref, line)
        state = {"std": getattr(p, "section_std", None)}
        out, pos = [], 0
        for m in PROTECT.finditer(line):
            out.append(self.link_text(p, line[pos:m.start()], state))
            out.append(self.protected(p, m.group(0), state))
            pos = m.end()
        out.append(self.link_text(p, line[pos:], state))
        return "".join(out)

    def protected(self, p, s, state):
        if s.startswith("`"):
            inner = s[1:-1]
            if re.fullmatch(r"(?:build/ecss/)?evidence/[\w./-]+", inner):
                self.count["links"] += 1
                return '<a href="%s"><tt>%s</tt></a>' % (re.sub(r"^build/ecss/", "", inner), inner)
            if inner in self.reg["ids"]:
                return self.ref(self.reg["ids"][inner], inner)
            if re.search(r"(?<![\w/.-])(?:%s|%s)\b" % (self.sym_re, self.src_re), inner):
                # code that names an API symbol, a test or a source file: typewriter text with doxygen links
                code = html.escape(inner, quote=False).replace("*", "&lowast;").replace("\\", "&#92;").replace("#", "&#35;")
                code = re.sub(r"(?<![\w/.-])(%s)(?![\w/-]|\.\w)" % self.src_re, lambda x: self.ref(self.reg["sources"][x.group(1)], x.group(1)), code)
                code = re.sub(r"(?<![\w.-])(%s)\b" % self.sym_re, lambda x: self.ref(x.group(1), x.group(1)), code)
                return "<tt>%s</tt>" % code
        return s
