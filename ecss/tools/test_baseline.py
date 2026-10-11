#!/usr/bin/env python3
"""test_baseline.py — ecss.py freeze and baseline in a scratch clone of HEAD (build/tmp/test_baseline): a baseline frozen from a
synthetic export and evidence of that commit passes, and the release is refused when the directory is missing, a frozen file
changed, the MANIFEST names another commit than its export, the evidence is of another commit, or the product changed since.
Exit 1 on the first case that does not hold."""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
S = os.path.join(ROOT, "build", "tmp", "test_baseline")


def sh(*cmd, env=None, ok=(0,)):
    p = subprocess.run(cmd, cwd=S, capture_output=True, text=True, env=dict(os.environ, **(env or {})))
    if p.returncode not in ok:
        sys.exit("test_baseline: %s: exit %d: %s" % (" ".join(cmd), p.returncode, (p.stdout + p.stderr).strip()))
    return p.returncode, p.stdout + p.stderr


def refused(why, expect):
    rc, out = sh("python3", "ecss/tools/ecss.py", "baseline", "T-1", ok=(0, 5))
    if rc != 5 or expect not in out:
        sys.exit("test_baseline: %s: not refused as expected (%r): %s" % (why, expect, out.strip()))
    print("ok   refused: %s" % why)


def main():
    shutil.rmtree(S, ignore_errors=True)
    subprocess.run(["git", "clone", "--quiet", "--no-tags", ROOT, S], check=True)
    # the clone runs the checkout's tools (ecss.py may be uncommitted while it is tested)
    shutil.copytree(os.path.join(ROOT, "ecss", "tools"), os.path.join(S, "ecss", "tools"), dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__"))
    sh("git", "-c", "user.name=t", "-c", "user.email=t@localhost", "commit", "-qam", "tools under test", ok=(0, 1))
    head = sh("git", "rev-parse", "HEAD")[1].strip()
    exp = os.path.join(S, "build", "tmp", "export")
    os.makedirs(exp)
    json.dump({"kyr_rms_package": 1, "kyr_rms_version": "test", "exported_by_name": "Tester", "project": {"key": "UUID7"},
               "artifacts": [], "edges": [], "baselines": [{"name": "T-1", "commit": head, "created_by_name": "Tester",
                                                            "approved_by_name": "Approver", "members": []}]},
              open(os.path.join(exp, "package.json"), "w"))
    ev = os.path.join(S, "build", "ecss", "evidence")
    os.makedirs(ev)
    open(os.path.join(ev, "configuration.txt"), "w").write("commit: %s\nstate: clean\n" % head)
    open(os.path.join(ev, "toolchain.txt"), "w").write("environment: release root test\n")
    rc, out = sh("python3", "ecss/tools/ecss.py", "freeze", "T-1", env={"ECSS_RMS_EXPORT": exp})
    rc, out = sh("python3", "ecss/tools/ecss.py", "baseline", "T-1", "--unpack")
    print("ok   frozen and accepted: " + out.strip().splitlines()[-1])
    sh("python3", "ecss/tools/ecss.py", "freeze", "T-1", env={"ECSS_RMS_EXPORT": exp}, ok=(5,))
    print("ok   refused: a second freeze of T-1")
    d = os.path.join(S, "ecss", "baseline", "T-1")
    keep = os.path.join(S, "build", "tmp", "T-1")
    shutil.copytree(d, keep)

    def restore():
        shutil.rmtree(d, ignore_errors=True)
        shutil.copytree(keep, d)
    with open(os.path.join(d, "package.json"), "a") as f:
        f.write(" ")
    refused("a frozen file changed", "is not the file the MANIFEST sums")
    restore()
    man = open(os.path.join(d, "MANIFEST")).read()
    open(os.path.join(d, "MANIFEST"), "w").write(man.replace("commit: " + head, "commit: " + "0" * 40))
    refused("the MANIFEST names another commit than its export", "names commit")
    restore()
    open(os.path.join(ev, "configuration.txt"), "w").write("commit: %s\nstate: clean\n" % ("1" * 40))
    sh("python3", "-c", "import sys; sys.path.insert(0, 'ecss/tools'); import ecss; ecss.evidence_tar(%r, 0)" % os.path.join(d, "evidence.tar.gz"))
    man = open(os.path.join(d, "MANIFEST")).read().splitlines()
    sha = sh("sha256sum", os.path.join(d, "evidence.tar.gz"))[1].split()[0]
    open(os.path.join(d, "MANIFEST"), "w").write("\n".join((sha + "  evidence.tar.gz") if l.endswith("  evidence.tar.gz") else l for l in man) + "\n")
    refused("the evidence is of another commit", "the frozen evidence is of commit " + "1" * 40)
    restore()
    shutil.rmtree(d)
    refused("the baseline directory is missing", "MANIFEST is missing")
    restore()
    with open(os.path.join(S, "VERSION"), "a") as f:
        f.write("\n")
    sh("git", "-c", "user.name=t", "-c", "user.email=t@localhost", "commit", "-qam", "a product change")
    refused("the product changed since the baseline's commit", "differs from this commit's in VERSION")
    shutil.rmtree(S)
    print("test_baseline: all cases hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
