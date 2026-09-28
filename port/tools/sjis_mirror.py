#!/usr/bin/env python3
"""Mirror the game sources with Japanese string literals in Shift-JIS.

The Wii build feeds every source file through sjiswrap, so narrow string
literals such as "共通跳躍" are Shift-JIS bytes in the executable, matching
the names stored in the game data (effects, objects, sounds...). Clang
has no Shift-JIS execution charset, so this writes a copy of the source
trees in which the non-ASCII characters of narrow string and character
literals are replaced by octal escapes of their Shift-JIS encoding. Everything
else (comments, wide literals) is copied as is. Files are only rewritten when
their content changes, keeping incremental builds incremental.

    sjis_mirror.py <repo root> <output dir> <tree> [<tree> ...]
"""

import os
import sys

EXTENSIONS = (".c", ".cpp", ".cc", ".h", ".hpp", ".inc", ".mch")


def encode_char(ch, path, line):
    try:
        data = ch.encode("cp932")
    except UnicodeEncodeError:
        sys.exit(f"{path}:{line}: no Shift-JIS encoding for {ch!r}")
    return "".join(f"\\{b:03o}" for b in data)


def convert(text, path):
    out = []
    i = 0
    n = len(text)
    line = 1
    changed = False
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            out.append(c)
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(text[i:j])
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            out.append(text[i:j])
            i = j
        elif c == '"' or c == "'":
            # Literal prefix (L, u, U, u8, R...): leave those untouched.
            k = len(out) - 1
            prefix = ""
            while k >= 0 and len(out[k]) == 1 and (out[k].isalnum() or out[k] == "_"):
                prefix = out[k] + prefix
                k -= 1
            wide_or_raw = prefix in ("L", "u", "U", "u8") or prefix.endswith("R")
            if prefix.endswith("R") and c == '"':
                # Raw string: copy through the closing delimiter.
                open_paren = text.find("(", i)
                delim = text[i + 1 : open_paren]
                end = text.find(")" + delim + '"', open_paren)
                end = n if end < 0 else end + len(delim) + 2
                line += text.count("\n", i, end)
                out.append(text[i:end])
                i = end
                continue
            out.append(c)
            i += 1
            while i < n and text[i] != c:
                ch = text[i]
                if ch == "\\" and i + 1 < n:
                    out.append(text[i : i + 2])
                    i += 2
                    continue
                if ch == "\n":  # unterminated (e.g. an apostrophe in #error text)
                    break
                if ord(ch) > 127 and not wide_or_raw:
                    out.append(encode_char(ch, path, line))
                    changed = True
                else:
                    out.append(ch)
                i += 1
            if i < n and text[i] == c:
                out.append(c)
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out), changed


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    root, out_dir, trees = sys.argv[1], sys.argv[2], sys.argv[3:]
    written = 0
    for tree in trees:
        for dirpath, _, files in os.walk(os.path.join(root, tree)):
            for name in files:
                if not name.endswith(EXTENSIONS):
                    continue
                src = os.path.join(dirpath, name)
                rel = os.path.relpath(src, root)
                dst = os.path.join(out_dir, rel)
                try:
                    if os.path.getmtime(dst) >= os.path.getmtime(src):
                        continue
                except OSError:
                    pass
                with open(src, "rb") as f:
                    raw = f.read()
                try:
                    text = raw.decode("utf-8")
                except UnicodeDecodeError:
                    data = raw
                else:
                    converted, changed = convert(text, rel)
                    data = converted.encode("utf-8") if changed else raw
                try:
                    with open(dst, "rb") as f:
                        if f.read() == data:
                            os.utime(dst)  # up to date: skip it next time
                            continue
                except FileNotFoundError:
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as f:
                    f.write(data)
                written += 1
    if written:
        print(f"sjis_mirror: updated {written} file(s)")


if __name__ == "__main__":
    main()
