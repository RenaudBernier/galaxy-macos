#!/usr/bin/env python3
"""Syntax-check decomp sources with host clang and bucket the errors.

Usage: trial_compile.py [--sample N] [--seed S] [--show] <dir or file>...
"""
import argparse, collections, os, random, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INCLUDES = [
    "port/include",
    "include",
    "libs/JSystem/include",
    "libs/RVLFaceLib/include",
    "libs/RVL_SDK/include",
    "libs/nw4r/include",
]
CXXFLAGS = ["-std=gnu++17", "-Wno-register", "-fshort-wchar", "-funsigned-char", "-fno-exceptions", "-fno-rtti", "-fsyntax-only",
            "-ferror-limit=0",
            "-DTARGET_PC=1", "-DVERSION=0", "-finput-charset=UTF-8", "-Werror=int-to-pointer-cast", "-Wno-c++11-narrowing"]
CFLAGS = ["-std=gnu11", "-fshort-wchar", "-funsigned-char", "-fsyntax-only", "-ferror-limit=0", "-w",
          "-DTARGET_PC=1", "-DVERSION=0"]

AURORA = False
AURORA_INCLUDE = os.environ.get("AURORA_INCLUDE", os.path.join(ROOT, "..", "deps", "aurora", "include"))

def compile_one(path):
    is_c = path.endswith(".c")
    cmd = ["clang" if is_c else "clang++"] + (CFLAGS if is_c else CXXFLAGS)
    if AURORA:
        cmd += ["-DPORT_AURORA=1"]
    cmd += ["-include", "port/prelude.h"]
    for i in INCLUDES:
        cmd += ["-I", i]
    if AURORA:
        cmd += ["-I", AURORA_INCLUDE]
    cmd.append(path)
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    return path, r.returncode, r.stderr

def normalize(msg):
    msg = re.sub(r"'[^']*'", "'X'", msg)
    msg = re.sub(r"\d+", "N", msg)
    return msg

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--show", action="store_true", help="print raw errors")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--list-failing", action="store_true")
    ap.add_argument("--grep", help="print TUs (and first full stderr) whose errors match this regex")
    ap.add_argument("--aurora", action="store_true", help="route SDK headers to Aurora (-DPORT_AURORA)")
    a = ap.parse_args()
    global AURORA
    AURORA = a.aurora
    files = []
    for p in a.paths:
        full = os.path.join(ROOT, p)
        if os.path.isdir(full):
            for d, _, fs in os.walk(full):
                files += [os.path.relpath(os.path.join(d, f), ROOT) for f in fs if f.endswith((".cpp", ".c"))]
        else:
            files.append(p)
    files.sort()
    if a.sample and a.sample < len(files):
        random.Random(a.seed).shuffle(files)
        files = sorted(files[:a.sample])
    buckets = collections.Counter()
    examples = {}
    headers = collections.Counter()
    failing = []
    with ThreadPoolExecutor(os.cpu_count()) as ex:
        for path, rc, err in ex.map(compile_one, files):
            if rc != 0:
                failing.append(path)
            if a.grep and re.search(a.grep, err):
                if not examples.get("__grep_shown"):
                    examples["__grep_shown"] = True
                    print(f"==== {path}\n{err[:6000]}")
                else:
                    print(f"==== {path}")
            seen = set()
            for line in err.splitlines():
                m = re.match(r"(.*?):(\d+):\d+: (fatal )?error: (.*)", line)
                if not m:
                    continue
                key = (m.group(1), m.group(2), m.group(4))
                if key in seen:
                    continue
                seen.add(key)
                n = normalize(m.group(4))
                buckets[n] += 1
                examples.setdefault(n, line)
                headers[m.group(1)] += 1
                if a.show:
                    print(line)
    print(f"\n{len(files)} files, {len(failing)} failing, {sum(buckets.values())} errors")
    print("\n== error kinds ==")
    for k, v in buckets.most_common(a.top):
        print(f"{v:6d}  {k}\n        e.g. {examples[k][:220]}")
    print("\n== files with most errors ==")
    for k, v in headers.most_common(25):
        print(f"{v:6d}  {k}")
    if a.list_failing:
        print("\n== failing ==")
        print("\n".join(failing))

if __name__ == "__main__":
    main()
