#!/usr/bin/env python3
"""Fail when a tracked doc points at a path that does not exist.

Docs drift: a link or a named path that survives a rename or a delete is a
broken claim, and a broken claim is what lets a stale doc mislead a reader.
Cheap guard, not a general link checker.

Also checks that the generated repository map is current, since a stale map
sends the reader to files that moved.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
PATH = re.compile(r"`([A-Za-z0-9_][A-Za-z0-9_./-]*/[A-Za-z0-9_./-]+\.[A-Za-z0-9]+)`")
SKIP = ("results/", "llvm/", "/")


def tracked_docs():
    out = subprocess.run(["git", "ls-files", "*.md"], cwd=ROOT,
                         capture_output=True, text=True, check=True)
    return [ROOT / p for p in out.stdout.split()]


def scan():
    problems = []
    for doc in tracked_docs():
        rel = str(doc.relative_to(ROOT))
        if rel.startswith(SKIP):
            continue
        text = doc.read_text(encoding="utf-8")
        for target in LINK.findall(text):
            if target.startswith(("http://", "https://", "#", "mailto:")):
                continue
            path = target.split("#", 1)[0]
            if path and not path.startswith(SKIP) and not (doc.parent / path).exists():
                problems.append(f"{rel}: link target missing: {target}")
        for path in PATH.findall(text):
            if not path.startswith(SKIP) and not (ROOT / path).exists():
                problems.append(f"{rel}: named path missing: {path}")
    return problems


def check_repo_map():
    result = subprocess.run([sys.executable, "tools/gen_repo_map.py", "--check"],
                            cwd=ROOT, capture_output=True, text=True)
    if result.returncode == 0:
        return []
    return [(result.stdout + result.stderr).strip()]


def main():
    problems = scan()
    problems += check_repo_map()
    for p in problems:
        print(p)
    if problems:
        print(f"{len(problems)} broken reference(s)")
        return 1
    print("docs: all links and named paths resolve")
    print("docs: repository map is up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
