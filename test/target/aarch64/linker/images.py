#!/usr/bin/env python3
# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Checks of little-endian ELF64 AArch64 images.
#
#   images.py compare A B [SECTION...]
#       The allocated sections (or the named ones) of A and B have the same
#       names, types, flags, addresses, sizes, and contents.
#   images.py headers IMAGE
#       Prints the ELF type, the entry point, and the program headers.
#   images.py veneers IMAGE
#       Prints the veneers of GNU ld and Cross ld: name, form (adrp or long),
#       and target, and where each section's veneers start.
#   images.py word IMAGE SYMBOL OFFSET
#       Prints the 32-bit word at SYMBOL + OFFSET.
#   images.py branches IMAGE BEGIN END [BEGIN END...]
#       Prints each B and BL between the symbols BEGIN and END with the
#       address it reaches, following range-extension veneers of GNU ld,
#       ld.lld, and Cross ld.
import struct
import sys

SHT_NOBITS = 8
SHF_ALLOC = 2


class Image:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        data = self.data
        if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
            sys.exit('%s: not a little-endian ELF64 file' % path)
        shoff, = struct.unpack_from('<Q', data, 0x28)
        shnum, shstrndx = struct.unpack_from('<HH', data, 0x3c)
        headers = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + 64 * index) for index in range(shnum)]
        names = headers[shstrndx]

        def string(table, offset):
            start = table[4] + offset
            return data[start:data.index(b'\0', start)].decode()

        self.sections = []
        for header in headers[1:]:
            name, kind, flags, address, offset, size = header[:6]
            self.sections.append({'name': string(names, name), 'type': kind, 'flags': flags,
                                  'address': address, 'size': size,
                                  'contents': b'' if kind == SHT_NOBITS else data[offset:offset + size]})
        phoff, = struct.unpack_from('<Q', data, 0x20)
        phnum, = struct.unpack_from('<H', data, 0x38)
        self.segments = [struct.unpack_from('<IIQQQQQQ', data, phoff + 56 * index) for index in range(phnum)]
        self.entry, = struct.unpack_from('<Q', data, 0x18)
        self.type, = struct.unpack_from('<H', data, 0x10)
        self.symbols = {}
        # The veneers of GNU ld and Cross ld, and the thunks of ld.lld.
        self.veneers = set()
        for header in headers:
            if header[1] != 2:
                continue
            table = headers[header[6]]
            for entry in range(header[5] // 24):
                name, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', data, header[4] + 24 * entry)
                if not name:
                    continue
                text = string(table, name)
                self.symbols.setdefault(text, value)
                if (text.startswith('__') and text.endswith('_veneer')) or text.startswith('__AArch64'):
                    self.veneers.add(value)

    def word(self, address):
        for section in self.sections:
            if section['contents'] and section['address'] <= address < section['address'] + section['size'] - 3:
                return struct.unpack_from('<I', section['contents'], address - section['address'])[0]
        return None

    def xword(self, address):
        low, high = self.word(address), self.word(address + 4)
        return None if low is None or high is None else low | high << 32


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def veneer(image, address):
    """The address a veneer at ADDRESS branches to, or None."""
    words = [image.word(address + 4 * index) for index in range(4)]
    if None in words[:3]:
        return None
    first, second, third, fourth = words
    if first & 0x9f00001f == 0x90000010 and second & 0xffc003ff == 0x91000210 and third == 0xd61f0200:
        immediate = ((first >> 29) & 3) | ((first >> 5) & 0x7ffff) << 2
        page = (address & ~0xfff) + (signed(immediate, 21) << 12)
        return (page + ((second >> 10) & 0xfff)) & (2 ** 64 - 1)
    if first == 0x58000090 and second == 0x10000011 and third == 0x8b110210 and fourth == 0xd61f0200:
        return (image.xword(address + 16) + address + 4) & (2 ** 64 - 1)
    if first == 0x58000050 and second == 0xd61f0200:
        return image.xword(address + 8)
    if first & 0xfc000000 == 0x14000000:
        return (address + 4 * signed(first & 0x3ffffff, 26)) & (2 ** 64 - 1)
    return None


def veneers(image):
    """GNU ld's and Cross ld's veneers: each one's name, form, and target,
    and where each output section's veneers start."""
    lines, starts = [], {}
    for name, address in image.symbols.items():
        if not (name.startswith('__') and name.endswith('_veneer')):
            continue
        first = image.word(address)
        form = 'adrp' if first & 0x9f00001f == 0x90000010 else 'long' if first == 0x58000090 else 'other'
        lines.append('%s %s %#x' % (name, form, veneer(image, address)))
        for section in image.sections:
            if section['address'] <= address < section['address'] + section['size']:
                starts[section['name']] = min(starts.get(section['name'], address), address)
    for line in sorted(lines):
        print(line)
    for name in sorted(starts):
        print('stubs %s %#x' % (name, starts[name]))


def branches(image, ranges):
    for begin, end in ranges:
        start, stop = image.symbols[begin], image.symbols[end]
        for address in range(start, stop, 4):
            word = image.word(address)
            if word is None or word & 0x7c000000 != 0x14000000:
                continue
            kind = 'bl' if word & 0x80000000 else 'b'
            target = (address + 4 * signed(word & 0x3ffffff, 26)) & (2 ** 64 - 1)
            while target in image.veneers:
                following = veneer(image, target)
                if following is None:
                    break
                target = following
            print('%s+%#x %s %#x' % (begin, address - start, kind, target))


def compare(first, second, names):
    def chosen(image):
        return {section['name']: section for section in image.sections
                if (section['name'] in names if names else section['flags'] & SHF_ALLOC)}
    a, b = chosen(first), chosen(second)
    failed = False
    for name in sorted(set(a) | set(b)):
        if name not in a or name not in b:
            print('%s: only in %s' % (name, first.path if name in a else second.path))
            failed = True
            continue
        for key in ('type', 'flags', 'address', 'size', 'contents'):
            if a[name][key] != b[name][key]:
                left, right = a[name][key], b[name][key]
                if key == 'contents':
                    left, right = left.hex(), right.hex()
                print('%s: %s differs\n  %s\n  %s' % (name, key, left, right))
                failed = True
    return not failed


def main():
    arguments = sys.argv[1:]
    if arguments[0] == 'compare':
        sys.exit(0 if compare(Image(arguments[1]), Image(arguments[2]), arguments[3:]) else 1)
    if arguments[0] == 'headers':
        image = Image(arguments[1])
        print('type %d entry %#x' % (image.type, image.entry))
        for kind, flags, offset, virtual, physical, file_size, memory_size, alignment in image.segments:
            print('%#x %#x %#x %#x %#x %#x %#x %#x' % (kind, flags, offset, virtual, physical, file_size,
                                                        memory_size, alignment))
        return
    if arguments[0] == 'veneers':
        veneers(Image(arguments[1]))
        return
    if arguments[0] == 'word':
        image = Image(arguments[1])
        print('%08x' % image.word(image.symbols[arguments[2]] + int(arguments[3], 0)))
        return
    if arguments[0] == 'branches':
        rest = arguments[2:]
        branches(Image(arguments[1]), list(zip(rest[::2], rest[1::2])))
        return
    sys.exit('unknown command ' + arguments[0])


if __name__ == '__main__':
    main()
