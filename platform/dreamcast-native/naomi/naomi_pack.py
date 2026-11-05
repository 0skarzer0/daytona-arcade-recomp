#!/usr/bin/env python3
"""Append a /dimm archive to a NAOMI netboot image.

    platform/dreamcast-native/naomi/naomi_pack.py <in.bin from naomibintool> <folder> <out.bin>

Every file under <folder> goes into the archive with its path relative to
<folder> (e.g. music/orig/1.wav -> /dimm/music/orig/1.wav). The archive starts
at the end of the program segment rounded up to 64 KB, where dimmfs_init()
looks for it (naomi_dimmfs.c). The BIOS only copies the program
segment into RAM; the archive stays in the NetDIMM and is read on demand.
"""
import os
import struct
import sys

ALIGN = 0x10000
NAME_MAX = 56


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    src_bin, folder, out_bin = sys.argv[1:]

    image = bytearray(open(src_bin, "rb").read())
    if image[:5] != b"NAOMI":
        sys.exit(f"{src_bin}: not a NAOMI image")
    seg_offset, _addr, seg_length = struct.unpack_from("<III", image, 0x360)
    end = seg_offset + seg_length
    base = (end + ALIGN - 1) & ~(ALIGN - 1)
    if len(image) > base:
        sys.exit(f"{src_bin}: data after the program segment, not a plain naomibintool image")
    image += b"\0" * (base - len(image))

    files = []
    for root, _dirs, names in os.walk(folder):
        for n in sorted(names):
            full = os.path.join(root, n)
            rel = os.path.relpath(full, folder).replace(os.sep, "/")
            if len(rel.encode()) >= NAME_MAX:
                sys.exit(f"name too long for the archive: {rel}")
            files.append((rel, full))
    files.sort()

    header_size = 16 + len(files) * (NAME_MAX + 8)
    data = bytearray()
    entries = []
    offset = (header_size + 31) & ~31
    for rel, full in files:
        blob = open(full, "rb").read()
        entries.append((rel, offset, len(blob)))
        data += blob
        pad = (-(offset + len(blob))) % 32
        data += b"\0" * pad
        offset += len(blob) + pad

    archive = bytearray(b"CBDIMMFS" + struct.pack("<II", 1, len(files)))
    for rel, off, size in entries:
        archive += rel.encode().ljust(NAME_MAX, b"\0") + struct.pack("<II", off, size)
    archive += b"\0" * (((header_size + 31) & ~31) - len(archive))
    archive += data

    image += archive
    open(out_bin, "wb").write(image)
    print(f"{out_bin}: program {seg_length} bytes, archive at 0x{base:08x}, "
          f"{len(files)} files, {len(archive)} bytes, total {len(image)} bytes")
    if len(image) > 0x10000000:
        print("warning: larger than 256 MB, beyond what the cartridge port addresses")


if __name__ == "__main__":
    main()
