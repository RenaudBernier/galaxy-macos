#!/usr/bin/env python3
"""Rewrite `123l` / `123ul` literals to `(s32)123` / `(u32)123`.

On the Wii `long` is s32, so this is type-identical there; on LP64 hosts the
suffix would produce a 64-bit type and change overload resolution.
"""
import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIRS = ["include", "libs/JSystem/include", "libs/nw4r/include", "libs/RVLFaceLib/include",
        "src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib"]
LIT = re.compile(r'(?<![\w.])((?:0[xX][0-9a-fA-F]+)|(?:\d+))([uU]?)[lL](?![lL\w])')
# split a line into code / string / char / comment segments (single-line granularity)
TOK = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//.*$|/\*.*?\*/)')

def fix_line(line):
    if line.lstrip().startswith("#"):
        return line
    parts = TOK.split(line)
    for i in range(0, len(parts), 2):  # even indices are code
        parts[i] = LIT.sub(lambda m: f"(u32){m.group(1)}" if m.group(2) else f"(s32){m.group(1)}", parts[i])
    return "".join(parts)

total = 0
for d in DIRS:
    for dp, _, fs in os.walk(os.path.join(ROOT, d)):
        for f in fs:
            if not f.endswith((".c", ".cpp", ".h", ".hpp")):
                continue
            p = os.path.join(dp, f)
            s = open(p, encoding="utf-8", errors="surrogateescape").read()
            out, in_block = [], False
            for line in s.split("\n"):
                if in_block:
                    out.append(line)
                    if "*/" in line:
                        in_block = False
                    continue
                if "/*" in line and "*/" not in line.split("/*", 1)[1]:
                    in_block = True
                    head, tail = line.split("/*", 1)
                    out.append(fix_line(head) + "/*" + tail)
                    continue
                out.append(fix_line(line))
            n = "\n".join(out)
            if n != s:
                total += sum(1 for a, b in zip(s.split("\n"), out) if a != b)
                open(p, "w", encoding="utf-8", errors="surrogateescape").write(n)
print("changed lines:", total)
