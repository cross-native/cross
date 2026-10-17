#!/usr/bin/env python3
# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compares two relocatable objects (ELF, COFF, or Mach-O) for semantic
# equality: section contents, relocations, and symbols, ignoring string-table
# layout and section order. Used by the x86-64 assembler differential tests.
import json
import re
import subprocess
import sys

READOBJ, OBJDUMP = sys.argv[1], sys.argv[2]


def run(args):
    return subprocess.run(args, capture_output=True, text=True).stdout


def fmt(path):
    m = re.search(r"file format (\S+)", run([OBJDUMP, "-f", path]))
    return m.group(1) if m else "?"


def section_bytes(path, is_macho):
    args = [OBJDUMP, "--macho", "-s", path] if is_macho else [OBJDUMP, "-s", path]
    result, cur = {}, None
    for line in run(args).splitlines():
        m = re.match(r"Contents of section ([^:]+):", line)
        if m:
            cur = m.group(1).strip()
            mm = re.match(r"\(([^,]+),([^)]+)\)", cur)
            if mm:
                cur = mm.group(2)
            result[cur] = []
            continue
        d = re.match(r"\s*([0-9a-f]+)\s+((?:[0-9a-f]{2,8}\s+){1,4})", line)
        if cur is not None and d:
            result[cur].extend(d.group(2).split())
    return {k: "".join(v) for k, v in result.items()}


def elf_summary(path):
    doc = json.loads(run([READOBJ, "--elf-output-style=JSON", "-S", "-r", "--symbols", path]))[0]
    names = {0: ""}
    for e in doc["Sections"]:
        names[e["Section"]["Index"]] = e["Section"]["Name"]["Name"]
    data = section_bytes(path, False)
    sections = {}
    for e in doc["Sections"]:
        s = e["Section"]
        n = s["Name"]["Name"]
        if n in (".strtab", ".shstrtab", ".symtab") or n.startswith((".rela", ".rel.")):
            continue
        sections[n] = (s["Type"]["Name"],
                       tuple(sorted(f["Name"] for f in s.get("Flags", {}).get("Flags", []))),
                       "" if s["Type"]["Name"] == "SHT_NOBITS" else data.get(n, ""))
    symbols = {}
    for e in doc.get("Symbols", []):
        y = e["Symbol"]
        n = y["Name"]["Name"] if isinstance(y["Name"], dict) else y["Name"]
        if not n or y["Type"]["Name"] == "Section":
            continue
        sec = y.get("Section", {})
        sec = sec.get("Name", "?") if isinstance(sec, dict) else sec
        symbols[n] = (y["Binding"]["Name"], y["Type"]["Name"], sec, y.get("Value", 0), y.get("Size", 0))
    relocs = {}
    for e in doc.get("Relocations", []):
        items = []
        for r in e.get("Relocs", []):
            rr = r["Relocation"]
            sym = rr.get("Symbol", {})
            sym = sym.get("Name", "") if isinstance(sym, dict) else sym
            if isinstance(sym, dict):
                sym = sym.get("Name", "")
            a = rr.get("Addend", 0)
            items.append((rr["Offset"], rr["Type"]["Name"], sym, a - (1 << 64) if a >= 1 << 63 else a))
        relocs[names.get(e["SectionIndex"], "?")] = sorted(items)
    return {"sections": sections, "symbols": symbols, "relocs": relocs}


def generic_summary(path, is_macho):
    data = section_bytes(path, is_macho)
    sections = {k: v for k, v in data.items()}
    relocs, cur, pending = {}, None, {}
    for line in run([READOBJ, "-r", "--expand-relocs", path]).splitlines():
        m = re.search(r"Section \(\d+\) (\S+) \{", line)
        if m:
            cur = m.group(1)
            relocs.setdefault(cur, [])
        for key in ("Offset", "Type", "Symbol"):
            mm = re.search(key + r": (\S+)", line)
            if mm:
                pending[key] = mm.group(1)
        if line.strip() == "}" and "Offset" in pending and cur is not None:
            relocs[cur].append((pending.get("Offset"), pending.get("Type"), pending.get("Symbol", "")))
            pending = {}
    relocs = {k: sorted(v) for k, v in relocs.items()}
    symbols, name, attrs = {}, None, {}
    for line in run([READOBJ, "--symbols", path]).splitlines():
        if "Symbol {" in line:
            name, attrs = None, {}
        m = re.search(r"Name: (\S+) \(", line)
        if m and name is None:
            name = m.group(1)
        for key in ("Value", "Section", "StorageClass", "ComplexType"):
            mm = re.search(key + r": (\S+)", line)
            if mm:
                attrs[key] = mm.group(1)
        if line.strip() == "}" and name:
            symbols[name] = (attrs.get("Value"), attrs.get("Section"), attrs.get("StorageClass"),
                             attrs.get("ComplexType"))
            name = None
    return {"sections": sections, "symbols": symbols, "relocs": relocs}


def summary(path):
    f = fmt(path)
    return elf_summary(path) if "elf" in f else generic_summary(path, "mach-o" in f)


def main():
    a, b = summary(sys.argv[3]), summary(sys.argv[4])
    diffs = []
    for key in ("sections", "symbols", "relocs"):
        for name in sorted(set(a[key]) | set(b[key])):
            if a[key].get(name) != b[key].get(name):
                diffs.append(f"{key}:{name}\n  A={a[key].get(name)}\n  B={b[key].get(name)}")
    for d in diffs:
        print(d)
    sys.exit(1 if diffs else 0)


main()
