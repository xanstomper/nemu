#!/usr/bin/env python3
"""Recover the NCA header key for a game card from prod.keys + the cart header.

A game card never ships title keys: the key area key (and therefore the NCA
header key) is derived from a master key plus the material in the 0xF000-byte
gamecard header. This sweeps the plausible derivations against the real NCA
headers and reports any hit by checking for the "NCA3"/"NCA2"/"NCA0" magic at
NCA header offset 0x200, which is a 1-in-2^32 oracle.

Usage: tools/nca_key_search.py <cart.xci> [max_pairs]
"""
import mmap
import os
import struct
import sys

from Crypto.Cipher import AES

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xci_probe import parse_hfs0  # noqa: E402

MAGICS = (b"NCA3", b"NCA2", b"NCA0")


def load_master_keys(path):
    keys = {}
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("master_key_"):
                name, value = line.split("=")
                try:
                    keys[int(name.strip()[-2:], 16)] = bytes.fromhex(value.strip())
                except ValueError:
                    pass
    return keys


def d(k, b):
    return AES.new(k, AES.MODE_ECB).decrypt(b)


def e(k, b):
    return AES.new(k, AES.MODE_ECB).encrypt(b)


def magic_probe(ct, k, ssz, io, big):
    """Return the first 4 bytes of plaintext at NCA header offset 0x200."""
    sec = 0x200 // ssz
    t = bytearray(16)
    x = sec
    for i in range(8):
        t[io + i] = ((x >> (8 * (7 - i))) & 0xFF) if big else ((x >> (8 * i)) & 0xFF)
    tw = e(k, bytes(t))
    cur = bytearray(tw)
    if ssz == 0x10:
        for b in range(33):
            if b == 32:
                blk = bytes(x ^ y for x, y in zip(ct, cur))
                return bytes(x ^ y for x, y in zip(d(k, blk), cur))[:4]
            carry = 0
            for i in range(16):
                nc = (cur[i] >> 7) & 1
                cur[i] = ((cur[i] << 1) | carry) & 0xFF
                carry = nc
            if carry:
                cur[0] ^= 0x87
        return b"\0\0\0\0"
    blk = bytes(x ^ y for x, y in zip(ct, tw))
    return bytes(x ^ y for x, y in zip(d(k, blk), tw))[:4]


def iter_ncas(mm):
    """Yield (name, abs_offset) for every .nca payload reachable from partitions."""
    node = parse_hfs0(mm, 0xF000)
    if not node:
        return
    base = node["base"] + node["header_size"]
    for part in node["entries"]:
        if part["name"] not in ("normal", "secure", "update"):
            continue
        sub = parse_hfs0(mm, base + part["off"])
        if not sub:
            continue
        sbase = sub["base"] + sub["header_size"]
        for f in sub["entries"]:
            if f["name"].endswith(".nca"):
                yield f["name"], sbase + f["off"]


def main():
    cart = sys.argv[1]
    max_pairs = int(sys.argv[2]) if len(sys.argv) > 2 else 256
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    mk = load_master_keys(os.path.join(root, "games", "prod.keys"))
    print(f"master keys: {len(mk)}")

    with open(cart, "rb") as fh:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        gh = mm[0:0x200]
        ncas = list(iter_ncas(mm))
        print(f"NCAs discovered: {len(ncas)}")
        if not ncas:
            print("no NCAs found")
            return 1

        # Use several independent NCAs so a single lucky hit cannot mislead us.
        samples = [(nm, mm[off + 0x200 : off + 0x210]) for nm, off in ncas[:4]]

        # Gamecard header application / encrypted-key-area-key windows, byte aligned.
        apps = [gh[o : o + 16] for o in range(0x00, 0xF1)]
        ekss = [gh[o : o + 16] for o in range(0x00, 0xF1)]

        tried = 0
        total = len(mk) * max_pairs * 3
        for rev, m in mk.items():
            for ai in range(max_pairs):
                app = apps[ai]
                tk = d(m, app)
                for kind in range(3):
                    if kind == 0:
                        cand = [d(tk, ekss[j]) for j in range(max_pairs)]
                    elif kind == 1:
                        cand = [d(m, ekss[j]) for j in range(max_pairs)]
                    else:
                        cand = [e(m, tk)] * max_pairs
                    for ci, k in enumerate(cand):
                        tried += 1
                        for ssz in (0x200, 0x10):
                            for io in (0, 8):
                                for big in (0, 1):
                                    ok = True
                                    for _nm, ct in samples:
                                        if magic_probe(ct, k, ssz, io, big) not in MAGICS:
                                            ok = False
                                            break
                                    if ok:
                                        print("HIT", hex(rev), ai, kind, ci, hex(ssz), io,
                                              "BE" if big else "LE", k.hex())
                                        return 0
            print(f"  master_key_{rev:02x} done ({tried}/{total})", flush=True)
        print(f"no key found ({tried} candidates)")
        return 1


if __name__ == "__main__":
    sys.exit(main())
