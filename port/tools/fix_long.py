#!/usr/bin/env python3
"""Rewrite ILP32 `long` spellings in decomp code to fixed-width typedefs.

On the Wii (MWCC, ILP32) `long` is 32 bits and u32/s32 are typedefs of
`unsigned long`/`long`, so this rewrite is ABI- and mangling-neutral there,
while making the code correct on LP64 hosts.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIRS = ["include", "libs/JSystem/include", "libs/nw4r/include", "libs/RVLFaceLib/include",
        "libs/RVL_SDK/include", "src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib"]
SKIP = {
    "libs/RVL_SDK/include/revolution/types.h",   # defines u32/s32 themselves
    "libs/RVL_SDK/include/macros.h",             # pointer casts: LP64 long is right
    "libs/nw4r/include/nw4r/misc.h",             # pointer-sized typedefs
    "libs/RVL_SDK/include/revolution/euart.h",   # not built on host
    "libs/RVL_SDK/include/revolution/bte/data_types.h",
}
EXTS = (".c", ".cpp", ".h", ".hpp", ".inc")

PAT = re.compile(r"""
    (?P<ll>\b(?:unsigned\s+|signed\s+)?long\s+long(?:\s+int)?\b)   # keep 64-bit
  | (?P<ld>\blong\s+double\b)                                      # keep
  | (?P<u>\bunsigned\s+long(?:\s+int)?\b)
  | (?P<s>\b(?:signed\s+)?long(?:\s+int)?\b)
""", re.X)

def rewrite(text):
    def rep(m):
        if m.group("ll") or m.group("ld"):
            return m.group(0)
        if m.group("u"):
            return "u32"
        return "s32"
    return PAT.sub(rep, text)

def main():
    dry = "--dry" in sys.argv
    total = files = 0
    for d in DIRS:
        for dp, _, fs in os.walk(os.path.join(ROOT, d)):
            for f in fs:
                if not f.endswith(EXTS):
                    continue
                p = os.path.join(dp, f)
                rel = os.path.relpath(p, ROOT)
                if rel in SKIP:
                    continue
                s = open(p, encoding="utf-8", errors="surrogateescape").read()
                n = rewrite(s)
                if n != s:
                    c = sum(1 for m in PAT.finditer(s) if m.group("u") or m.group("s"))
                    total += c
                    files += 1
                    if not dry:
                        open(p, "w", encoding="utf-8", errors="surrogateescape").write(n)
    print(f"{'would rewrite' if dry else 'rewrote'} {total} occurrences in {files} files")

if __name__ == "__main__":
    main()
