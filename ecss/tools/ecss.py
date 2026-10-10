#!/usr/bin/env python3
"""ecss.py — builds the ECSS qualification package of uuid7 under build/ecss from the rms export (ecss/rms_export/) or, without
one, from the repository's CSVs, plus the repository's own facts: the tags in the sources, the test logs, the coverage, the
pipeline summary, the ELF files and deps.txt.

  ecss.py generate     the model, the diagrams' PlantUML, the Markdown documents (build/ecss/md), the rms loader (build/ecss/rms)
  ecss.py api-doxyfile the doxygen configuration of the header pre-pass (stdout)

Only the standard library; run from anywhere, paths resolve from the repository root.
"""
import collections
import csv
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

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ECSS = os.path.join(ROOT, "ecss")
OUT = os.path.join(ROOT, "build", "ecss")
MD = os.path.join(OUT, "md")
PUML = os.path.join(OUT, "puml")
SVG = os.path.join(OUT, "svg")
EVID = os.path.join(OUT, "evidence")
RMS_EXPORT = os.environ.get("ECSS_RMS_EXPORT") or os.path.join(ECSS, "rms_export")   # another export directory: ECSS_RMS_EXPORT
PROJECT_KEY = "UUID7"

# The rms vocabulary, in the order of kyr_rms's enums (app/include/kyr/rms/domain/model/types.h): the package export carries
# the numbers.
KINDS = ["requirement", "document", "component", "verification-case", "verification-run", "change-request", "decision", "interface",
         "evidence", "hazard", "failure-mode", "mitigation", "fault-tree-event", "fdir-rule", "review", "review-comment", "document-node",
         "assumption", "rationale", "source-reference", "test-procedure", "test-step", "configuration-item", "user", "role"]
STATUSES = ["draft", "candidate", "proposed", "in-review", "needs-rework", "approved", "baselined", "implemented", "verified",
            "validated", "rejected", "obsolete", "superseded", "withdrawn"]
METHODS = ["none", "test", "analysis", "inspection", "review", "demonstration", "simulation"]
CRITICALITIES = ["none", "low", "medium", "high", "safety"]
LINKS = ["derives-from", "refines", "allocates-to", "satisfies", "verifies", "validated-by", "references", "impacts", "duplicates",
         "conflicts-with", "implemented-by", "verified-by", "mitigates", "caused-by", "affects", "depends-on", "traces-to", "supersedes",
         "assumption-for", "justified-by", "constrained-by", "input-to", "output-of", "evidences", "reviews", "approves", "invalidates",
         "has-child", "belongs-to", "covers", "uses-configuration"]
ECSS_METHOD = {"test": "T", "analysis": "A", "inspection": "I", "review": "R", "demonstration": "D", "simulation": "S", "none": "-"}
CSV_COLUMNS = ["human_id", "kind", "title", "body", "rationale", "criteria", "method", "status", "owner", "source_ref", "applicability",
               "notes"]
REVIEWS = ["SRR", "PDR", "CDR", "QR", "AR", "ORR"]
FILES = [("MGT", "Management file"), ("TS", "Technical specification"), ("DDF", "Design definition file"),
         ("DJF", "Design justification file"), ("PAF", "Product assurance file"), ("MF", "Maintenance file"), ("SF", "Security file"),
         ("RB", "Requirements baseline"), ("OP", "Operational documentation")]
SUITES = {"tests/ITs": ("IT", "it"), "tests/plantuml": ("SD", "plantuml")}
PLANTUML_JAR = os.environ.get("PLANTUML_JAR", "/home/rh/plantuml/plantuml-mit-1.2025.10.jar")


def rel(path):
    return os.path.relpath(path, ROOT)


def anchor(ident):
    return re.sub(r"[^A-Za-z0-9_]", "_", ident)


def run(cmd, **kw):
    try:
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)
        return p.returncode, p.stdout + p.stderr
    except OSError as e:
        return 127, str(e)


def read(path, default=""):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError:
        return default


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def md_escape(text):
    """Text from data into Markdown that doxygen reads: no HTML-looking tags, no doxygen commands, no #links, no bold from __x__,
    no table breaks."""
    text = text or ""
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    text = text.replace("\\", "\\\\").replace("@", "\\@")
    text = re.sub(r"#(?=\w)", r"\\#", text)
    text = re.sub(r"\b(\w*__\w*)\b", r"`\1`", text)
    text = text.replace("|", "\\|")
    return text


def notes_of(s):
    out = {}
    for part in (s or "").split(";"):
        if "=" in part:
            k, v = part.split("=", 1)
            out[k.strip()] = v.strip()
    return out


class Art:
    """One rms artifact: what a CSV row or a package-export revision carries."""

    def __init__(self, row, origin):
        self.id = row.get("human_id", "")
        self.kind = row.get("kind", "")
        self.title = row.get("title", "")
        self.body = row.get("body", "")
        self.rationale = row.get("rationale", "")
        self.criteria = row.get("criteria", "")
        self.method = row.get("method", "") or "none"
        self.status = row.get("status", "") or "draft"
        self.owner = row.get("owner", "")
        self.source_ref = row.get("source_ref", "")
        self.applicability = row.get("applicability", "")
        self.notes = notes_of(row.get("notes", ""))
        self.raw_notes = row.get("notes", "")
        self.origin = origin
        self.revision = row.get("revision")
        self.uuid = row.get("uuid")
        self.revs = []
        self.criticality = row.get("criticality", "")

    def row(self):
        return [self.id, self.kind, self.title, self.body, self.rationale, self.criteria, self.method, self.status, self.owner,
                self.source_ref, self.applicability, self.raw_notes]


def load_csv(path, origin):
    if not os.path.exists(path):
        return []
    with open(path, newline="", encoding="utf-8") as f:
        return [Art(r, origin) for r in csv.DictReader(f)]


# ---------------------------------------------------------------------------------------------------------------------------------
# The rms export
# ---------------------------------------------------------------------------------------------------------------------------------

def load_rms():
    """ecss/rms_export/package.json (kyr-rms-cli package export) and, optional, verification.csv (matrix verification)."""
    pkg_path = os.path.join(RMS_EXPORT, "package.json")
    if not os.path.exists(pkg_path):
        return None
    pkg = json.loads(read(pkg_path))
    by_uuid = {}
    arts = []
    rev_status = {}
    for a in pkg.get("artifacts", []):
        if a.get("deleted"):
            continue
        revs = sorted(a.get("revisions", []), key=lambda r: r.get("number", 0))
        if not revs:
            continue
        for x in revs:
            rev_status[x.get("uuid")] = STATUSES[x.get("status", 0)] if x.get("status", 0) < len(STATUSES) else "draft"
        r = revs[-1]
        kind = KINDS[a["kind"]] if a.get("kind", -1) < len(KINDS) else str(a.get("kind"))
        row = {"human_id": a.get("human_id", ""), "kind": kind, "title": r.get("title", ""), "body": r.get("body", ""),
               "rationale": r.get("rationale", ""), "criteria": r.get("criteria", ""),
               "method": METHODS[r.get("method", 0)] if r.get("method", 0) < len(METHODS) else "none",
               "status": STATUSES[r.get("status", 0)] if r.get("status", 0) < len(STATUSES) else "draft", "owner": r.get("owner", ""),
               "source_ref": r.get("source_ref", ""), "applicability": r.get("applicability", ""), "notes": r.get("notes", ""),
               "revision": r.get("number"), "uuid": a.get("uuid"),
               "criticality": CRITICALITIES[r.get("criticality", 0)] if r.get("criticality", 0) < len(CRITICALITIES) else "none"}
        art = Art(row, "rms")
        art.revs = [{"n": x.get("number"), "at_ms": x.get("changed_at_ms", 0), "reason": x.get("reason", ""),
                     "status": STATUSES[x.get("status", 0)] if x.get("status", 0) < len(STATUSES) else "draft",
                     "by": x.get("changed_by_name") or x.get("changed_by") or ""} for x in revs]
        by_uuid[a.get("uuid")] = art
        arts.append(art)
    links = []
    for e in pkg.get("edges", []):
        s, d = by_uuid.get(e.get("src")), by_uuid.get(e.get("dst"))
        if s and d:
            t = LINKS[e["type"]] if e.get("type", -1) < len(LINKS) else str(e.get("type"))
            links.append((s.id, t, d.id, "rms"))
    baselines = []
    for b in sorted(pkg.get("baselines", []), key=lambda b: b.get("created_at_ms", 0)):
        baselines.append({"name": b.get("name", ""), "created_at_ms": b.get("created_at_ms", 0), "description": b.get("description", ""),
                          "by": b.get("created_by_name") or b.get("created_by") or "",
                          "approved_by": b.get("approved_by_name") or b.get("approved_by") or "",
                          "approved_at_ms": b.get("approved_at_ms", 0), "approval_exported": "approved_by" in b,
                          "commit": b.get("commit", "") or ((HEX_RE.search(b.get("description", "")) or [""])[0]),
                          "member_status": collections.Counter(rev_status.get(m.get("revision"), "unknown") for m in b.get("members", [])),
                          "members": len(b.get("members", []))})
    verdicts = {}
    vpath = os.path.join(RMS_EXPORT, "verification.csv")
    if os.path.exists(vpath):
        with open(vpath, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                if r.get("case_id"):
                    verdicts[r["case_id"]] = {"result": r.get("latest_result", ""), "at_ms": r.get("latest_result_at_ms", ""),
                                              "by": r.get("latest_result_by", ""), "suspect": r.get("claim_suspect", "")}
    return {"project": pkg.get("project", {}), "arts": arts, "links": links, "baselines": baselines, "verdicts": verdicts,
            "exported_by": pkg.get("exported_by_name") or pkg.get("exported_by") or "",
            "format": pkg.get("kyr_rms_package", "?"), "producer": pkg.get("kyr_rms_version", ""),
            "path": rel(pkg_path), "sha256": hashlib.sha256(read(pkg_path).encode()).hexdigest()}


# ---------------------------------------------------------------------------------------------------------------------------------
# The repository's sources: doc comments, tags, tests
# ---------------------------------------------------------------------------------------------------------------------------------

HEX_RE = re.compile(r"\b[0-9a-f]{7,40}\b")
DOC_RE = re.compile(r"/\*\*(?!<)(.*?)\*/", re.S)
ID_RE = re.compile(r"UUID7-[A-Z]+-[A-Za-z0-9_-]+")


def tag_ids(block, tag):
    ids = []
    for m in re.finditer(r"@%s\b([^@]*)" % tag, block):
        ids += ID_RE.findall(m.group(1))
    return ids


def brief_of(block):
    text = re.sub(r"^[ \t]*\*[ \t]?", "", block, flags=re.M)
    m = re.search(r"@brief\s+(.*?)(?:\n\s*\n|@\w|$)", text, re.S)
    if not m:
        return ""
    return re.sub(r"\s+", " ", m.group(1)).strip()


def detail_of(block):
    text = re.sub(r"^[ \t]*\*[ \t]?", "", block, flags=re.M)
    text = re.sub(r"@brief\s+.*?(?:\n\s*\n|$)", "", text, count=1, flags=re.S)
    text = re.split(r"\n\s*@(?:verifies|satisfies|ingroup|param|return|author|date|file)\b", text)[0]
    return re.sub(r"\s+", " ", text).strip()


def decl_name(decl):
    d = re.sub(r"_Atomic\s*\([^)]*\)", "_Atomic", decl)
    m = re.match(r"\s*#\s*(?:define|ifndef|ifdef)\s+(\w+)", d)
    if m:
        return m.group(1), "macro"
    if d.lstrip().startswith("typedef"):
        m = re.search(r"\(\s*\*\s*(\w+)\s*\)", d) or re.search(r"(\w+)\s*;", d)
        return (m.group(1) if m else "?"), "typedef"
    m = re.search(r"(\w+)\s*\(", d)
    if m and "=" not in d.split("(")[0]:
        return m.group(1), "function"
    m = re.search(r"(\w+)\s*(?:=|;|\[)", d)
    return (m.group(1) if m else "?"), "variable"


def scan_sources():
    """Symbols of app/ with their component (@ingroup) and @satisfies; test cases of tests/ with @verifies."""
    symbols, tests = {}, []
    for path in sorted(glob.glob(os.path.join(ROOT, "app", "*.[ch]"))):
        text = read(path)
        for m in DOC_RE.finditer(text):
            block = m.group(1)
            line = text.count("\n", 0, m.start()) + 1
            if "@file" in block:
                name, kind = os.path.basename(path), "file"
            else:
                rest = text[m.end():].lstrip("\n")
                decl = rest.split("\n", 1)[0]
                if decl.strip().startswith("/*") or not decl.strip():
                    continue
                name, kind = decl_name(decl)
            groups = re.findall(r"@ingroup\s+(\w+)", block)
            sym = symbols.setdefault(name, {"name": name, "kind": kind, "file": rel(path), "line": line, "group": None,
                                            "satisfies": [], "brief": ""})
            if groups and not sym["group"]:
                sym["group"] = groups[0]
            sym["satisfies"] += [i for i in tag_ids(block, "satisfies") if i not in sym["satisfies"]]
            if not sym["brief"]:
                sym["brief"] = brief_of(block)
    for d, (prefix, exe) in SUITES.items():
        for path in sorted(glob.glob(os.path.join(ROOT, d, "*.c"))):
            text = read(path)
            for m in DOC_RE.finditer(text):
                rest = text[m.end():].lstrip("\n")
                fm = re.match(r"static void (\w+)\(void\*\* state\)", rest)
                if not fm:
                    continue
                name = fm.group(1)
                short = re.sub(r"^(test_|sd_)", "", name)
                tests.append({"id": "UUID7-TC-%s-%s" % (prefix, short), "name": name, "suite": prefix, "exe": exe, "file": rel(path),
                              "line": text.count("\n", 0, m.start()) + 1, "brief": brief_of(m.group(1)),
                              "detail": detail_of(m.group(1)), "verifies": tag_ids(m.group(1), "verifies")})
    for path, cid, title in (("tests/stress/stress.c", "UUID7-TC-ST-stress", "Generation cost benchmark"),
                             ("tests/package/smoke.c", "UUID7-TC-PK-smoke", "Installed-package smoke test")):
        text = read(os.path.join(ROOT, path))
        m = DOC_RE.search(text)
        if m:
            tests.append({"id": cid, "name": os.path.basename(path), "suite": cid.split("-")[2], "exe": None, "file": path, "line": 1,
                          "brief": brief_of(m.group(1)) or title, "detail": detail_of(m.group(1)),
                          "verifies": tag_ids(m.group(1), "verifies")})
    return symbols, tests


# ---------------------------------------------------------------------------------------------------------------------------------
# Results: test logs, pipeline, stress, coverage, evidence checks
# ---------------------------------------------------------------------------------------------------------------------------------

def cmocka_results():
    """{(exe, test): {cell: verdict}} from tests/results/logs/it/**/<exe>.txt."""
    res = {}
    base = os.path.join(ROOT, "tests", "results", "logs", "it")
    for path in sorted(glob.glob(os.path.join(base, "**", "*.txt"), recursive=True)):
        exe = os.path.basename(path)[:-4]
        cell = os.path.relpath(os.path.dirname(path), base)
        cell = "coverage" if cell == "." else cell
        for line in read(path).splitlines():
            m = re.match(r"^\[\s*(OK|FAILED|ERROR|SKIPPED)\s*\] (\w+)$", line)
            if m:
                v = {"OK": "passed", "FAILED": "failed", "ERROR": "failed", "SKIPPED": "skipped"}[m.group(1)]
                cur = res.setdefault((exe, m.group(2)), {})
                if cur.get(cell) != "failed":
                    cur[cell] = v
    return res


def pipeline_summary():
    text = read(os.path.join(EVID, "pipeline.txt"))
    stages, in_summary = {}, False
    for line in text.splitlines():
        if line.startswith("== summary"):
            in_summary = True
            continue
        if in_summary:
            m = re.match(r"^\s+(ok|FAIL|SKIP)\s+(\S+)(.*)$", line)
            if m:
                stages[m.group(2)] = (m.group(1), m.group(3).strip())
            elif line.strip() and not line.startswith(" "):
                in_summary = False
    green = "pipeline GREEN" in text
    return stages, green, bool(text)


def stress_summary():
    rows = []
    path = os.path.join(ROOT, "tests", "results", "stress", "stress_summary.tsv")
    for line in read(path).splitlines():
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) >= 6:
            rows.append({"status": f[0], "profile": f[1], "linkage": f[2], "bench": f[3], "cost": f[4], "rate": f[5]})
    return rows


def coverage_summary():
    d = os.path.join(ROOT, "tests", "results", "ITs", "coverage")
    try:
        js = json.loads(read(os.path.join(d, "coverage-summary.json")))
    except ValueError:
        js = {}
    missing = ""
    for line in read(os.path.join(d, "ITs_coverage.txt")).splitlines():
        if line.startswith("app/uuid7.c"):
            parts = line.split(None, 4)
            missing = parts[4] if len(parts) > 4 else ""
    return js, missing


def tool(cmd):
    rc, out = run(cmd)
    return out if rc == 0 else ""


def evidence_checks(stages):
    """The automated analyses of ecss/DJF/verification_cases.csv (check=...): verdict, detail, evidence file."""
    os.makedirs(EVID, exist_ok=True)
    rel_so = "build/release/libuuid7.so"
    so = os.path.join(ROOT, rel_so)
    a = os.path.join(ROOT, "build/release/libuuid7.a")
    obj = os.path.join(ROOT, "build/release/obj/app__uuid7.c.o")
    checks = {}

    def put(name, verdict, detail, text):
        write(os.path.join(EVID, name + ".txt"), text)
        checks[name] = {"verdict": verdict, "detail": detail, "evidence": "evidence/%s.txt" % name}

    if os.path.exists(so):
        und = tool(["nm", "-D", "--undefined-only", so])
        bad = [s for s in ("malloc", "calloc", "realloc", "free", "aligned_alloc", "posix_memalign")
               if re.search(r"\b%s\b" % s, und)]
        put("no_alloc", "failed" if bad else "passed", ("references " + ", ".join(bad)) if bad else "none referenced",
            "$ nm -D --undefined-only %s\n%s" % (rel_so, und))
        dyn = tool(["readelf", "-d", so])
        needed = re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", dyn)
        put("needed", "passed" if needed == ["libc.so.6"] else "failed", "NEEDED: " + ", ".join(needed), "$ readelf -d %s\n%s" % (rel_so, dyn))
        defined = tool(["nm", "-D", "--defined-only", so])
        hooks = re.findall(r"\buuid7_test_\w+", defined)
        put("no_test_hooks", "failed" if hooks else "passed", ", ".join(hooks) if hooks else "no uuid7_test_ symbol",
            "$ nm -D --defined-only %s\n%s" % (rel_so, defined))
    if os.path.exists(obj):
        syms = tool(["nm", "-S", "--size-sort", obj])
        data = re.findall(r"^[0-9a-f]+ ([0-9a-f]+) ([bBdD]) (\w+)$", syms, re.M)
        names = sorted(n for _, _, n in data)
        sizes = {n: int(s, 16) for s, _, n in data}
        ok = names == ["g_clock_failures", "g_uuid_rng_func", "g_v7_state"] and all(v == 8 for v in sizes.values())
        size = tool(["size", obj])
        put("footprint", "passed" if ok else "failed", ", ".join("%s %d B" % (n, sizes[n]) for n in names),
            "$ nm -S --size-sort %s\n%s\n$ size %s\n%s" % (rel(obj), syms, rel(obj), size))
    if os.path.exists(a):
        g = tool(["nm", "-g", "--defined-only", a])
        names = re.findall(r"^[0-9a-f]+ [A-Z] (\w+)$", g, re.M)
        bad = [n for n in names if not n.startswith("uuid7_")]
        put("prefix", "failed" if bad or not names else "passed", ("unprefixed: " + ", ".join(bad)) if bad else
            "%d global symbols, all uuid7_" % len(names), "$ nm -g --defined-only build/release/libuuid7.a\n" + g)
    logs = sorted(glob.glob(os.path.join(ROOT, "build", "pipeline", "build_*.log")))
    if logs:
        hits = []
        for p in logs:
            hits += ["%s: %s" % (os.path.basename(p), l) for l in read(p).splitlines() if "warning:" in l]
        put("no_warnings", "failed" if hits else "passed", "%d warning(s) in %d build logs" % (len(hits), len(logs)),
            "\n".join(hits) or "no 'warning:' line in: " + ", ".join(os.path.basename(p) for p in logs))
    js, missing = coverage_summary()
    if js:
        lp, bp = js.get("line_percent", 0), js.get("branch_percent", 0)
        put("coverage", "passed" if lp >= 80 and bp >= 65 else "failed", "statement %.1f %%, decision %.1f %%" % (lp, bp),
            json.dumps(js, indent=1) + "\nuncovered lines of app/uuid7.c: " + missing)
    src = read(os.path.join(ROOT, "app", "uuid7.c"))
    incs = re.findall(r'^\s*#\s*include\s+([<"][^>"]+[>"])', src, re.M)
    allowed = {"<errno.h>", "<fcntl.h>", "<limits.h>", "<stdatomic.h>", "<stdint.h>", "<string.h>", "<time.h>", "<unistd.h>",
               "<sys/random.h>", '"uuid7.h"', '"uuid7_test.h"'}
    bad = [i for i in incs if i not in allowed]
    put("includes", "failed" if bad else "passed", ("unexpected: " + " ".join(bad)) if bad else " ".join(incs),
        "#include lines of app/uuid7.c:\n" + "\n".join(incs))
    sa = re.findall(r"_Static_assert\((.*?),", src)
    need = ["CHAR_BIT == 8", "UCHAR_MAX == 255u", "sizeof(uint8_t) == 1u", "sizeof(uint64_t) == 8u"]
    put("static_asserts", "passed" if all(n in sa for n in need) else "failed", "%d static assertions" % len(sa),
        "_Static_assert conditions of app/uuid7.c:\n" + "\n".join(sa))
    warn = read(os.path.join(OUT, "xml_api", "warnings.txt"))
    if os.path.exists(os.path.join(OUT, "xml_api")):
        hdr = [l for l in warn.splitlines() if "uuid7.h:" in l and "uuid7_test.h" not in l]
        put("api_doc", "failed" if hdr else "passed", "%d doxygen warning(s) for app/uuid7.h" % len(hdr), "\n".join(hdr) or "none")
    rep = read(os.path.join(EVID, "reproducible.txt"))
    if rep:
        checks["reproducible"] = {"verdict": "passed" if "identical" in rep.splitlines()[-1] else "failed",
                                  "detail": rep.splitlines()[-1], "evidence": "evidence/reproducible.txt"}
    hard = read(os.path.join(EVID, "hardening.txt"))
    if hard:
        checks["hardening"] = {"verdict": "passed" if "all hard checks passed" in hard else "failed",
                               "detail": (re.findall(r"check_hardening: .*", hard) or ["?"])[-1], "evidence": "evidence/hardening.txt"}
    for st, (v, why) in stages.items():
        checks["stage:" + st] = {"verdict": {"ok": "passed", "FAIL": "failed", "SKIP": "not-run"}[v],
                                 "detail": "pipeline stage %s: %s%s" % (st, v, (" " + why) if why else ""),
                                 "evidence": "evidence/pipeline.txt"}
    stress = stress_summary()
    for r in stress:
        if r["profile"] == "release" and r["linkage"] == "static" and r["bench"] == "stress":
            try:
                cost = float(r["cost"])
            except ValueError:
                cost = float("inf")
            checks["stress_budget"] = {"verdict": "passed" if r["status"] == "PASS" and cost <= 100.0 else "failed",
                                       "detail": "release/static mean %.1f ns per uuid (budget 100 ns)" % cost,
                                       "evidence": "evidence/stress_summary.tsv"}
            shutil.copy(os.path.join(ROOT, "tests", "results", "stress", "stress_summary.tsv"), os.path.join(EVID, "stress_summary.tsv"))
    return checks


def check_verdict(spec, checks):
    """check=<name>[,..] or stage:<a>,<b>: every named check must pass."""
    names = []
    if spec.startswith("stage:"):
        names = ["stage:" + s for s in spec[6:].split(",")]
    elif spec.startswith("stress_budget"):
        names = ["stress_budget"]
    else:
        names = spec.split(",")
    got = [checks.get(n) for n in names]
    if any(g is None for g in got):
        return "not-run", "; ".join("%s: not run" % n for n, g in zip(names, got) if g is None), None
    verdict = "failed" if any(g["verdict"] == "failed" for g in got) else (
        "not-run" if any(g["verdict"] == "not-run" for g in got) else "passed")
    return verdict, "; ".join(g["detail"] for g in got), got[0]["evidence"]


# ---------------------------------------------------------------------------------------------------------------------------------
# The model
# ---------------------------------------------------------------------------------------------------------------------------------

class Model:
    pass


def git(*args):
    rc, out = run(["git"] + list(args))
    return out.strip() if rc == 0 else ""


def read_kv(path):
    out = {}
    for line in read(path).splitlines():
        if ": " in line:
            k, v = line.split(": ", 1)
            out.setdefault(k.strip(), v.strip())
    return out


def buildinfos():
    out = []
    for p in sorted(glob.glob(os.path.join(ROOT, "build", "debs", "*.buildinfo"))):
        kv = read_kv(p)
        tools = dict(re.findall(r"^ (gcc|libc): (.*)$", read(p), re.M))
        out.append({"deb": kv.get("Deb", os.path.basename(p)), "sha256": kv.get("Deb-Sha256", ""), "root": kv.get("Build-Root", "?"),
                    "commit": kv.get("Git-Commit", ""), "gcc": tools.get("gcc", ""), "libc": tools.get("libc", "")})
    return out


# A revision that moves an artifact to one of these statuses records a decision (a review's outcome, an approval, a RID's
# disposition, a verification): the export must say who took it.
DECISIONS = ("approved", "needs-rework", "rejected", "baselined", "implemented", "verified", "validated")


def actor_refusal(M):
    """When the rms is the source of truth, a decision or a baseline without an actor refuses it: the package cannot say who
    reviewed or approved. Returns the refusal message, or ""."""
    if M.mode != "rms":
        return ""
    bl = [b["name"] for b in M.baselines if not b["by"]]
    revs = []
    for a in M.rms["arts"]:
        prev = None
        for r in a.revs:
            if r["status"] in DECISIONS and r["status"] != prev and not r["by"]:
                revs.append("%s revision %s %s" % (a.id, r["n"], r["status"]))
            prev = r["status"]
    res = sorted(c for c, v in M.rms["verdicts"].items() if v.get("result") and not v.get("by"))
    if not (bl or revs or res):
        return ""
    parts = ([("%d baseline(s) (%s)" % (len(bl), ", ".join(bl)))] if bl else []) + \
            ([("%d decision(s) (first: %s)" % (len(revs), revs[0]))] if revs else []) + \
            ([("%d verification result(s) (first: %s)" % (len(res), res[0]))] if res else [])
    return ("the rms export %s names no actor for %s: who reviewed, approved and froze cannot be shown, so it is refused as the "
            "source of truth; refresh it from a kyr_rms that records actors (0.38.0 or later)" % (M.rms["path"], "; ".join(parts)))


def configuration(M):
    """What the evidence is of and where it was produced (evidence/configuration.txt and toolchain.txt, written by make.sh where
    the gates run), the release tag, the packages' buildinfo, the baselines' commits; every disagreement becomes a flag."""
    M.ev = read_kv(os.path.join(EVID, "configuration.txt"))
    M.toolchain = read_kv(os.path.join(EVID, "toolchain.txt"))
    M.ev_commit = M.ev.get("commit", "")
    env = M.toolchain.get("environment", "")
    M.in_root = env.startswith("release root")
    M.env_text = (("the release root, identity %s" % env.split()[-1]) if M.in_root else env) if env else \
        "not recorded (no evidence/toolchain.txt: run ecss/make.sh)"
    M.tag = "v" + M.version
    tag_commit = git("rev-list", "-n", "1", M.tag) if M.tag in git("tag", "--list", M.tag).split() else ""
    M.tag_text = ("%s (commit %s)" % (M.tag, tag_commit[:12])) if tag_commit else "%s: to be created at release" % M.tag
    M.debs = buildinfos()
    flags = []
    if not M.ev_commit:
        flags.append("no evidence configuration is recorded (run ecss/make.sh)")
    else:
        if M.ev_commit != M.commit:
            flags.append("the evidence is of commit %s; these documents are generated at commit %s" % (M.ev_commit[:12], M.short))
        if M.ev.get("state") != "clean":
            flags.append("the evidence ran with uncommitted changes on top of commit %s" % M.ev_commit[:12])
    if env and not M.in_root:
        flags.append("the evidence is a host build, not the release environment")
    if tag_commit and tag_commit != M.ev_commit:
        flags.append("tag %s is at commit %s, not the evidence commit" % (M.tag, tag_commit[:12]))
    for d in M.debs:
        if d["commit"] and M.ev_commit and d["commit"] != M.ev_commit:
            flags.append("%s was built from commit %s, not the evidence commit" % (d["deb"], d["commit"][:12]))
    for b in M.baselines:
        c = b["commit"]
        full = git("rev-parse", "--verify", "--quiet", c + "^{commit}") if c else ""
        if not c:
            b["check"] = "**none recorded**"
            flags.append("rms baseline %s carries no uuid7 commit" % b["name"])
        elif not full:
            b["check"] = "**mismatch**: %s is not a commit of this repository" % c
            flags.append("rms baseline %s names %s, which is not a commit of this repository" % (b["name"], c))
        elif M.ev_commit and run(["git", "merge-base", "--is-ancestor", full, M.ev_commit])[0] != 0:
            b["check"] = "**mismatch**: %s is not in the history of the evidence commit" % full[:12]
            flags.append("rms baseline %s froze commit %s, which is not in the history of the evidence commit" % (b["name"], full[:12]))
        else:
            b["check"] = full[:12] + (" (the evidence commit)" if full == M.ev_commit else "")
        b["full_commit"] = full
        loose = {k: v for k, v in b["member_status"].items() if k in ("draft", "candidate", "proposed", "in-review", "needs-rework", "unknown")}
        if loose:
            flags.append("rms baseline %s froze %d member(s) not approved (%s)" % (b["name"], sum(loose.values()),
                                                                                 ", ".join("%d %s" % (v, k) for k, v in sorted(loose.items()))))
    reviewed = [b for b in M.baselines if any(b["name"].upper().startswith(rv) for rv in REVIEWS)]
    if reviewed and reviewed[-1].get("full_commit") and M.ev_commit and reviewed[-1]["full_commit"] != M.ev_commit:
        flags.append("the newest review baseline, %s, froze commit %s; the evidence is of commit %s" % (
            reviewed[-1]["name"], reviewed[-1]["full_commit"][:12], M.ev_commit[:12]))
    M.config_flags = flags


def h_configuration(M, a):
    tc = M.toolchain
    rows = [["Product", "uuid7 %s (libuuid7, libuuid7-dev)" % M.version],
            ["Evidence of", ("commit %s (%s), branch %s, produced %s" % (M.ev_commit, M.ev.get("state", "?"), M.ev.get("branch", "?"),
                                                                        M.ev.get("at", "?"))) if M.ev_commit else "not recorded"],
            ["Environment", md_escape(M.env_text)],
            ["Toolchain (read where the evidence ran)", ", ".join("%s %s" % (k, md_escape(tc[k])) for k in
                                                                  ("os", "gcc", "glibc", "binutils", "cmocka", "gcovr", "clang-format", "python3")
                                                                  if tc.get(k)) or "not recorded"],
            ["Documents generated at", "commit %s%s" % (M.commit, " (uncommitted changes)" if M.dirty else "")],
            ["Release tag", M.tag_text],
            ["Configuration flags", "; ".join(md_escape(f) for f in M.config_flags) if M.config_flags else "none"]]
    return table(["Item", "Value"], rows)


def h_toolchain(M, a):
    tc = M.toolchain
    if not tc:
        return "The toolchain is not recorded: `ecss/make.sh` writes `evidence/toolchain.txt` where the gates run."
    rows = [[k, md_escape(tc[k])] for k in ("environment", "os", "gcc", "glibc", "binutils", "cmocka", "gcovr", "clang-format", "python3",
                                            "kernel") if tc.get(k)]
    host = [["doxygen", tool(["doxygen", "--version"]).strip() or "-"],
            ["graphviz", (re.findall(r"version (\S+)", tool(["dot", "-V"])) or ["-"])[0]],
            ["python3", sys.version.split()[0]], ["binutils (nm, readelf, size)", (tool(["nm", "--version"]).splitlines() or ["-"])[0].split()[-1]]]
    return ("The gates, the tests, the coverage and the packages ran here (`evidence/toolchain.txt`, read at run time where they "
            "ran):\n\n" + table(["Tool", "Version"], rows) + "\n\nThe documents, the binary analyses and the site are made on the "
            "workstation, outside that environment:\n\n" + table(["Tool", "Version"], host))


def build_model():
    M = Model()
    M.version = read(os.path.join(ROOT, "VERSION")).strip()
    M.commit = git("rev-parse", "HEAD")
    M.short = M.commit[:7]
    M.branch = git("rev-parse", "--abbrev-ref", "HEAD")
    M.dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    M.date = git("log", "-1", "--format=%cs") or datetime.date.today().isoformat()
    M.generated = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    M.symbols, M.tests = scan_sources()
    M.rms = load_rms()
    csv_arts = (load_csv(os.path.join(ECSS, "TS", "requirements.csv"), "csv") + load_csv(os.path.join(ECSS, "documents.csv"), "csv") +
                load_csv(os.path.join(ECSS, "DDF", "components.csv"), "csv") + load_csv(os.path.join(ECSS, "MGT", "reviews.csv"), "csv") +
                load_csv(os.path.join(ECSS, "DJF", "verification_cases.csv"), "csv"))
    M.csv_by_id = {a.id: a for a in csv_arts}
    # Artifacts: the rms when exported, else the CSVs; the tests the sources declare complete either side.
    arts = list(M.rms["arts"]) if M.rms else list(csv_arts)
    M.mode = "rms" if M.rms else "csv"
    by_id = {a.id: a for a in arts}
    for t in M.tests:
        if t["id"] not in by_id:
            row = {"human_id": t["id"], "kind": "verification-case", "title": t["brief"] or t["name"], "body": t["detail"],
                   "criteria": "Every assertion of %s holds." % t["name"], "method": "test", "status": "approved",
                   "owner": "Roman Horshkov", "source_ref": "%s:%d" % (t["file"], t["line"]), "applicability": "D",
                   "notes": "suite=%s; function=%s" % (t["suite"], t["name"])}
            a = Art(row, "code")
            arts.append(a)
            by_id[a.id] = a
        else:
            a = by_id[t["id"]]
            if not a.body:
                a.body = t["detail"]
    M.arts, M.by_id = arts, by_id
    M.reqs = sorted([a for a in arts if a.kind == "requirement"], key=lambda a: a.id)
    M.docs = [a for a in arts if a.kind == "document"]
    M.comps = [a for a in arts if a.kind == "component"]
    M.reviews = [a for a in arts if a.kind == "review"]
    M.cases = [a for a in arts if a.kind == "verification-case"]
    M.crs = [a for a in arts if a.kind == "change-request"]
    # Links: the rms's when exported; the repository's facts always (tags, CSV notes), kept to report drift.
    repo_links = []
    for t in M.tests:
        for r in t["verifies"]:
            repo_links.append((r, "verified-by", t["id"], "code"))
    for a in csv_arts:
        for r in ID_RE.findall(a.notes.get("verifies", "")):
            repo_links.append((r, "verified-by", a.id, "csv"))
    group_comp = {a.notes.get("group"): a.id for a in (M.comps if M.comps else []) if a.notes.get("group")}
    for c in [a for a in csv_arts if a.kind == "component"]:
        group_comp.setdefault(c.notes.get("group"), c.id)
    M.group_comp = group_comp
    for s in M.symbols.values():
        comp = group_comp.get(s["group"])
        for r in s["satisfies"]:
            if comp:
                repo_links.append((comp, "satisfies", r, "code"))
    for a in csv_arts:
        if a.kind == "requirement":
            doc = {"SRS": "UUID7-DOC-SRS", "ICD": "UUID7-DOC-ICD"}.get(a.notes.get("doc"))
            if doc:
                repo_links.append((doc, "has-child", a.id, "csv"))
        if a.kind == "document" and a.applicability != "N":
            for rv in (a.notes.get("reviews", "")).split(","):
                if "UUID7-REV-" + rv.strip() in M.csv_by_id:
                    repo_links.append(("UUID7-REV-" + rv.strip(), "reviews", a.id, "csv"))
    M.repo_links = sorted(set(repo_links))
    M.links = sorted(set(M.rms["links"])) if M.rms else M.repo_links
    key = lambda l: (l[0], l[1], l[2])
    rms_keys = {key(l) for l in M.links}
    M.drift = [l for l in M.repo_links if key(l) not in rms_keys] if M.rms else []
    M.baselines = M.rms["baselines"] if M.rms else []
    M.open_items = load_open_items()
    configuration(M)
    M.refusal = actor_refusal(M)
    # Results
    M.stages, M.green, M.pipeline_ran = pipeline_summary()
    M.checks = evidence_checks(M.stages)
    M.cmocka = cmocka_results()
    M.stress = stress_summary()
    M.coverage, M.uncovered = coverage_summary()
    M.result = {}
    test_by_id = {t["id"]: t for t in M.tests}
    for c in M.cases:
        t = test_by_id.get(c.id)
        n = c.notes
        if t and t["exe"]:
            cells = M.cmocka.get((t["exe"], t["name"]), {})
            if not cells:
                M.result[c.id] = {"verdict": "not-run", "detail": "no log", "cells": cells, "evidence": None}
            else:
                failed = [k for k, v in cells.items() if v == "failed"]
                M.result[c.id] = {"verdict": "failed" if failed else "passed", "cells": cells,
                                  "detail": ("failed in " + ", ".join(failed)) if failed else "passed in %d cells" % len(cells),
                                  "evidence": "evidence/logs"}
        elif n.get("check"):
            v, d, e = check_verdict(n["check"], M.checks)
            if v == "passed" and n.get("verdict") and n["verdict"] != "passed":
                v = "failed"
            M.result[c.id] = {"verdict": v, "detail": d, "evidence": e,
                              "recorded": ("%s, %s" % (n.get("date", ""), n.get("by", ""))) if n.get("verdict") else ""}
        elif n.get("verdict"):
            M.result[c.id] = {"verdict": n["verdict"], "detail": "recorded %s by %s" % (n.get("date", "?"), n.get("by", "?")),
                              "evidence": None}
        else:
            M.result[c.id] = {"verdict": "not-run", "detail": "no result", "evidence": None}
        if M.rms and c.id in M.rms["verdicts"]:
            M.result[c.id]["rms"] = M.rms["verdicts"][c.id]
    # Requirement status
    M.cases_of, M.comps_of, M.reqs_of_comp, M.reqs_of_case = {}, {}, {}, {}
    for s, t, d, _ in M.links:
        if t == "verified-by":
            M.cases_of.setdefault(s, []).append(d)
            M.reqs_of_case.setdefault(d, []).append(s)
        elif t == "verifies":
            M.cases_of.setdefault(d, []).append(s)
            M.reqs_of_case.setdefault(s, []).append(d)
        elif t == "satisfies":
            M.comps_of.setdefault(d, []).append(s)
            M.reqs_of_comp.setdefault(s, []).append(d)
        elif t == "allocates-to":
            M.comps_of.setdefault(s, []).append(d)
            M.reqs_of_comp.setdefault(d, []).append(s)
    M.symbols_of = {}
    for s in M.symbols.values():
        for r in s["satisfies"]:
            M.symbols_of.setdefault(r, []).append(s)
    M.req_status = {}
    for r in M.reqs:
        cs = sorted(set(M.cases_of.get(r.id, [])))
        vs = [M.result.get(c, {"verdict": "not-run"})["verdict"] for c in cs]
        if not cs:
            st = "no case"
        elif "failed" in vs:
            st = "failed"
        elif all(v == "passed" for v in vs):
            st = "verified"
        else:
            st = "not run"
        M.req_status[r.id] = st
    M.diagrams = {}
    return M


# ---------------------------------------------------------------------------------------------------------------------------------
# Diagrams
# ---------------------------------------------------------------------------------------------------------------------------------

def api_doxyfile():
    return "\n".join([
        "PROJECT_NAME = uuid7", "OUTPUT_DIRECTORY = build/ecss/xml_api",
        "INPUT = app/uuid7.h app/uuid7.c app/uuid7_test.h build/ecss/md/components.dox",
        "FILE_PATTERNS = *.h *.c *.dox", "GENERATE_HTML = NO", "GENERATE_LATEX = NO", "GENERATE_XML = YES", "XML_PROGRAMLISTING = NO",
        "EXTRACT_ALL = NO", "EXTRACT_STATIC = YES", "OPTIMIZE_OUTPUT_FOR_C = YES", "WARN_IF_UNDOCUMENTED = YES", "WARN_NO_PARAMDOC = YES",
        "WARN_LOGFILE = build/ecss/xml_api/warnings.txt", "QUIET = YES", "REFERENCES_RELATION = YES", "REFERENCED_BY_RELATION = YES",
        "ENABLE_PREPROCESSING = YES", "MACRO_EXPANSION = YES", "EXPAND_ONLY_PREDEF = YES",
        "PREDEFINED = UUID7_TESTING __linux__ _Atomic(x)=x", "HAVE_DOT = NO",
        'ALIASES = "satisfies=\\xrefitem satisfies \\"Satisfies\\" \\"Implementation trace\\"" '
        '"verifies=\\xrefitem verifies \\"Verifies\\" \\"Verification trace\\""', ""])


def xml_members():
    """Members of the header pre-pass: functions, typedefs, defines with their briefs, params and references."""
    import xml.etree.ElementTree as ET
    out, seen = [], set()
    d = os.path.join(OUT, "xml_api", "xml")
    for f in sorted(glob.glob(os.path.join(d, "*.xml"))):
        try:
            tree = ET.parse(f)
        except ET.ParseError:
            continue
        for cd in tree.iter("compounddef"):
            if cd.get("kind") not in ("file", "group"):
                continue
            for md in cd.iter("memberdef"):
                if md.get("id") in seen:
                    continue
                seen.add(md.get("id"))
                loc = md.find("location")
                fname = os.path.basename(loc.get("declfile") or loc.get("file") or "") if loc is not None else ""
                text = lambda tag: "".join(md.find(tag).itertext()).strip() if md.find(tag) is not None else ""
                out.append({"file": fname, "kind": md.get("kind"), "static": md.get("static") == "yes", "name": text("name"),
                            "type": text("type"), "args": text("argsstring"), "init": text("initializer"),
                            "brief": re.sub(r"\s+", " ", text("briefdescription")),
                            "params": [("".join(p.find("type").itertext()).strip() if p.find("type") is not None else "",
                                        p.findtext("declname") or "") for p in md.findall("param")],
                            "refs": [r.text for r in md.findall("references")]})
    return out


def header_members():
    """Fallback parser of app/uuid7.h when the pre-pass is missing."""
    text = read(os.path.join(ROOT, "app", "uuid7.h"))
    out = []
    for m in re.finditer(r"^#\s*define\s+(UUID7_\w+)\s+(\S+)", text, re.M):
        out.append({"file": "uuid7.h", "kind": "define", "static": False, "name": m.group(1), "type": "", "args": "", "init": m.group(2),
                    "brief": "", "params": [], "refs": []})
    for m in re.finditer(r"^(?:typedef\s+)?(\w[\w\s\*]*?)\s*\(?\*?(\w+)\)?\s*\(([^)]*)\)\s*;", text, re.M):
        kind = "typedef" if m.group(0).startswith("typedef") else "function"
        out.append({"file": "uuid7.h", "kind": kind, "static": False, "name": m.group(2), "type": m.group(1).strip(),
                    "args": "(%s)" % m.group(3), "init": "", "brief": "", "params": [], "refs": []})
    return out


def gen_diagrams(M):
    os.makedirs(PUML, exist_ok=True)
    members = xml_members() or header_members()
    M.members = members
    hdr = [m for m in members if m["file"] == "uuid7.h"]
    # Class diagram: the public header as one class, the callback as an interface.
    L = ["@startuml class_uuid7", "' generated by ecss/tools/ecss.py from app/uuid7.h; do not edit", "title uuid7 public interface (app/uuid7.h)",
         "skinparam shadowing false", "hide empty members", 'class "uuid7.h" as H <<header>> {']
    for m in hdr:
        if m["kind"] == "define":
            L.append("  {static} %s = %s" % (m["name"], m["init"].replace("u", "") if re.match(r"^\d+u$", m["init"]) else m["init"]))
    for m in hdr:
        if m["kind"] == "function":
            L.append("  {static} %s %s%s" % (m["type"], m["name"], m["args"]))
    L.append("}")
    for m in hdr:
        if m["kind"] == "typedef":
            ret = m["type"].replace("(*", "").strip()
            params = m["args"].lstrip(")").strip() or "()"
            L += ['interface "%s" as CB <<callback>> {' % m["name"], "  %s fn%s" % (ret, params), "}"]
            L.append('H ..> CB : uuid7_init(), uuid7_set_rng_func()')
    L += ['class "16-byte UUIDv7" as ID <<value>> {', "  bytes 0..5 : unix_ts_ms (48 bits, big-endian)",
          "  byte 6 : version 7 | rand_a[11:8]", "  byte 7 : rand_a[7:0] (sequence)", "  byte 8 : variant 0b10 | rand_b[61:56]",
          "  bytes 9..15 : rand_b[55:0]", "}", "H ..> ID : uuid7_gen() writes\\nuuid7_init(), uuid7_raise_floor() read", "@enduml", ""]
    write(os.path.join(PUML, "class_uuid7.puml"), "\n".join(L))
    # Package diagram: deps.txt, the tiers, the runtime dependency.
    deps = [l.split() for l in read(os.path.join(ROOT, "deps.txt")).splitlines() if l.strip() and not l.startswith("#")]
    tier = re.search(r"^# tier:\s*(\d+)\s*(\w+)", read(os.path.join(ROOT, "deps.txt")), re.M)
    tiers = {}
    for l in read(os.path.join(ROOT, "build", "sysroot", "share", "kyr-build", "tiers.txt")).splitlines():
        f = l.split()
        if f and f[0].isdigit():
            for n in f[1:]:
                tiers[n] = f[0]
    needed = M.checks.get("needed", {}).get("detail", "NEEDED: libc.so.6").replace("NEEDED: ", "")
    L = ["@startuml package_uuid7", "' generated by ecss/tools/ecss.py from deps.txt, tiers.txt and readelf; do not edit",
         "title uuid7 and its pinned dependencies", "skinparam shadowing false", "left to right direction",
         'package "tier %s %s: uuid7 %s" as U {' % (tier.group(1) if tier else "?", tier.group(2) if tier else "", M.version),
         '  artifact "libuuid7.so.%s\\n(libuuid7)" as SO' % M.version, '  artifact "uuid7.h, libuuid7.a\\n(libuuid7-dev)" as DEV', "}",
         'package "runtime" {', '  artifact "%s" as LIBC' % needed, "}", "SO --> LIBC : DT_NEEDED"]
    own = [d for d in deps if d[0] == "own"]
    apt = [d for d in deps if d[0] == "apt"]
    for d in own:
        L += ['package "tier %s: %s %s" as P_%s {' % (tiers.get(d[1].replace("-", "_"), tiers.get("kyr_build", "?")), d[1], d[2],
                                                      anchor(d[1])),
              '  artifact "%s_%s_all.deb\\nsha256 %s..." as A_%s' % (d[1], d[2], d[4][:12], anchor(d[1])), "}",
              'U ..> P_%s : build-time pin (own, %s)' % (anchor(d[1]), d[3])]
    L.append('package "Ubuntu (apt, build and test)" as APT {')
    for d in apt:
        L.append('  artifact "%s%s" as T_%s' % (d[1], (" " + d[2]) if len(d) > 2 else "", anchor(d[1])))
    L += ["}", "U ..> APT : build-time", "@enduml", ""]
    write(os.path.join(PUML, "package_uuid7.puml"), "\n".join(L))
    # Component diagram: the SDD components, the calls between them (pre-pass references).
    owner = {}
    for name, s in M.symbols.items():
        if s["group"]:
            owner[name] = s["group"]
    deps_c = set()
    for m in members:
        g = owner.get(m["name"])
        for r in m["refs"]:
            h = owner.get(r)
            if g and h and g != h:
                deps_c.add((g, h))
    comps = sorted(M.comps, key=lambda c: c.id)
    L = ["@startuml component_uuid7", "' generated by ecss/tools/ecss.py from components and the code's @ingroup tags; do not edit",
         "title uuid7 components (SDD <5.3>)", "skinparam shadowing false", 'actor "Caller" as C', 'interface "uuid7.h" as API']
    for c in comps:
        L.append('component "%s\\n%s" as %s' % (c.title, c.id, anchor(c.notes.get("group", c.id))))
    L.append("C --> API")
    for m in members:
        if m["file"] == "uuid7.h" and m["kind"] == "function":
            g = owner.get(m["name"])
            if g:
                L.append("API ..> %s : %s()" % (g, m["name"]))
    for g, h in sorted(deps_c):
        L.append("%s --> %s" % (g, h))
    L += ["@enduml", ""]
    write(os.path.join(PUML, "component_uuid7.puml"), "\n".join(L))
    # Hand-written diagrams of the repository and of ecss/.
    for p in sorted(glob.glob(os.path.join(ROOT, "docs", "diagrams", "*.puml")) + glob.glob(os.path.join(ECSS, "**", "*.puml"),
                                                                                             recursive=True)):
        shutil.copy(p, os.path.join(PUML, os.path.basename(p)))
    for p in sorted(glob.glob(os.path.join(PUML, "*.puml"))):
        name = os.path.basename(p)[:-5]
        t = re.search(r"^title (.*)$", read(p), re.M)
        M.diagrams[name] = t.group(1) if t else name


def render(M):
    """SVG with PlantUML when java is installed; the .puml sources either way."""
    java = shutil.which("java")
    M.render_note = ""
    if not java:
        M.render_note = "java is not installed: the diagrams stay PlantUML sources (sudo apt install default-jre-headless renders them)."
        return False
    if not os.path.exists(PLANTUML_JAR):
        M.render_note = "PlantUML jar %s not found: the diagrams stay PlantUML sources." % PLANTUML_JAR
        return False
    os.makedirs(SVG, exist_ok=True)
    rc, out = run([java, "-Djava.awt.headless=true", "-jar", PLANTUML_JAR, "-tsvg", "-o", SVG] + sorted(glob.glob(os.path.join(PUML, "*.puml"))))
    write(os.path.join(EVID, "plantuml.txt"), out)
    if rc != 0:
        M.render_note = "PlantUML failed (evidence/plantuml.txt): the diagrams without an SVG stay PlantUML sources."
    return rc == 0


# ---------------------------------------------------------------------------------------------------------------------------------
# Markdown
# ---------------------------------------------------------------------------------------------------------------------------------

def rref(i):
    return '\\ref %s "%s"' % (anchor("req_" + i), i)


def cref(i, M=None):
    return '\\ref %s "%s"' % (anchor("tc_" + i), i)


def dref(i, M):
    d = M.by_id.get(i)
    if not d or d.applicability == "N":
        return md_escape(d.title if d else i)
    path = d.notes.get("path", "")
    page, _, sec = path.partition("#")
    label = os.path.basename(page)[:-3] if page.endswith(".md") else i
    target = sec or page_label(page)
    return '\\ref %s "%s"' % (target, md_escape(d.title))


def page_label(path):
    base = os.path.basename(path)[:-3] if path.endswith(".md") else path
    return "UUID7_DOC_" + anchor(base.upper())


def verdict_word(v):
    return {"passed": "passed", "failed": "**FAILED**", "not-run": "not run", "skipped": "skipped"}.get(v, v)


def status_word(s):
    return {"verified": "verified", "failed": "**FAILED**", "no case": "**no verification case**", "not run": "not run"}.get(s, s)


def table(head, rows):
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c).replace("\n", " ") for c in r) + " |")
    return "\n".join(out)


def diagram_md(M, name, caption=None):
    caption = caption or M.diagrams.get(name, name)
    svg = os.path.join(SVG, name + ".svg")
    src = read(os.path.join(PUML, name + ".puml"))
    if not src:
        return "> Diagram %s: no PlantUML source (the producing step did not run)." % name
    link = '<a href="diagrams/%s.puml">%s.puml</a>' % (name, name)
    if os.path.exists(svg):
        return '\\image html %s.svg "%s"\n\nSource: %s' % (name, md_escape(caption), link)
    return ("**%s** (%s). Rendering waits for java; the PlantUML source:\n\n~~~{.txt}\n%s~~~" % (md_escape(caption), link, src))


def product_text(M):
    ev = ("evidence of commit %s (%s) in %s" % (M.ev_commit, M.ev.get("state", "?"), md_escape(M.env_text))) if M.ev_commit else \
        "no evidence recorded"
    docs = "" if M.ev_commit == M.commit and not M.dirty else "; documents generated at commit %s%s" % (
        M.short, " (uncommitted changes)" if M.dirty else "")
    return "uuid7 %s; %s%s; release tag %s%s" % (M.version, ev, docs, M.tag_text,
                                                 ("; **%d configuration flag(s)**, \\ref UUID7_DOC_SCF \"SCF\"" % len(M.config_flags))
                                                 if M.config_flags else "")


def status_text(M, d, rev=""):
    """An rms status with who set it, and, when the reviews were a scripted demonstration, that the status belongs to it."""
    if not d.revs or d.status not in ("approved", "baselined", "implemented", "verified", "validated", "rejected"):
        return d.status + rev
    r = next((x for x in reversed(d.revs) if x["status"] == d.status), d.revs[-1])
    demo = M.mode == "rms" and any(review_facts(M, rv)["scripted"] for rv in REVIEWS)
    return "%s%s by %s%s" % (d.status, rev, who(r["by"]), " in the scripted rms demonstration (\\ref UUID7_DOC_JRR \"JRR\")" if demo else "")


def h_docinfo(M, a):
    d = M.by_id.get(a.get("id"))
    if not d:
        return "> Document %s is not in the register." % a.get("id")
    src = "rms export (%s, project %s)" % (M.rms["path"], PROJECT_KEY) if M.mode == "rms" else "repository CSVs (no rms export present)"
    rev = (" (rms revision %s)" % d.revision) if d.revision else ""
    return table(["Item", "Value"], [
        ["Document", "%s — %s" % (d.id, md_escape(d.title))], ["ECSS file", d.notes.get("file", "")],
        ["DRD", md_escape(d.notes.get("drd", "-"))], ["Called from", md_escape(d.rationale)],
        ["Reviews (ECSS-E-ST-40 Table A-1)", d.notes.get("reviews", "").replace(",", ", ")],
        ["Status", status_text(M, d, rev)], ["Product", product_text(M)],
        ["Generated", "%s by ecss/make.sh from %s" % (M.generated, src)],
        ["Criticality", "software category D (ECSS-Q-ST-80 Table D-1), see \\ref UUID7_DOC_TAILORING \"the tailoring\"; the rms "
                        "criticality field is another scale, \\ref tailoring_scales \"mapped there\""],
        ["Claim", "%s (basis: \\ref index \"the front page\")" % md_escape(claim(M)[0])]])


REFS = [
    ("AD1", "ECSS-E-ST-40C Rev.1", "Space engineering — Software, 30 April 2025"),
    ("AD2", "ECSS-Q-ST-80C Rev.2", "Space product assurance — Software product assurance, 30 April 2025"),
    ("AD3", "ECSS-M-ST-40C", "Space project management — Configuration and information management (SCMP and SCF DRDs; not held here)"),
    ("RD1", "RFC 9562", "Universally Unique IDentifiers (UUIDs), May 2024"),
    ("RD2", "NAMING.md", "The platform's naming rule (one namespace, prefixes, versions)"),
    ("RD3", "DEPENDENCIES.md", "The platform's dependency rule (deps.txt classes apt, vendor, own)"),
    ("RD4", "kyr-build 2.1.0", "The pinned build tooling: profile catalog, hardening gate, ABI gate, pipeline, packaging (deps.txt)"),
    ("RD5", "kyr_rms docs/CLI.md", "kyr-rms-cli, the requirements management system holding requirements, links, results, reviews and baselines"),
    ("RD6", "ISO/IEC 9899:2011", "Programming languages — C (C11)"),
]


def h_refs(M, a):
    rows = [[i, md_escape(t), md_escape(d)] for i, t, d in REFS]
    return ("Applicable documents (AD) and reference documents (RD). The ECSS standards are cited, never copied.\n\n" +
            table(["Id", "Document", "Title"], rows))


TERMS = [
    ("CAS", "compare-and-swap, the atomic operation that reserves a (ms, seq) pair"),
    ("floor", "a persisted identifier imported so that later identifiers sort after it"),
    ("logical clock", "the millisecond of the reservation state; it may run ahead of the wall clock by UUID7_MAX_AHEAD_MS"),
    ("rand_a / sequence", "the 12-bit field after the version nibble; here a counter within one logical millisecond (RFC 9562 6.2 method 1)"),
    ("rand_b / tail", "the 62 random bits after the variant"),
    ("reservation state", "the single atomic 64-bit word holding the last reserved (ms << 12 | seq)"),
    ("rms", "the requirements management system (kyr-rms-cli): artifacts, links, results, reviews, baselines"),
    ("cell", "one build profile x linkage combination in which a suite runs"),
    ("pipeline", "utils/run_pipeline.sh: every gate of the repository"),
    ("DRD / DRL", "document requirements definition / list (ECSS-E-ST-40 Annex A)"),
    ("RB, TS, DDF, DJF, MGT, MF, OP, SF, PAF", "the ECSS files (ECSS-E-ST-40 Figure A-1)"),
]


def h_terms(M, a):
    return ("The terms of ECSS-S-ST-00-01 and of the applicable documents apply; in addition:\n\n" +
            table(["Term", "Meaning"], [[md_escape(t), md_escape(d)] for t, d in TERMS]))


def req_block(M, r, level):
    syms = M.symbols_of.get(r.id, [])
    comps = sorted(set(M.comps_of.get(r.id, [])))
    cs = sorted(set(M.cases_of.get(r.id, [])))
    sat = ", ".join(("%s()" % s["name"]) if s["kind"] == "function" else ("`%s`" % s["name"]) for s in syms) or "-"
    comp_txt = ", ".join('\\ref %s "%s"' % (anchor("cmp_" + c), md_escape(M.by_id[c].title if c in M.by_id else c)) for c in comps) or "-"
    ver = ", ".join("%s (%s)" % (cref(c), verdict_word(M.result.get(c, {}).get("verdict", "not-run"))) for c in cs) or "**none**"
    lines = ["%s %s %s {#%s}" % ("#" * level, r.id, md_escape(r.title), anchor("req_" + r.id)), "", md_escape(r.body), "",
             "- Rationale: " + md_escape(r.rationale),
             "- Verification: %s (%s) — %s" % (ECSS_METHOD.get(r.method, r.method), r.method, md_escape(r.criteria)),
             "- Source: %s · ECSS category: %s · rms criticality: %s (\\ref tailoring_scales \"not an ECSS category\") · Status: %s" % (
                 md_escape(r.source_ref), md_escape(r.applicability), r.criticality or "not set",
                                                                status_text(M, r, (" (rms revision %s)" % r.revision) if r.revision else "")),
             "- Implemented by: %s; components: %s" % (sat, comp_txt), "- Verified by: %s → %s" % (ver, status_word(M.req_status[r.id])), ""]
    return "\n".join(lines)


def h_reqs(M, a):
    doc, sec = a.get("doc"), a.get("section")
    rs = [r for r in M.reqs if r.notes.get("doc") == doc and (not sec or r.notes.get("section") == sec)]
    if not rs:
        return "No requirement in this section."
    out, group = [], None
    level = int(a.get("level", "4"))
    for r in rs:
        g = r.notes.get("group", "")
        if g != group and a.get("groups", "yes") == "yes":
            out.append("**%s**\n" % md_escape(g))
            group = g
        out.append(req_block(M, r, level))
    return "\n".join(out)


def h_validation_matrix(M, a):
    doc = a.get("doc")
    rows = []
    for r in M.reqs:
        if doc and r.notes.get("doc") != doc:
            continue
        cs = sorted(set(M.cases_of.get(r.id, [])))
        rows.append([rref(r.id), md_escape(r.title), ECSS_METHOD.get(r.method, r.method),
                     ", ".join(cref(c) for c in cs) or "**none**", status_word(M.req_status[r.id])])
    return table(["Requirement", "Title", "Method", "Validation (cases)", "Status"], rows)


def h_upward_trace(M, a):
    doc = a.get("doc")
    rs = [r for r in M.reqs if r.notes.get("doc") == doc]
    fwd = {}
    for r in rs:
        for src in [s.strip() for s in re.split(r";", r.source_ref) if s.strip()]:
            fwd.setdefault(src, []).append(r.id)
    f_rows = [[md_escape(s), ", ".join(rref(i) for i in ids)] for s, ids in sorted(fwd.items())]
    b_rows = [[rref(r.id), md_escape(r.source_ref)] for r in rs]
    return ("Forward: upper-level source to requirement.\n\n" + table(["Upper-level source", "Requirements"], f_rows) +
            "\n\nBackward: requirement to upper-level source.\n\n" + table(["Requirement", "Upper-level source"], b_rows))


def h_reqtable(M, a):
    rows = [[rref(r.id), md_escape(r.title), r.notes.get("doc", ""), r.notes.get("section", ""), ECSS_METHOD.get(r.method, r.method),
             r.status, status_word(M.req_status[r.id])] for r in M.reqs]
    return table(["Requirement", "Title", "Doc", "Section", "Method", "Status", "Verification"], rows)


def h_components(M, a):
    out = []
    members = {m["name"]: m for m in getattr(M, "members", [])}
    for c in sorted(M.comps, key=lambda c: c.id):
        g = c.notes.get("group", "")
        syms = sorted([s for s in M.symbols.values() if s["group"] == g], key=lambda s: (s["kind"], s["name"]))
        funcs = [s for s in syms if s["kind"] == "function"]
        data = [s for s in syms if s["kind"] in ("variable", "macro", "typedef")]
        reqs = sorted(set(M.reqs_of_comp.get(c.id, [])))
        deps = set()
        for s in funcs:
            for r in members.get(s["name"], {}).get("refs", []):
                o = M.symbols.get(r, {}).get("group")
                if o and o != g:
                    deps.add(o)
        dep_txt = ", ".join('\\ref %s "%s"' % (anchor("cmp_" + M.group_comp.get(d, d)), md_escape(M.by_id.get(M.group_comp.get(d, ""),
                                                                                                          Art({}, "")).title or d))
                            for d in sorted(deps)) or "none"

        def sig(s):
            m = members.get(s["name"])
            return "`%s %s%s`" % (m["type"], s["name"], m["args"]) if m and m["kind"] == "function" else "%s()" % s["name"]
        out += ["#### %s %s {#%s}" % (c.id, md_escape(c.title), anchor("cmp_" + c.id)), "",
                "- Identifier: %s (doxygen group \\ref %s)" % (c.id, g),
                "- Type: %s; development: %s" % (md_escape(c.notes.get("type", "")), md_escape(c.notes.get("development", ""))),
                "- Purpose: %s Requirements: %s" % (md_escape(c.body), ", ".join(rref(r) for r in reqs) or "-"),
                "- Function: %s" % md_escape(c.rationale),
                "- Subordinates: %s" % (", ".join("%s()" % s["name"] for s in funcs) or "none"),
                "- Dependencies: %s" % dep_txt,
                "- Interfaces: %s" % ("; ".join(sig(s) for s in funcs) or "data only"),
                "- Resources: static storage only (see `Data`); no allocation, no file descriptor except the built-in RNG's transient one",
                "- References: %s" % md_escape(c.source_ref),
                "- Data: %s" % (", ".join("`%s`" % s["name"] for s in data) or "none"), ""]
    return "\n".join(out)


def h_allocation(M, a):
    rows = []
    for r in M.reqs:
        comps = sorted(set(M.comps_of.get(r.id, [])))
        syms = M.symbols_of.get(r.id, [])
        rows.append([rref(r.id), ", ".join(md_escape(M.by_id[c].title) if c in M.by_id else c for c in comps) or "-",
                     ", ".join("`%s`" % s["name"] for s in syms) or "-"])
    back = []
    for c in sorted(M.comps, key=lambda c: c.id):
        back.append([md_escape(c.title), ", ".join(rref(r) for r in sorted(set(M.reqs_of_comp.get(c.id, [])))) or "-"])
    return ("Forward: requirement to component and code element (from the `@satisfies` tags of the code).\n\n" +
            table(["Requirement", "Components", "Code elements"], rows) + "\n\nBackward: component to requirements.\n\n" +
            table(["Component", "Requirements"], back))


def h_diagram(M, a):
    return diagram_md(M, a["name"], a.get("caption"))


def h_scenarios(M, a):
    out = []
    for t in [t for t in M.tests if t["suite"] == "SD"]:
        out += ["#### %s" % md_escape(t["brief"].rstrip(".")), "",
                "Test %s (%s), verifies %s; result: %s." % (cref(t["id"]), "%s()" % t["name"], ", ".join(rref(r) for r in t["verifies"]),
                                                             verdict_word(M.result.get(t["id"], {}).get("verdict", "not-run"))), "",
                diagram_md(M, t["name"], t["brief"]), ""]
    return "\n".join(out)


def h_testcases(M, a):
    suites = a.get("suites", "IT,SD,PL,PK,ST").split(",")
    out = []
    test_by_id = {t["id"]: t for t in M.tests}
    for c in sorted(M.cases, key=lambda c: c.id):
        t = test_by_id.get(c.id)
        suite = t["suite"] if t else c.id.split("-")[2]
        if c.method != "test" or suite not in suites:
            continue
        reqs = sorted(set(M.reqs_of_case.get(c.id, [])))
        res = M.result.get(c.id, {})
        if t:
            items = "`%s` (%s:%d)" % (t["name"], t["file"], t["line"])
            env = ("the suite executable `%s`, linked against libuuid7 built with -DUUID7_TESTING, in every cell %s" %
                   (t["exe"], "(6 profiles x static/shared, then the coverage build)")) if t["exe"] else md_escape(c.body)
            inputs = ("the clock, RNG callback and imported identifiers the test sets up (deterministic through the test seams, unless the "
                      "case is about the real clock or the built-in RNG); the exact values are in %s:%d" % (t["file"], t["line"])
                      if t["exe"] else md_escape(c.body))
            outputs = "return codes and decoded identifier fields asserted by the test" if t["exe"] else md_escape(c.criteria)
        else:
            items, env, inputs, outputs = md_escape(c.source_ref), "the pipeline host (SValP <7>)", md_escape(c.body), md_escape(c.criteria)
        out += ["#### %s {#%s}" % (c.id + ("" if t and t["exe"] else " " + md_escape(c.title)), anchor("tc_" + c.id)), "",
                "- Purpose: %s" % md_escape(c.title if not t else (t["brief"] + (" " + t["detail"] if t["detail"] else ""))),
                "- Test items: %s; requirements: %s" % (items, ", ".join(rref(r) for r in reqs) or "-"),
                "- Inputs: %s" % inputs, "- Outputs: %s" % outputs,
                "- Pass-fail: %s" % (md_escape(c.criteria) if not t or not t["exe"] else
                                     "every assertion holds and the executable exits 0 in every cell"),
                "- Environment: %s" % env,
                "- Constraints: %s" % ("none (deterministic; the real clock or entropy only where the case says so)" if t and t["exe"]
                                       else "runs after the stages it reads"),
                "- Dependencies: none (each case resets the generator first)" if t and t["exe"] else "- Dependencies: the pipeline stages named above",
                "- Result: %s — %s" % (verdict_word(res.get("verdict", "not-run")), md_escape(res.get("detail", ""))), ""]
    return "\n".join(out)


def h_noncases(M, a):
    out = []
    for c in sorted(M.cases, key=lambda c: c.id):
        if c.method == "test":
            continue
        reqs = sorted(set(M.reqs_of_case.get(c.id, [])))
        res = M.result.get(c.id, {})
        ev = res.get("evidence")
        out += ["%s %s %s {#%s}" % ("#" * int(a.get("level", "3")), c.id, md_escape(c.title), anchor("tc_" + c.id)), "",
                "- Method: %s (%s)%s" % (ECSS_METHOD.get(c.method, c.method), c.method,
                                         " — automated by ecss/make.sh" if c.notes.get("check") else " — recorded"),
                "- Requirements: %s" % (", ".join(rref(r) for r in reqs) or "-"),
                "- Procedure: %s" % md_escape(c.body), "- Item: %s" % md_escape(c.source_ref),
                "- Criteria: %s" % md_escape(c.criteria),
                "- Result: %s — %s%s" % (verdict_word(res.get("verdict", "not-run")), md_escape(res.get("detail", "")),
                                         (' (<a href="%s">evidence</a>)' % ev) if ev else ""), ""]
    return "\n".join(out)


def h_case_matrix(M, a):
    rows = []
    for c in sorted(M.cases, key=lambda c: c.id):
        r = M.result.get(c.id, {})
        row = [cref(c.id), md_escape(c.title), ECSS_METHOD.get(c.method, c.method),
               ", ".join(rref(x) for x in sorted(set(M.reqs_of_case.get(c.id, [])))) or "-", verdict_word(r.get("verdict", "not-run"))]
        if M.rms:
            v = r.get("rms")
            row.append(("%s (%s)" % (v["result"] or "none", datetime.datetime.fromtimestamp(int(v["at_ms"]) / 1000).strftime("%Y-%m-%d"))
                        if v.get("at_ms") else v["result"] or "none") if v else "not recorded")
        rows.append(row)
    return table(["Case", "Title", "Method", "Requirements", "Result (this run)"] + (["Latest rms result"] if M.rms else []), rows)


def h_results_cells(M, a):
    cells = sorted({cell for v in M.cmocka.values() for cell in v})
    out = []
    for exe, label in (("it", "Integration suite (tests/ITs)"), ("plantuml", "Scenario suite (tests/plantuml)")):
        tests = sorted({t for (e, t) in M.cmocka if e == exe})
        if not tests:
            out.append("%s: no log (the suite did not run)." % label)
            continue
        rows = []
        for cell in cells:
            vs = [M.cmocka[(exe, t)].get(cell) for t in tests]
            n_ok = sum(1 for v in vs if v == "passed")
            n_bad = sum(1 for v in vs if v == "failed")
            rows.append([cell, len([v for v in vs if v]), n_ok, n_bad,
                         '<a href="evidence/logs/%s/%s.txt">log</a>' % (cell if cell != "coverage" else ".", exe)])
        out += ["**%s**: %d test cases.\n" % (label, len(tests)), table(["Cell (profile/linkage)", "Run", "Passed", "Failed", "Log"], rows), ""]
    return "\n".join(out)


def h_results_tests(M, a):
    rows = []
    for t in M.tests:
        if not t["exe"]:
            continue
        r = M.result.get(t["id"], {})
        rows.append([cref(t["id"]), "%s()" % t["name"], ", ".join(rref(x) for x in t["verifies"]), verdict_word(r.get("verdict", "not-run")),
                     md_escape(r.get("detail", ""))])
    return table(["Case", "Function", "Verifies", "Result", "Detail"], rows)


def h_req_results(M, a):
    rows = []
    for r in M.reqs:
        cs = sorted(set(M.cases_of.get(r.id, [])))
        rows.append([rref(r.id), ECSS_METHOD.get(r.method, r.method),
                     ", ".join("%s %s" % (cref(c), verdict_word(M.result.get(c, {}).get("verdict", "not-run"))) for c in cs) or "-",
                     status_word(M.req_status[r.id])])
    return table(["Requirement", "Method", "Cases and results", "Status"], rows)


def h_trace_matrix(M, a):
    rows = []
    for r in M.reqs:
        comps = sorted(set(M.comps_of.get(r.id, [])))
        syms = M.symbols_of.get(r.id, [])
        cs = sorted(set(M.cases_of.get(r.id, [])))
        rows.append([rref(r.id), ", ".join(md_escape(M.by_id[c].title) if c in M.by_id else c for c in comps) or "-",
                     ", ".join("`%s`" % s["name"] for s in syms) or "-", ", ".join(cref(c) for c in cs) or "**none**",
                     ", ".join(verdict_word(M.result.get(c, {}).get("verdict", "not-run")) for c in cs) or "-",
                     status_word(M.req_status[r.id])])
    return table(["Requirement", "Design element", "Code", "Verification", "Results", "Status"], rows)


def h_gaps(M, a):
    gaps = [r for r in M.reqs if M.req_status[r.id] != "verified"]
    unref = [c for c in M.cases if not M.reqs_of_case.get(c.id)]
    out = []
    if not gaps:
        out.append("Every requirement is verified: each has at least one case and every case passed.")
    else:
        out.append(table(["Requirement", "Status"], [[rref(r.id), status_word(M.req_status[r.id])] for r in gaps]))
    if unref:
        out.append("\nCases that verify no requirement: " + ", ".join(cref(c.id) for c in unref) + ".")
    if M.drift:
        out.append("\nRepository links absent from the rms export (re-run the loader): " +
                   "; ".join("%s %s %s" % l[:3] for l in M.drift[:40]) + ("…" if len(M.drift) > 40 else ""))
    return "\n".join(out)


def h_summary(M, a):
    n = len(M.reqs)
    by = {}
    for r in M.reqs:
        by[M.req_status[r.id]] = by.get(M.req_status[r.id], 0) + 1
    v = {}
    for c in M.cases:
        x = M.result.get(c.id, {}).get("verdict", "not-run")
        v[x] = v.get(x, 0) + 1
    return table(["Measure", "Value"], [
        ["Requirements", "%d (%d SRS, %d ICD)" % (n, len([r for r in M.reqs if r.notes.get("doc") == "SRS"]),
                                                   len([r for r in M.reqs if r.notes.get("doc") == "ICD"]))],
        ["Verified", by.get("verified", 0)], ["Failed", by.get("failed", 0)], ["Not run", by.get("not run", 0)],
        ["Without a case", by.get("no case", 0)],
        ["Verification cases", "%d (%s)" % (len(M.cases), ", ".join("%d %s" % (k2, k) for k, k2 in sorted(v.items())))],
        ["Coverage of app/uuid7.c", "statement %.1f %%, decision %.1f %%" % (M.coverage.get("line_percent", 0),
                                                                           M.coverage.get("branch_percent", 0)) if M.coverage else "not run"],
        ["Pipeline", ("GREEN" if M.green else "**not green**") if M.pipeline_ran else "not run in this refresh"]])


def h_pipeline(M, a):
    if not M.stages:
        return "The pipeline did not run in this refresh (ecss/make.sh --docs-only)."
    rows = [[s, {"ok": "ok", "FAIL": "**FAIL**", "SKIP": "skipped"}[v], md_escape(why)] for s, (v, why) in M.stages.items()]
    return table(["Stage", "Result", "Note"], rows) + '\n\nLog: <a href="evidence/pipeline.txt">pipeline.txt</a>.'


def h_install(M, a):
    rows = [[st, {"ok": "ok", "FAIL": "**FAIL**", "SKIP": "skipped"}[M.stages[st][0]] if st in M.stages else "not run"]
            for st in ("package", "package-smoke")]
    log = read(os.path.join(ROOT, "build", "pipeline", "package-smoke.log")).strip().splitlines()
    return (table(["Stage", "Result"], rows) + "\n\nLast line of the smoke test: `%s`." % (md_escape(log[-1]) if log else "no log"))


def h_coverage(M, a):
    if not M.coverage:
        return "No coverage run in this refresh."
    c = M.coverage
    return (table(["Measure", "Covered", "Total", "Percent", "Agreed (UUID7-SRS-090)"], [
        ["Statements (lines)", c.get("line_covered"), c.get("line_total"), "%.1f %%" % c.get("line_percent", 0), ">= 80 %"],
        ["Decisions (branches)", c.get("branch_covered"), c.get("branch_total"), "%.1f %%" % c.get("branch_percent", 0), ">= 65 %"],
        ["Functions", c.get("function_covered"), c.get("function_total"), "%.1f %%" % c.get("function_percent", 0), "-"]]) +
        "\n\nUncovered lines of app/uuid7.c: %s. They are the clock-failure path and the entropy fallback, which the test seams "
        "cannot reach (they bypass clock_gettime() and the built-in RNG's syscalls), and the lost race of the lazy RNG installation; "
        "%s, %s and %s cover them by inspection and analysis (ECSS-E-ST-40 5.8.3.5d). Report: "
        '<a href="evidence/coverage/ITs_coverage.html">gcovr HTML</a>.' % (md_escape(M.uncovered), cref("UUID7-IN-001"),
                                                                       cref("UUID7-IN-002"), cref("UUID7-AN-012")))


RMS_CRITICALITY = [
    ("none", "no rule beyond the lifecycle"),
    ("low", "no rule beyond the lifecycle"),
    ("medium", "no rule beyond the lifecycle"),
    ("high", "two different approvers before the item is approved (kyr_rms workflow)"),
    ("safety", "two different approvers; it marks a safety function, which category D excludes (\\ref UUID7_DOC_TAILORING \"tailoring\" <3>)"),
]


def h_criticality(M, a):
    """The one mapping between the rms criticality field and the ECSS software criticality category, with the items of this
    project counted by both."""
    count = collections.Counter((x.criticality or "not set", x.applicability or "-") for x in M.arts if x.kind == "requirement")
    rows = []
    for level, rule in RMS_CRITICALITY:
        n = {cat: v for (lv, cat), v in count.items() if lv == level}
        rows.append([level, rule, "none: an rms level never sets or changes the ECSS category", ", ".join(
            "%d of category %s" % (v, c) for c, v in sorted(n.items())) or "0"])
    if any(lv == "not set" for lv, _ in count):
        rows.append(["not set", "the CSV import carries no criticality column", "-", ", ".join(
            "%d of category %s" % (v, c) for (lv, c), v in sorted(count.items()) if lv == "not set")])
    bad = [x.id for x in M.arts if x.kind == "requirement" and x.criticality == "safety"]
    return ("Two scales, one mapping. The ECSS software criticality category (A to D, ECSS-Q-ST-80 Table D-1) belongs to the software "
            "product and follows from the consequences of its failure (<3>): every item here is category D, in the `applicability` "
            "field. The rms `criticality` field is a workflow priority of kyr_rms that decides how many approvers an item needs; it "
            "is not an ECSS category and maps to none:\n\n" +
            table(["rms criticality", "What it does in the rms", "ECSS category it maps to", "Requirements here"], rows) +
            "\n\nAn item whose failure could reach the consequences of a category above D would reclassify the software (<3>), not "
            "raise an rms level. %s" % (("**Inconsistent: %s carry rms criticality safety in a category D product.**" % ", ".join(bad))
                                        if bad else "No item carries rms criticality safety."))


def tool_version(M, spec):
    kind, _, name = spec.partition(":")
    if kind == "toolchain":
        v = M.toolchain.get(name, "")
        if name == "environment":
            return ("release root %s" % v.split()[-1][:16]) if v.startswith("release root") else (v or "not recorded")
        return (v + (" (release root)" if M.in_root else " (host)")) if v else "not recorded"
    if kind == "pin":
        pin = [l.split() for l in read(os.path.join(ROOT, "deps.txt")).splitlines() if l.startswith("own") and len(l.split()) > 4
               and l.split()[1] == name]
        return ("%s (sha256 %s…)" % (pin[0][2], pin[0][4][:12])) if pin else "not pinned"
    if kind == "rms":
        if M.mode != "rms":
            return "no export in this refresh"
        return ("%s; " % M.rms["producer"] if M.rms["producer"] else "version not carried by the export; ") + \
            "package format %s, export sha256 %s…" % (M.rms["format"], M.rms["sha256"][:12])
    if name == "python3":
        return sys.version.split()[0] + " (workstation)"
    if name == "doxygen":
        return (tool(["doxygen", "--version"]).strip() or "-") + " (workstation)"
    if name == "graphviz":
        return (re.findall(r"version (\S+)", tool(["dot", "-V"])) or ["-"])[0] + " (workstation)"
    if name == "plantuml":
        return "%s; %s" % (os.path.basename(PLANTUML_JAR), "java present" if shutil.which("java") else "java absent: not rendered")
    if name == "git":
        return (tool(["git", "--version"]).split() or ["-"])[-1] + " (workstation)"
    return "-"


def h_tools(M, a):
    """The tools and reused software of the life cycle with their version in this refresh, use, prior use and how their output
    is checked (ECSS-Q-ST-80 5.6.1, 5.6.2; ECSS-E-ST-40 SDP <5.4>)."""
    path = os.path.join(ECSS, "PAF", "tools.csv")
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    cols = a.get("columns", "tool,version,class,use,prior_use,output_checked_by,availability").split(",")
    head = {"tool": "Tool", "version": "Version in this refresh", "class": "Class", "use": "Use", "prior_use": "Prior use (5.6.1.1)",
            "output_checked_by": "How its output is checked (5.6.1.3)", "availability": "Availability (5.6.1.2)"}
    body = [[md_escape(tool_version(M, r["version"])) if c == "version" else md_escape(r[c]) for c in cols] for r in rows]
    return "Source: `ecss/PAF/tools.csv`.\n\n" + table([head[c] for c in cols], body)


def stack_usage():
    """{function: (bytes, qualifier)} from the gcc -fstack-usage file of the release build (evidence/stack)."""
    out = {}
    for line in read(os.path.join(EVID, "stack", "uuid7.su")).splitlines():
        f = line.split("\t")
        if len(f) == 3:
            out[f[0].rsplit(":", 1)[-1]] = (int(f[1]), f[2])
    return out


def h_budgets(M, a):
    fp = M.checks.get("footprint", {})
    size = read(os.path.join(EVID, "footprint.txt"))
    m = re.search(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\w+\s+\S+$", size, re.M)
    text_b, data_b, bss_b = (m.group(1), m.group(2), m.group(3)) if m else ("?", "?", "?")
    stress = [r for r in M.stress if r["linkage"] == "static"]
    rows = [[r["profile"], r["bench"], r["status"], r["cost"], r["rate"]] for r in stress]
    margin = "not measured in this refresh"
    margin_short = "-"
    m2 = re.search(r"mean ([0-9.]+) ns", M.checks.get("stress_budget", {}).get("detail", ""))
    if m2:
        cost = float(m2.group(1))
        margin_short = "%.1f ns (%.0f %%)" % (100.0 - cost, 100.0 - cost)
        margin = "%.1f ns measured against the 100 ns budget of UUID7-SRS-040: a margin of %.1f ns (%.0f %%)" % (cost, 100.0 - cost,
                                                                                                             100.0 - cost)
    su = stack_usage()
    refs = {m["name"]: m["refs"] for m in getattr(M, "members", []) if m["kind"] == "function"}

    def worst(fn, seen=()):
        if fn in seen:
            return 0
        own = su.get(fn, (0, ""))[0]
        return own + max([worst(r, seen + (fn,)) for r in refs.get(fn, []) if r in refs] or [0])
    pub = sorted(n for n in su if n.startswith("uuid7_"))
    stack_rows = [["%s()" % n, su[n][0], worst(n), su[n][1]] for n in pub]
    deepest = max([r[2] for r in stack_rows] or [0])
    dynamic = [n for n, (_, q) in su.items() if q != "static"]
    st = "%s" % (("worst case %d B per call (%s)" % (deepest, "all frames static" if not dynamic else "**dynamic frames: %s**" %
                                                     ", ".join(dynamic))) if su else "not measured in this refresh")
    fp_sum = sum(int(x) for x in re.findall(r"(\d+) B", fp.get("detail", ""))) if fp.get("verdict") else None
    budget_rows = [
        ["Static data", "UUID7-SRS-050", "24 B", ("%d B" % fp_sum) if fp_sum is not None else "not measured",
         ("%d B" % (24 - fp_sum)) if fp_sum is not None else "-"],
        ["Heap", "UUID7-SRS-051", "0 B", M.checks.get("no_alloc", {}).get("detail", "not measured"), "-"],
        ["Generation cost (release, one thread)", "UUID7-SRS-040", "100 ns", (m2.group(1) + " ns") if m2 else "not measured", margin_short],
        ["Stack per call", "none (no requirement)", "not specified", st, "not computable without a budget"],
        ["Code (.text of the release object)", "none (no requirement)", "not specified", "%s B" % text_b, "not computable without a budget"]]
    return ("Budgets and margins of this refresh, measured in the evidence's environment (%s):\n\n" % md_escape(M.env_text) +
            table(["Budget", "Requirement", "Budget value", "Measured", "Margin"], budget_rows) +
            "\n\nStack and code size are measured and tracked here at every refresh but have no budget requirement; the budgets are "
            "not yet recorded as rms results at the reviews (\\ref UUID7_DOC_SDP \"SDP\" <4.6>).\n\n"
            "Memory (release object app__uuid7.c.o): code %s B, initialised data %s B, static data %s B (%s). No heap, no "
            "thread-local storage.\n\nStack (`gcc -fstack-usage` on the release build; the worst case follows the references between "
            "the functions, the callback counted as the built-in RNG; the C library's own frames are not counted):\n\n%s\n\n"
            "Processing (tests/stress, static linkage). `stress`: one thread, cost in ns per identifier, rate in identifiers per "
            "second. `stress_mt`: 10 threads on one reservation word, cost per identifier per thread under contention, aggregate "
            "rate.\n\n%s\n\nRelease single-thread: %s." %
            (text_b, data_b, bss_b, md_escape(fp.get("detail", "not measured")),
             table(["Function", "Own frame (B)", "Worst case (B)", "Kind"], stack_rows) if stack_rows else "not measured in this refresh",
             table(["Profile", "Benchmark", "Status", "Cost", "Rate"], rows) if rows else "no stress run in this refresh", margin))


def h_checks(M, a):
    rows = [[k, verdict_word(v["verdict"]), md_escape(v["detail"]), '<a href="%s">evidence</a>' % v["evidence"] if v.get("evidence") else "-"]
            for k, v in sorted(M.checks.items())]
    return table(["Check", "Result", "Detail", "Evidence"], rows)


def h_doc_plan(M, a):
    rows = []
    for d in M.docs:
        if d.applicability == "N":
            continue
        rows.append([d.notes.get("file", ""), dref(d.id, M), md_escape(d.notes.get("drd", "-")),
                     d.notes.get("reviews", "").replace(",", ", "), "reviewed at those milestones (SRevP)",
                     "approved by the maintainer at the review; status in the rms"])
    return table(["File", "Document", "DRD", "Delivered at", "Review", "Approval"], rows)


def h_drl(M, a):
    rows = []
    for f, _ in FILES:
        for d in [d for d in M.docs if d.notes.get("file") == f]:
            rows.append([f, md_escape(d.title), md_escape(d.notes.get("drd", "-")), d.notes.get("reviews", "").replace(",", ", "),
                         ("produced: " + dref(d.id, M)) if d.applicability != "N" else "tailored out",
                         md_escape(d.rationale)])
    return table(["File", "DRL item", "DRD", "Reviews", "Disposition", "Clause / justification"], rows)


def h_review_docs(M, a):
    rv = a.get("review")
    docs = [d for d in M.docs if rv in d.notes.get("reviews", "").split(",")]
    rows = [[d.notes.get("file", ""), dref(d.id, M), "produced" if d.applicability != "N" else "tailored out", d.status] for d in docs]
    return table(["File", "Document", "Disposition", "Status"], rows)


def ms_time(ms, fmt="%Y-%m-%d %H:%M:%S"):
    return datetime.datetime.fromtimestamp(ms / 1000).strftime(fmt) if ms else "-"


def who(actor):
    return md_escape(actor) if actor else "**unrecorded**"


# A review recorded as opened and closed within this many seconds was not held by people: no meeting reads a data package,
# raises and dispositions RIDs that fast. The JRR states the measured time with the verdict.
SCRIPTED_REVIEW_S = 600


def cast_declared(M):
    """The rms project's own statement that its cast is scripted (its name or description), or ""."""
    p = M.rms["project"] if M.rms else {}
    text = "%s %s" % (p.get("name", ""), p.get("description", ""))
    return text.strip() if "scripted" in text.lower() else ""


def review_facts(M, rv):
    """What the rms export says about one review: its revisions, opening and closing, the baseline, the RIDs, the actors, and
    whether the record shows a scripted cast (the project says so, or the review was opened and closed in under
    SCRIPTED_REVIEW_S)."""
    a = M.by_id.get("UUID7-REV-" + rv)
    revs = a.revs if a else []
    opened = next((r for r in revs if r["status"] == "in-review"), None)
    closed = next((r for r in revs if opened and r["at_ms"] >= opened["at_ms"] and r["status"] in ("approved", "needs-rework", "rejected")), None)
    bl = [x for x in M.baselines if x["name"].upper().startswith(rv)]
    b = bl[-1] if bl else None
    rids = sorted([x for x in M.arts if x.kind == "review-comment" and x.id.startswith("UUID7-RID-%s-" % rv)], key=lambda x: x.id)
    actors = [r["by"] for r in revs if opened and r["at_ms"] >= opened["at_ms"]] + [r["by"] for x in rids for r in x.revs]
    if b:
        actors += [b["by"]] + ([b["approved_by"]] if b["approval_exported"] else [])
    seconds = (closed["at_ms"] - opened["at_ms"]) / 1000.0 if opened and closed else None
    declared = cast_declared(M)
    scripted = bool(declared) or (seconds is not None and seconds < SCRIPTED_REVIEW_S)
    return {"art": a, "revs": revs, "opened": opened, "closed": closed, "baseline": b, "rids": rids, "seconds": seconds,
            "held": bool(closed and closed["status"] == "approved"), "scripted": scripted, "declared": declared,
            "named": sorted({x for x in actors if x}), "unnamed": len([x for x in actors if not x])}


def review_status(M, rv):
    f = review_facts(M, rv)
    a, b = f["art"], f["baseline"]
    if f["held"] or b:
        cast = " (scripted cast, \\ref UUID7_DOC_JRR \"JRR\")" if f["scripted"] else ""
        txt = "recorded in the rms%s: closed %s by %s" % (cast, ms_time(f["closed"]["at_ms"], "%Y-%m-%d") if f["closed"] else "-",
                                                          who(f["closed"]["by"]) if f["closed"] else "-")
        if b:
            txt += "; baseline %s frozen by %s, %d members" % (md_escape(b["name"]), who(b["by"]), b["members"])
        return txt
    if a:
        return "not held (%s%s); no rms baseline" % (a.status, (", rms revision %s" % a.revision) if a.revision else "")
    return "not planned"


def load_open_items():
    path = os.path.join(ECSS, "MGT", "open_items.csv")
    if not os.path.exists(path):
        return []
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def open_claims(M, place):
    out = []
    for o in M.open_items:
        if o.get("status") == "open" and o.get("claim", "").startswith(place + ":"):
            out.append((o["claim"].split(":", 1)[1].strip(), o["id"]))
    return out


def claim(M):
    """The package's claim, from its own facts: the verification of this refresh, the open items of the SDP <4.6>, the rms
    record of the reviews, the AR. Returns (sentence, [(phrase, basis)])."""
    basis = []
    n_ver = len([r for r in M.reqs if M.req_status[r.id] == "verified"])
    green = M.green if M.pipeline_ran else False
    if n_ver == len(M.reqs) and M.reqs and green:
        basis.append(("documented and self-verified after ECSS-E-ST-40C Rev.1 / ECSS-Q-ST-80C Rev.2 at software criticality category D",
                      "%d of %d requirements verified; pipeline GREEN; evidence of commit %s (%s) in %s; every verification performed "
                      "by the developer" % (n_ver, len(M.reqs), M.ev_commit[:12] or "?", M.ev.get("state", "?"), md_escape(M.env_text))))
    else:
        basis.append(("documented after ECSS-E-ST-40C Rev.1 / ECSS-Q-ST-80C Rev.2 at software criticality category D, self-verification "
                      "incomplete", "%d of %d requirements verified; pipeline %s" % (n_ver, len(M.reqs), (
                          "GREEN" if green else "not green") if M.pipeline_ran else "not run in this refresh")))
    for phrase, oi in open_claims(M, "scope"):
        basis.append((phrase, "open item %s" % oi))
    facts = [review_facts(M, rv) for rv in REVIEWS]
    held = [(rv, f) for rv, f in zip(REVIEWS, facts) if f["held"] or f["baseline"]]
    if M.mode != "rms":
        basis.append(("no review recorded", "no rms export: the reviews live in the rms"))
    elif not held:
        basis.append(("no review held", "the rms export records no closed review"))
    elif any(f["scripted"] for _, f in held):
        sec = ", ".join("%s %.1f s" % (rv, f["seconds"]) for rv, f in held if f["seconds"] is not None)
        basis.append(("the review workflow demonstrated in kyr rms with a scripted cast",
                      "%s recorded in the rms export; %s" % (", ".join(rv for rv, _ in held),
                                                            ("the project declares it: '%s'" % facts[0]["declared"]) if facts[0]["declared"]
                                                            else "opened to closed: " + sec)))
    else:
        basis.append(("reviews %s recorded in kyr rms" % ", ".join(rv for rv, _ in held), "the rms export, \\ref UUID7_DOC_JRR \"JRR\""))
    for phrase, oi in open_claims(M, "review"):
        basis.append((phrase, "open item %s" % oi))
    ar = facts[REVIEWS.index("AR")]
    if ar["held"] and ar["baseline"] and not ar["scripted"]:
        basis.append(("accepted at the AR", "AR closed and baseline %s frozen in the rms" % ar["baseline"]["name"]))
    else:
        basis.append(("not accepted (AR not held)" if not (ar["held"] or ar["baseline"]) else "not accepted (AR scripted)",
                      "the rms export: UUID7-REV-AR %s" % (ar["art"].status if ar["art"] else "absent")))
    for phrase, oi in open_claims(M, "end"):
        basis.append((phrase, "open item %s" % oi))
    return "uuid7 %s: %s." % (M.version, "; ".join(p for p, _ in basis)), basis


def h_claim(M, a):
    text, basis = claim(M)
    return ("**%s**\n\n" % md_escape(text) + table(["Claim", "Basis"], [[md_escape(p), b] for p, b in basis]) +
            "\n\nThe claim is computed at every refresh from the results, the rms export and the open items of the "
            "\\ref UUID7_DOC_SDP \"SDP\" <4.6>; what it does not claim is listed there with what closes it.")


def h_open_items(M, a):
    if not M.open_items:
        return "No open item."
    rows = [[o["id"], md_escape(o["finding"]), md_escape(o["title"]), o["status"], md_escape(o["now"]), md_escape(o["closes_with"]),
             md_escape(o["who"]), md_escape(o["rms_feature"]), md_escape(o["clauses"])] for o in M.open_items]
    n = len([o for o in M.open_items if o["status"] == "open"])
    return ("%d open item(s) of %d. Source: `ecss/MGT/open_items.csv`; the finding is the review's of 2026-10-10.\n\n" % (n, len(M.open_items)) +
            table(["Item", "Finding", "Title", "Status", "True now", "Closed by", "Who", "rms feature needed", "Clauses"], rows))


def h_reviews(M, a):
    rows = [[rv, review_status(M, rv), len([d for d in M.docs if rv in d.notes.get("reviews", "").split(",") and d.applicability != "N"])]
            for rv in REVIEWS]
    return table(["Review", "Status", "Documents in the data package"], rows)


def h_baselines(M, a):
    if not M.baselines:
        return "No baseline: %s." % ("the rms export holds none" if M.mode == "rms" else "baselines live in the rms; no export is present")
    return ("Each rms baseline must name the uuid7 commit it froze (a `commit` field, or the commit in its description); the "
            "members are counted by the status they were frozen at.\n\n" +
            table(["Baseline", "Frozen", "By", "Approved by", "uuid7 commit", "Members by status"],
                  [[md_escape(b["name"]), ms_time(b["created_at_ms"], "%Y-%m-%d %H:%M"), who(b["by"]),
                    (who(b["approved_by"]) if b["approved_by"] else "**no**") if b["approval_exported"] else "not in this export", b["check"],
                    ", ".join("%d %s" % (v, k) for k, v in sorted(b["member_status"].items()))] for b in M.baselines]))


def h_tags(M, a):
    rows = []
    for line in git("for-each-ref", "--sort=v:refname", "refs/tags/v*", "--format=%(refname:short) %(objecttype)").splitlines():
        name, kind = line.split()
        signed = kind == "tag" and bool(re.search(r"-----BEGIN (PGP|SSH) SIGNATURE-----", git("cat-file", "tag", name)))
        rows.append(["`%s`" % name, "annotated" if kind == "tag" else "**lightweight**", "yes" if signed else "**no**",
                     git("rev-list", "-n", "1", name)[:12]])
    note = "" if any(r[0] == "`%s`" % M.tag for r in rows) else "\n\nNo tag `%s` exists: it is created at release." % M.tag
    return (table(["Tag", "Kind", "Signed", "Commit"], rows) if rows else "No release tag.") + note


def h_matrix_csv(M, a):
    path = os.path.join(ECSS, a["file"])
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    head, body = rows[0], rows[1:]
    counts = {}
    for r in body:
        counts[r[3]] = counts.get(r[3], 0) + 1
    def refl(s):
        out = []
        for part in [p.strip() for p in s.split(";")]:
            m = re.match(r"^(\w+)/(\w+)\.md(.*)$", part)
            out.append("%s%s" % ('\\ref %s "%s"' % (page_label(m.group(2) + ".md"), m.group(1) + "/" + m.group(2)) if m else md_escape(part),
                                 md_escape(m.group(3)) if m else ""))
        return "; ".join(out)
    return ("%d clauses: %s. Source: `ecss/%s`.\n\n" % (len(body), ", ".join("%s %d" % (k, v) for k, v in sorted(counts.items())), a["file"]) +
            table([h.replace("_", " ") for h in head], [[md_escape(r[0]), md_escape(r[1]), md_escape(r[2]), r[3], refl(r[4]), md_escape(r[5])]
                                                        for r in body]))


def h_deps(M, a):
    rows = []
    for l in read(os.path.join(ROOT, "deps.txt")).splitlines():
        if l.strip() and not l.startswith("#"):
            f = l.split()
            rows.append([f[0], f[1], f[2] if len(f) > 2 else "", f[3] if len(f) > 3 else "", ("`%s`" % f[4][:16] + "…") if len(f) > 4 else ""])
    return table(["Class", "Package", "Version", "Source", "sha256"], rows)


def h_scf_items(M, a):
    files = git("ls-files").splitlines()
    rows = []
    for f in files:
        p = os.path.join(ROOT, f)
        if os.path.isfile(p):
            with open(p, "rb") as fh:
                rows.append(["`%s`" % f, os.path.getsize(p), "`%s`" % hashlib.sha256(fh.read()).hexdigest()[:16]])
    return ("%d controlled files at commit %s (git ls-files; SHA-256 truncated to 16 hex digits, full values in "
            '<a href="evidence/scf_files.txt">scf_files.txt</a>).\n\n' % (len(rows), M.short) + table(["File", "Bytes", "SHA-256"], rows))


def h_release(M, a):
    sums = read(os.path.join(ROOT, "build", "debs", "SHA256SUMS")).strip().splitlines()
    tags = git("tag", "--list", "v*", "--sort=-v:refname").splitlines()
    prev = tags[0] if tags else ""
    log = git("log", "--oneline", "%s..HEAD" % prev) if prev else git("log", "--oneline", "-20")
    rows = [["`%s`" % l.split()[1], "`%s`" % l.split()[0]] for l in sums if len(l.split()) == 2]
    bi = {d["deb"]: d for d in M.debs}
    rows = [r + [md_escape(bi.get(r[0].strip("`"), {}).get("root", "?")), bi.get(r[0].strip("`"), {}).get("commit", "?")[:12],
                 md_escape(bi.get(r[0].strip("`"), {}).get("gcc", "?")), md_escape(bi.get(r[0].strip("`"), {}).get("libc", "?"))] for r in rows]
    return ("Version %s; evidence of commit %s (%s); release tag %s. Previous release tag: %s.\n\n**Packages** (build/debs/SHA256SUMS, "
            "and each package's .buildinfo: a Build-Root of `none` is a development build, not a release):\n\n%s\n\n"
            "**Changes since %s**:\n\n~~~{.txt}\n%s\n~~~" % (M.version, M.ev_commit or "not recorded", M.ev.get("state", "?"), M.tag_text,
                                                          prev or "none", table(["Package", "SHA-256", "Build-Root", "Commit", "gcc", "libc"], rows)
                                                          if rows else "not built", prev or "the start", log or "none"))


def h_receiving(M, a):
    txt = read(os.path.join(EVID, "deps.txt"))
    pin = [l for l in read(os.path.join(ROOT, "deps.txt")).splitlines() if l.startswith("own")]
    ver = read(os.path.join(ROOT, "build", "sysroot", "share", "kyr-build", "VERSION")).strip()
    return ("Pinned procured software (deps.txt `own` lines): %s. utils/deps.sh downloads the release deb once into the cache, verifies "
            "its SHA-256 against the pin and refuses it otherwise; the installed kyr-build reports version %s. Output of this refresh: "
            '<a href="evidence/deps.txt">deps.txt</a>%s.' % ("; ".join("`%s`" % " ".join(l.split()[:3]) for l in pin), ver or "?",
                                                            "" if txt else " (not run)"))


def h_spr(M, a):
    if not M.crs:
        return ("No software problem report or nonconformance is recorded: %s." %
                ("the rms export holds no change-request" if M.mode == "rms" else "problem reports are rms change-requests and no rms export is present"))
    return table(["Report", "Title", "Status"], [[c.id, md_escape(c.title), c.status] for c in M.crs])


def h_jrr(M, a):
    if M.mode != "rms":
        return "No joint review report: the reviews are recorded in the rms and no rms export is present."
    red = ("**RED: %s.**\n\n" % md_escape(M.refusal)) if M.refusal else ""
    declared = cast_declared(M)
    rows = []
    for rv in REVIEWS:
        f = review_facts(M, rv)
        b = f["baseline"]
        rows.append([rv, "closed: approved" if f["held"] else (f["art"].status if f["art"] else "not planned"),
                     who(f["closed"]["by"]) if f["closed"] else "-", ("%s by %s" % (md_escape(b["name"]), who(b["by"]))) if b else "none",
                     (who(b["approved_by"]) if b["approval_exported"] else "not exported") if b else "-",
                     ("%.1f s" % f["seconds"]) if f["seconds"] is not None else "-",
                     ("scripted" if f["scripted"] else "not shown scripted") if (f["held"] or b) else "-"])
    out = [table(["Review", "Outcome", "Closed by", "Baseline frozen by", "Baseline approved by", "Opened to closed", "Cast"], rows), "",
           "Cast: %s. A review the export shows opened and closed in under %d s is reported as scripted: no group of people reads "
           "a data package and raises, discusses and dispositions RIDs in that time." % (
               ("the rms project declares it: '%s'" % md_escape(declared)) if declared else "the rms project does not declare it",
               SCRIPTED_REVIEW_S), ""]
    for rv in REVIEWS:
        f = review_facts(M, rv)
        if not f["art"]:
            continue
        out += ["## %s %s {#jrr_%s}" % (rv, md_escape(f["art"].title), rv.lower()), ""]
        if not (f["held"] or f["baseline"]):
            out += ["Not held: the review artifact is %s; no baseline." % f["art"].status, ""]
            continue
        out += ["- Participants named by the export: %s%s" % (", ".join(md_escape(x) for x in f["named"]) or "none",
                                                            ("; %d record(s) without an actor" % f["unnamed"]) if f["unnamed"] else ""),
                "- Recorded: opened %s, closed %s by %s (%s)" % (ms_time(f["opened"]["at_ms"]) if f["opened"] else "-",
                                                         ms_time(f["closed"]["at_ms"]) if f["closed"] else "-",
                                                         who(f["closed"]["by"]) if f["closed"] else "-",
                                                         "scripted cast" if f["scripted"] else "cast not shown scripted"),
                "- Decision: %s" % (md_escape(f["closed"]["reason"]) if f["closed"] else "none recorded"),
                "- Baseline: %s" % (("%s, frozen %s by %s, %d members, uuid7 commit %s; approved %s" % (
                    md_escape(f["baseline"]["name"]), ms_time(f["baseline"]["created_at_ms"]), who(f["baseline"]["by"]),
                    f["baseline"]["members"], f["baseline"]["check"], ("by %s %s" % (who(f["baseline"]["approved_by"]), ms_time(f["baseline"]["approved_at_ms"])))
                    if f["baseline"]["approved_by"] else ("(not in this export)" if not f["baseline"]["approval_exported"] else "**no**")))
                    if f["baseline"] else "none"), ""]
        if f["revs"]:
            out += [table(["Revision", "Status", "At", "By", "Reason"], [[r["n"], r["status"], ms_time(r["at_ms"]), who(r["by"]),
                                                                         md_escape(r["reason"])] for r in f["revs"]]), ""]
        if f["rids"]:
            rr = []
            for x in f["rids"]:
                raised = x.revs[0] if x.revs else {"by": "", "at_ms": 0}
                last = x.revs[-1] if x.revs else {"by": "", "status": x.status, "reason": ""}
                rr.append([x.id, md_escape(x.title), who(raised["by"]), x.status, who(last["by"]) if len(x.revs) > 1 else "-",
                           md_escape(last.get("reason", ""))])
            out += ["RIDs:", "", table(["RID", "Discrepancy", "Raised by", "Disposition", "Dispositioned by", "Reason"], rr), ""]
    return red + "\n".join(out)


def h_metrics(M, a):
    src = read(os.path.join(ROOT, "app", "uuid7.c"))
    sloc = len([l for l in src.splitlines() if l.strip() and not l.strip().startswith(("/*", "*", "//"))])
    funcs = [s for s in M.symbols.values() if s["kind"] == "function" and s["file"].endswith(".c")]
    failures = sum(1 for c in M.cases if M.result.get(c.id, {}).get("verdict") == "failed")
    return table(["Metric (SPAP <7>)", "Value", "Target"], [
        ["Size: non-comment source lines of app/uuid7.c", sloc, "-"], ["Size: documented functions in app/uuid7.c", len(funcs), "-"],
        ["Test coverage (statement / decision)", "%.1f %% / %.1f %%" % (M.coverage.get("line_percent", 0), M.coverage.get("branch_percent", 0))
         if M.coverage else "not run", ">= 80 % / >= 65 %"],
        ["Compiler warnings, six profiles", M.checks.get("no_warnings", {}).get("detail", "not run"), "0"],
        ["Failed verification cases (problems found in validation)", failures, "0"],
        ["Requirements without a passing verification", len([r for r in M.reqs if M.req_status[r.id] != "verified"]), "0"]])


def h_api(M, a):
    def decl(m):
        if m["kind"] == "function":
            return "`%s %s%s`" % (m["type"], m["name"], m["args"])
        if m["kind"] == "typedef":
            return "`typedef %s%s%s`" % (m["type"], m["name"], m["args"])
        return "`#define %s %s`" % (m["name"], m["init"])
    rows = [["`%s`" % m["name"], m["kind"], decl(m), md_escape(m["brief"])] for m in getattr(M, "members", []) if m["file"] == "uuid7.h"]
    return table(["Name", "Kind", "Declaration", "Brief"], rows)


def h_source(M, a):
    if M.mode == "rms":
        return ("Source of truth: the rms export `%s` (sha256 `%s`, project %s): %d artifacts, %d links, %d baselines; verification "
                "results %s. The repository's tags and CSVs are compared with it: %d repository link(s) are not in the export." %
                (M.rms["path"], M.rms["sha256"][:16], PROJECT_KEY, len(M.rms["arts"]), len(M.rms["links"]), len(M.baselines),
                 "from `verification.csv`" if M.rms["verdicts"] else "not exported", len(M.drift)))
    return ("Source of truth: the repository's CSVs (`ecss/TS/requirements.csv` and the others, in the rms import format), because no "
            "rms export is present under `ecss/rms_export/`. Links come from the code's `@satisfies` / `@verifies` tags and the CSV notes.")


def h_flags(M, a):
    rc, out = run(["bash", "-c", "./utils/gcc_build_profiles.sh print-cflags %s; ./utils/gcc_build_profiles.sh print-cppflags %s; "
                   "./utils/gcc_build_profiles.sh print-ldflags %s" % ((a.get("profile", "release"),) * 3)])
    flags = re.sub(r"-f(?:file|debug)-prefix-map=\S+", "", out)
    return "~~~{.txt}\n%s\n~~~" % "\n".join(l.strip() for l in flags.splitlines() if l.strip())


HANDLERS = {
    "docinfo": h_docinfo, "refs": h_refs, "terms": h_terms, "reqs": h_reqs, "validation-matrix": h_validation_matrix,
    "upward-trace": h_upward_trace, "reqtable": h_reqtable, "components": h_components, "allocation": h_allocation,
    "diagram": h_diagram, "scenarios": h_scenarios, "testcases": h_testcases, "noncases": h_noncases, "case-matrix": h_case_matrix,
    "results-cells": h_results_cells, "results-tests": h_results_tests, "req-results": h_req_results, "trace-matrix": h_trace_matrix,
    "gaps": h_gaps, "summary": h_summary, "pipeline": h_pipeline, "coverage": h_coverage, "budgets": h_budgets, "checks": h_checks,
    "doc-plan": h_doc_plan, "drl": h_drl, "review-docs": h_review_docs, "reviews": h_reviews, "baselines": h_baselines,
    "matrix-csv": h_matrix_csv, "deps": h_deps, "scf-items": h_scf_items, "release": h_release, "receiving": h_receiving,
    "spr": h_spr, "jrr": h_jrr, "install": h_install, "metrics": h_metrics, "api": h_api, "source": h_source, "flags": h_flags,
    "claim": h_claim, "open-items": h_open_items, "tools": h_tools, "criticality": h_criticality, "configuration": h_configuration, "toolchain": h_toolchain, "tags": h_tags,
}
DIRECTIVE = re.compile(r"^<!--[ \t]*ecss:([\w-]+)((?:[ \t]+[\w-]+=(?:\"[^\"]*\"|\S+))*)[ \t]*-->[ \t]*$", re.M)


def expand(M, text, where):
    def one(m):
        args = {k: v.strip('"') for k, v in re.findall(r"([\w-]+)=(\"[^\"]*\"|\S+)", m.group(2))}
        h = HANDLERS.get(m.group(1))
        if not h:
            print("ecss.py: %s: unknown directive %s" % (where, m.group(1)), file=sys.stderr)
            return m.group(0)
        return h(M, args)
    return DIRECTIVE.sub(one, text)


def gen_docs(M):
    os.makedirs(MD, exist_ok=True)
    for path in sorted(glob.glob(os.path.join(ECSS, "*", "*.md"))):
        if os.path.basename(os.path.dirname(path)) in ("tools", "rms_export", "common"):
            continue
        out = os.path.join(MD, os.path.relpath(path, ECSS))
        write(out, expand(M, read(path), rel(path)))
    readme = read(os.path.join(ECSS, "README.md"))
    readme = re.sub(r"^# (.*)$", r"# \1 {#UUID7_DOC_README}", readme, count=1, flags=re.M)
    write(os.path.join(MD, "README.md"), expand(M, readme, "ecss/README.md"))
    gen_index(M)
    components_dox(M.comps)


def gen_index(M):
    n_sd = len([t for t in M.tests if t["suite"] == "SD"])
    L = ["# uuid7 %s — ECSS package (category D)" % M.version, "",
         "The time-ordered UUIDv7 library of the kyr platform, documented as software of criticality category D after ECSS-E-ST-40C "
         "Rev.1 and ECSS-Q-ST-80C Rev.2. Every page is generated by `ecss/make.sh` from the rms (or, without an export, the "
         "repository's CSVs) and from the repository's own facts; nothing here is edited by hand. The pipeline is explained in "
         "\\ref UUID7_DOC_README \"the ECSS pipeline\".", "",
         "## Claim", ""] + (["**RED: %s.**" % md_escape(M.refusal), ""] if M.refusal else []) + [h_claim(M, {}), "",
         "Open items: %d (\\ref UUID7_DOC_SDP \"SDP\" <4.6>)." % len([o for o in M.open_items if o["status"] == "open"]), "",
         "## Identification", "", h_configuration(M, {}), "", table(["Item", "Value"], [
             ["Generated", M.generated], ["Criticality", "category D (ECSS-Q-ST-80 Table D-1), \\ref UUID7_DOC_TAILORING \"tailoring\""],
             ["Diagrams", "%d PlantUML sources; %s" % (len(M.diagrams), M.render_note or "rendered to SVG")]]), "",
         h_source(M, {}), "", "## Status", "", h_summary(M, {}), "", "## Documents by ECSS file", ""]
    for f, name in FILES:
        docs = [d for d in M.docs if d.notes.get("file") == f]
        if not docs:
            continue
        rows = []
        for d in docs:
            rows.append([dref(d.id, M) if d.applicability != "N" else md_escape(d.title), md_escape(d.notes.get("drd", "-")),
                         d.notes.get("reviews", "").replace(",", ", "),
                         ("%s%s" % (d.status, " (generated)" if d.notes.get("gen") == "yes" else "")) if d.applicability != "N"
                         else "tailored out"])
        L += ["### %s (%s)" % (f, name), "", table(["Document", "DRD", "Reviews", "Status"], rows), ""]
    L += ["## Documents by review", "", h_reviews(M, {}), ""]
    for rv in REVIEWS:
        docs = [d for d in M.docs if rv in d.notes.get("reviews", "").split(",") and d.applicability != "N"]
        L += ["**%s**: %s" % (rv, ", ".join(dref(d.id, M) for d in docs) or "none"), ""]
    L += ["## Diagrams", "", "Scenario sequence diagrams (generated by the %d scenario tests of `tests/plantuml`): \\ref UUID7_DOC_SUM "
          "\"SUM <11>\"; class, package and component diagrams: \\ref UUID7_DOC_SDD \"SDD\"." % n_sd, "",
          table(["Diagram", "Title", "Rendered"], [[n, md_escape(t), "SVG" if os.path.exists(os.path.join(SVG, n + ".svg")) else "PlantUML source"]
                                                    for n, t in sorted(M.diagrams.items())]), "",
          "## Code", "", "The API reference (Files, Topics) is generated from the doc comments of `app/`; the `@satisfies` and `@verifies` "
          "tags of the code and the tests become the \\ref satisfies \"implementation trace\" and \\ref verifies \"verification trace\" "
          "lists.", ""]
    write(os.path.join(MD, "index.md"), "\n".join(L))


# ---------------------------------------------------------------------------------------------------------------------------------
# The rms loader: the repository's facts into an rms database
# ---------------------------------------------------------------------------------------------------------------------------------

def gen_rms(M):
    d = os.path.join(OUT, "rms")
    os.makedirs(d, exist_ok=True)
    arts = load_csv(os.path.join(ECSS, "TS", "requirements.csv"), "csv") + load_csv(os.path.join(ECSS, "documents.csv"), "csv") + \
        load_csv(os.path.join(ECSS, "DDF", "components.csv"), "csv") + load_csv(os.path.join(ECSS, "MGT", "reviews.csv"), "csv") + \
        load_csv(os.path.join(ECSS, "DJF", "verification_cases.csv"), "csv")
    ids = {a.id for a in arts}
    for t in M.tests:
        if t["id"] not in ids:
            a = M.by_id[t["id"]]
            arts.append(a)
    with open(os.path.join(d, "artifacts.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(CSV_COLUMNS)
        for a in arts:
            w.writerow(a.row())
    with open(os.path.join(d, "links.tsv"), "w", encoding="utf-8") as f:
        for s, t, dd, o in M.repo_links:
            f.write("%s\t%s\t%s\n" % (s, t, dd))
    with open(os.path.join(d, "results.tsv"), "w", encoding="utf-8") as f:
        for c in M.cases:
            r = M.result.get(c.id, {})
            v = {"passed": "passed", "failed": "failed"}.get(r.get("verdict"), "not-run")
            f.write("%s\t%s\t%s\n" % (c.id, v, (r.get("detail", "") or "").replace("\t", " ")[:900]))
    write(os.path.join(d, "load.sh"), LOADER.replace("@COMMIT@", M.short).replace("@VERSION@", M.version))
    write(os.path.join(d, "export.sh"), EXPORTER)
    os.chmod(os.path.join(d, "load.sh"), 0o755)
    os.chmod(os.path.join(d, "export.sh"), 0o755)


LOADER = r"""#!/usr/bin/env bash
# load.sh <db-root> <actor-uid> — generated by ecss/make.sh (uuid7 @VERSION@, commit @COMMIT@): puts the repository's facts into an
# rms database (kyr-rms-cli, $KYR_RMS_CLI or on PATH): the artifacts of the CSVs and of the tests, the links of the tags and notes,
# the results of this refresh. Idempotent: the import recognises unchanged rows, an existing link is refused and skipped, a result
# is a new record each run (the latest one counts).
set -uo pipefail
DB="${1:?usage: load.sh <db-root> <actor-uid>}"; ACT="${2:?usage: load.sh <db-root> <actor-uid>}"
CLI="${KYR_RMS_CLI:-kyr-rms-cli}"; P=UUID7
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; ROOT="$(cd "${HERE}/../../.." && pwd)"
rms() { "${CLI}" "${DB}" "$@" --actor "${ACT}"; }
rms project create "${P}" 'uuid7 time-ordered UUIDv7 library' > /dev/null 2>&1 || true   # exists already: refused, fine
rms import "${HERE}/artifacts.csv" --map "${ROOT}/ecss/TS/requirements.map.json" --project "${P}" --reason 'uuid7 @VERSION@ @COMMIT@' || exit 1
n=0; while IFS=$'\t' read -r s t d; do rms link "$s" "$t" "$d" --project "${P}" >/dev/null 2>&1 && n=$((n + 1)); done < "${HERE}/links.tsv"
printf 'links created: %d\n' "$n"
while IFS=$'\t' read -r c v sum; do
    [[ "$v" == not-run ]] && continue
    rms record "$c" "$v" --project "${P}" --summary "$sum" --env 'uuid7 @VERSION@ @COMMIT@, ecss/make.sh' >/dev/null || printf 'record %s failed\n' "$c"
done < "${HERE}/results.tsv"
printf 'loaded; review, freeze (kyr-rms-cli %s baseline <REVIEW> --project %s), then export.sh\n' "${DB}" "${P}"
"""

EXPORTER = r"""#!/usr/bin/env bash
# export.sh <db-root> <actor-uid> — generated by ecss/make.sh: the rms project UUID7 into ecss/rms_export/ (package.json, the
# lossless package; verification.csv, the verification matrix with each case's latest result), which the next make.sh reads.
set -euo pipefail
DB="${1:?usage: export.sh <db-root> <actor-uid>}"; ACT="${2:?usage: export.sh <db-root> <actor-uid>}"
CLI="${KYR_RMS_CLI:-kyr-rms-cli}"; P=UUID7
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; ROOT="$(cd "${HERE}/../../.." && pwd)"
mkdir -p "${ROOT}/ecss/rms_export"
"${CLI}" "${DB}" package export --project "${P}" --actor "${ACT}" > "${ROOT}/ecss/rms_export/package.json"
"${CLI}" "${DB}" matrix verification --project "${P}" --actor "${ACT}" > "${ROOT}/ecss/rms_export/verification.csv"
printf 'exported: ecss/rms_export/package.json, ecss/rms_export/verification.csv\n'
"""


def summary(M):
    js = {"version": M.version, "commit": M.commit, "mode": M.mode, "requirements": len(M.reqs),
          "srs": len([r for r in M.reqs if r.notes.get("doc") == "SRS"]), "icd": len([r for r in M.reqs if r.notes.get("doc") == "ICD"]),
          "verified": len([r for r in M.reqs if M.req_status[r.id] == "verified"]), "cases": len(M.cases),
          "tests": len([t for t in M.tests if t["exe"]]), "scenarios": len([t for t in M.tests if t["suite"] == "SD"]),
          "documents": len([d for d in M.docs if d.applicability != "N"]), "tailored_out": len([d for d in M.docs if d.applicability == "N"]),
          "diagrams": len(M.diagrams), "svg": len(glob.glob(os.path.join(SVG, "*.svg"))), "render_note": M.render_note,
          "pipeline_green": M.green, "pipeline_ran": M.pipeline_ran, "claim": claim(M)[0], "evidence_commit": M.ev_commit,
          "evidence_state": M.ev.get("state", ""), "environment": M.env_text, "config_flags": M.config_flags, "tag": M.tag_text,
          "open_items": len([o for o in M.open_items if o["status"] == "open"]), "refusal": M.refusal}
    write(os.path.join(OUT, "summary.json"), json.dumps(js, indent=1) + "\n")
    return js


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "generate"
    if cmd == "api-doxyfile":
        sys.stdout.write(api_doxyfile())
        return 0
    if cmd == "components-dox":
        rms = load_rms()
        comps = [a for a in rms["arts"] if a.kind == "component"] if rms else load_csv(os.path.join(ECSS, "DDF", "components.csv"), "csv")
        components_dox(comps)
        return 0
    if cmd != "generate":
        print(__doc__)
        return 2
    M = build_model()
    gen_diagrams(M)
    render(M)
    gen_docs(M)
    gen_rms(M)
    files = [l for l in git("ls-files").splitlines() if os.path.isfile(os.path.join(ROOT, l))]
    lines = []
    for f in files:
        with open(os.path.join(ROOT, f), "rb") as fh:
            lines.append("%s  %s" % (hashlib.sha256(fh.read()).hexdigest(), f))
    write(os.path.join(EVID, "scf_files.txt"), "\n".join(lines) + "\n")
    js = summary(M)
    print("ecss.py: %(requirements)d requirements (%(srs)d SRS, %(icd)d ICD), %(verified)d verified; %(cases)d verification cases; "
          "%(documents)d documents, %(tailored_out)d DRL items tailored out; %(diagrams)d diagrams, %(svg)d rendered; source: %(mode)s" % js)
    if M.refusal:
        print("ecss.py: RED: " + M.refusal, file=sys.stderr)
        return 3
    return 0


def components_dox(comps):
    """The doxygen groups of the SDD components (the code's @ingroup targets)."""
    dox = []
    for c in sorted(comps, key=lambda c: c.id):
        g = c.notes.get("group")
        if g:
            dox += ["/**", " * @defgroup %s %s" % (g, c.title), " * @brief %s" % c.body.replace("*/", "* /"), " *",
                    " * SDD component %s: \\ref %s \"its description\"." % (c.id, anchor("cmp_" + c.id)), " */", ""]
    write(os.path.join(MD, "components.dox"), "\n".join(dox))


if __name__ == "__main__":
    sys.exit(main())
