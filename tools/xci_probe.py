#!/usr/bin/env python3
"""Ground-truth XCI/NCA structure analyzer for the NEMU cart format work.

Independent of the C++ loader so we can diff its view against reality.
Usage: tools/xci_probe.py <cart> [--keys dir]
"""
import mmap
import os
import re
import struct
import sys

MAGICS = [b"HFS0", b"PFS0", b"HS00", b"HEAD", b"NCA0", b"NCA2", b"NCA3", b"NCZS", b"NSZ0"]


def scan_magics(mm):
    """Return {magic: [offsets...]} across the whole image."""
    out = {}
    size = len(mm)
    for m in MAGICS:
        hits = []
        pos = 0
        while pos < size:
            i = mm.find(m, pos)
            if i < 0:
                break
            hits.append(i)
            pos = i + 1
            if len(hits) > 200:
                break
        out[m.decode()] = hits
    return out


def parse_hfs0(mm, base, label="", esz_override=None):
    """Parse an HFS0/PFS0 header at absolute `base`.

    `esz` is the file-entry stride. Real carts use 0x18 (plain HFS0) or
    0x40 (PFS1-style entries carrying a per-file hash). We auto-detect.
    """
    hdr = mm[base : base + 0x10]
    if len(hdr) < 0x10:
        return None
    magic = hdr[0:4]
    cnt, strsz, res = struct.unpack_from("<III", hdr, 4)
    if magic not in (b"HFS0", b"PFS0") or cnt == 0 or cnt > 4096 or strsz > 0x10000:
        return None

    candidates = [esz_override] if esz_override else [0x18, 0x40]
    entries = None
    esz = None
    for cand in candidates:
        st = 0x10 + cnt * cand
        if st + strsz + 4 > len(mm) - base:
            continue
        e = []
        ok = True
        for i in range(cnt):
            o = base + 0x10 + cand * i
            off, size = struct.unpack_from("<QQ", mm, o)
            no, hs = struct.unpack_from("<II", mm, o + 16)
            if no >= strsz or size > len(mm) - base or off > len(mm) - base:
                ok = False
                break
            e.append((off, size, no, hs))
        if not ok:
            continue
        # Names must be printable ASCII in the strtab -> validates the stride.
        st_abs = base + st
        strtab = mm[st_abs : st_abs + strsz]
        if not all(32 <= b < 127 for (_, _, no, _) in e for b in [strtab[no]]):
            continue
        entries = e
        esz = cand
        break
    if entries is None:
        return None

    st = 0x10 + cnt * esz
    strtab = mm[base + st : base + st + strsz]
    names = []
    for (off, size, no, hs) in entries:
        end = strtab.find(b"\0", no)
        names.append(strtab[no:end].decode("ascii", "replace"))
    # header_size u32 right after the string table: file data begins there.
    # Some carts (incl. this one) have no usable value there because the data
    # region starts immediately after the aligned header, so fall back to the
    # canonical align(header_end, 0x200).
    hs_off = base + st + strsz
    raw_hs = struct.unpack_from("<I", mm, hs_off)[0] if hs_off + 4 <= len(mm) else 0
    default_hs = (st + strsz + 0x1FF) & ~0x1FF
    sane = (st + strsz + 4) <= raw_hs <= 0x20000 and raw_hs % 0x200 == 0
    header_size = raw_hs if sane else default_hs
    return {
        "base": base,
        "magic": magic.decode(),
        "count": cnt,
        "esz": esz,
        "strtab": strsz,
        "strtab_abs": base + st,
        "header_size": header_size,
        "entries": [
            {"name": n, "off": o, "size": s, "hashed": h}
            for (o, s, no, h), n in zip(entries, names)
        ],
    }


def dump_node(mm, base, depth=0, max_depth=3, esz_override=None, label=""):
    ind = "  " * depth
    node = parse_hfs0(mm, base, esz_override=esz_override)
    if not node:
        return None
    print(
        f"{ind}{label}{node['magic']}@0x{base:x} count={node['count']} "
        f"esz={node['esz']:#x} strtab={node['strtab']:#x} hdr_size={node['header_size']:#x}"
    )
    for e in node["entries"]:
        data_abs = base + node["header_size"] + e["off"]
        print(
            f"{ind}  {e['name']:<24} off={e['off']:<12x} size={e['size']:<12x} "
            f"hashed={e['hashed']:<10x} -> abs=0x{data_abs:x}"
        )
        if depth < max_depth and e["size"] > 0x10:
            # A nested container header may sit at the entry origin (base+off,
            # this is how XCI partitions nest) or at the data origin.
            for cand in (base + e["off"], data_abs):
                if cand == base:
                    continue
                sub = parse_hfs0(mm, cand, esz_override=esz_override)
                if sub:
                    dump_node(mm, cand, depth + 1, max_depth, esz_override,
                              label=f"[{e['name']}] ")
                    break
            else:
                if e["name"].endswith((".nca", ".cnmt", ".nsp", ".tik", ".cert")):
                    head = mm[data_abs : data_abs + 0x20]
                    print(f"{ind}    (raw) {head.hex(' ')}")
    return node


def main():
    cart = sys.argv[1]
    size = os.path.getsize(cart)
    print(f"cart={cart} size={size} ({size:#x})")
    with open(cart, "rb") as fh:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        print("\n=== magic map (first 200 each) ===")
        mh = scan_magics(mm)
        for k, v in mh.items():
            shown = ", ".join(hex(x) for x in v[:12])
            more = f" (+{len(v)-12} more)" if len(v) > 12 else ""
            print(f"  {k:<5} n={len(v):<4} {shown}{more}")

        print("\n=== XCI partition table descent ===")
        for pt in (0xF000, 0x200, 0x0):
            node = parse_hfs0(mm, pt)
            if node and {e["name"] for e in node["entries"]} & {"normal", "secure", "update", "logo"}:
                print(f"  --> found partition table at {pt:#x}")
                dump_node(mm, pt, 0, 2, label="ROOT ")
                break
        else:
            print("  no XCI partition table found")
        mm.close()


if __name__ == "__main__":
    main()
