#!/usr/bin/env python3
# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compares two little-endian ELF64 relocatable objects: the header's machine
# and flags; each section's type, flags, alignment, entry size, and contents;
# each section's relocations; and the symbols as a multiset. Section and
# symbol order and string tables are left out, and so are the $d mapping
# symbols of sections without code, which GNU as leaves out and LLVM MC
# writes. With --relocations, only .text and the relocations count.
import struct
import sys

SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS = 2, 3, 4, 8
SHF_EXECINSTR = 4
STT_SECTION = 3


def string(data, offset):
    end = data.index(b'\0', offset)
    return data[offset:end].decode()


def summary(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        sys.exit('%s: not a little-endian ELF64 object' % path)
    machine, = struct.unpack_from('<H', data, 0x12)
    flags, = struct.unpack_from('<I', data, 0x30)
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shnum, shstrndx = struct.unpack_from('<HH', data, 0x3c)
    headers = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + 64 * index) for index in range(shnum)]
    strings = headers[shstrndx]
    names = [string(data, strings[4] + header[0]) for header in headers]
    sections, symbols, relocations = {}, [], {}
    symbol_names = []
    mapped = set()
    for index, (name, kind, sflags, _, offset, size, link, info, align, entsize) in enumerate(headers):
        if index == 0 or kind in (SHT_SYMTAB, SHT_STRTAB, SHT_RELA):
            continue
        contents = '' if kind == SHT_NOBITS else data[offset:offset + size].hex()
        sections[names[index]] = (kind, sflags, align, entsize, size, contents)
    for index, header in enumerate(headers):
        if header[1] != SHT_SYMTAB:
            continue
        table = headers[header[6]]
        for entry in range(header[5] // 24):
            name, info, other, shndx, value, size = struct.unpack_from(
                '<IBBHQQ', data, header[4] + 24 * entry)
            text = string(data, table[4] + name)
            where = names[shndx] if 0 < shndx < len(names) else str(shndx)
            if info & 15 == STT_SECTION:
                text = where
            symbol_names.append(text)
            if entry == 0 or info & 15 == STT_SECTION:
                continue
            if text == '$x':
                mapped.add(where)
            symbols.append((text, info >> 4, info & 15, other, where, value, size))
    for index, header in enumerate(headers):
        if header[1] != SHT_RELA:
            continue
        items = []
        for entry in range(header[5] // 24):
            offset, info, addend = struct.unpack_from('<QQq', data, header[4] + 24 * entry)
            items.append((offset, info & 0xffffffff, symbol_names[info >> 32], addend))
        relocations[names[header[7]]] = sorted(items)
    code = {names[index] for index, header in enumerate(headers) if header[2] & SHF_EXECINSTR}
    symbols = [symbol for symbol in symbols
               if symbol[0] != '$d' or symbol[4] in code or symbol[4] in mapped]
    return {'header': (machine, flags), 'sections': sections, 'relocations': relocations,
            'symbols': sorted(symbols)}


def main():
    arguments = sys.argv[1:]
    only = arguments[0] == '--relocations'
    if only:
        arguments = arguments[1:]
    first, second = summary(arguments[0]), summary(arguments[1])
    if only:
        for summary_ in (first, second):
            summary_['header'] = summary_['symbols'] = None
            summary_['sections'] = {'.text': summary_['sections'].get('.text', ())[-1:]}
    differences = []
    for key in ('header', 'symbols'):
        if first[key] != second[key]:
            differences.append('%s\n  A=%s\n  B=%s' % (key, first[key], second[key]))
    for key in ('sections', 'relocations'):
        for name in sorted(set(first[key]) | set(second[key])):
            if first[key].get(name) != second[key].get(name):
                differences.append('%s %s\n  A=%s\n  B=%s' % (key, name, first[key].get(name),
                                                             second[key].get(name)))
    for difference in differences:
        print(difference)
    sys.exit(1 if differences else 0)


main()
