#!/usr/bin/env python3
"""doxy_filter.py <file> — doxygen's FILTER_PATTERNS for app/ and tests/: on the @satisfies and @verifies lines, every requirement id
becomes a link to its entry in the SRS or the ICD; in the doc comments, a documented macro, type or variable named bare (outside
code and backticks) becomes doxygen's #name link, as a function is linked by its (). Line count unchanged, so the source browser
stays aligned."""
import json
import os
import re
import sys

ID = re.compile(r"\b(UUID7-(?:SRS|ICD)-\d{3})\b")
HERE = os.path.dirname(os.path.abspath(__file__))
try:
    with open(os.path.join(HERE, "..", "..", "build", "ecss", "links.json"), encoding="utf-8") as f:
        names = [n for n in json.load(f)["symbols"] if not re.match(r"^(?:uuid7_[a-z_]+|_[a-z_]+|test_\w+|sd_\w+)$", n) or n.endswith("_t")]
except (OSError, ValueError, KeyError):
    names = []
SYM = re.compile(r"(?<![\w#@`\\.-])(%s)\b(?!`)" % "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True))) if names else None


def link_bare(text):
    # backtick spans stay as they are: doxygen links the names inside them itself
    parts = re.split(r"(`[^`]*`)", text)
    return "".join(p if p.startswith("`") else SYM.sub(r"#\1", p) for p in parts)


code, doc = False, False
with open(sys.argv[1], encoding="utf-8") as f:
    for line in f:
        starts = re.match(r"\s*(?:/\*\*|///|//!)", line)
        if "@satisfies" in line or "@verifies" in line:
            line = ID.sub(lambda m: '\\ref req_%s "%s"' % (m.group(1).replace("-", "_"), m.group(1)), line)
        elif SYM and (doc or starts):
            if re.search(r"[@\\](?:code|verbatim)\b", line):
                code = True
            elif re.search(r"[@\\](?:endcode|endverbatim)\b", line):
                code = False
            elif not code:
                line = link_bare(line)
        if re.match(r"\s*/\*\*", line) and "*/" not in line.split("/**", 1)[1]:
            doc = True
        elif doc and "*/" in line:
            doc = False
        sys.stdout.write(line)
