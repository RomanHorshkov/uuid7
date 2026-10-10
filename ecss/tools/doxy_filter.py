#!/usr/bin/env python3
"""doxy_filter.py <file> — doxygen's FILTER_PATTERNS for app/ and tests/: on the @satisfies and @verifies lines, every requirement id
becomes a link to its entry in the SRS or the ICD. Line count unchanged, so the source browser stays aligned."""
import re
import sys

ID = re.compile(r"\b(UUID7-(?:SRS|ICD)-\d{3})\b")

with open(sys.argv[1], encoding="utf-8") as f:
    for line in f:
        if "@satisfies" in line or "@verifies" in line:
            line = ID.sub(lambda m: '\\ref req_%s "%s"' % (m.group(1).replace("-", "_"), m.group(1)), line)
        sys.stdout.write(line)
