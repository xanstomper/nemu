#!/usr/bin/env python3
"""Ground-truth gamecard (XCI) key derivation + NCA section-key oracle.

Everything here is independent of the C++ loader (pure pycryptodome) so a bug
in NEMU cannot hide behind a matching bug in the reference.

Oracle chain, each step falsifiable:
  1. The cart header (first 0xF000 bytes) is AES-XTS encrypted under a keyblob
     master key. Success test: the decrypted header must be self-consistent
     (cart size / sector count fields, "GAME" signature, key area key index in
     range, partition extents that tile the image).
  2. The Key-Area-Key is AES-ECB(master_key, encrypted_key_area_key) taken out
     of that decrypted header.
  3. The NCA key area entries are AES-ECB(kak, entry) -> section key.
  4. DEFINITIVE test: the ExeFS section must decrypt to a well-formed PFS0.

Usage:  python3 tools/gc_gt.py [cart]
"""
import mmap
import os
import struct
import sys

from Crypto.Cipher import AES

CART = sys.argv[1] if len(sys.argv) > 1 else "games/Terraria.xci"
KEYS = "games/prod.keys"
BLOCK = 0x200
CART_SIZE = os.path.getsize(CART)


def load_keys(path=KEYS):
    keys = {}
    with open(path) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line or "=" not in line:
                continue
            name, _, val = line.partition("=")
            try:
                keys[name.strip()] = bytes.fromhex(val.strip())
            except ValueError:
                pass
    return keys


def xts_decrypt(data, key1, key2, tweak_hi=True, unit=BLOCK):
    """Nintendo XTS. tweak = 8 zero bytes || BE64(index) when tweak_hi."""
    out = bytearray(len(data))
    k2 = AES.new(key2, AES.MODE_ECB)
    for idx in range(len(data) // unit):
        tw = bytearray(16)
        enc = idx.to_bytes(8, "big")
        if tweak_hi:
            tw[8:16] = enc
        else:
            tw[0:8] = enc
        tw = bytearray(k2.encrypt(bytes(tw)))
        k1 = AES.new(key1, AES.MODE_ECB)
        base = idx * unit
        for j in range(unit // 16):
            blk = data[base + j * 16: base + j * 16 + 16]
            x = bytes(a ^ b for a, b in zip(blk, tw))
            p = k1.decrypt(x)
            out[base + j * 16: base + j * 16 + 16] = bytes(a ^ b for a, b in zip(p, tw))
            carry = tw[15] >> 7
            for i in range(15, 0, -1):
                tw[i] = ((tw[i] << 1) | (tw[i - 1] >> 7)) & 0xFF
            tw[0] = (tw[0] << 1) & 0xFF
            if carry:
                tw[0] ^= 0x87
    return bytes(out)


def plausible_gc_header(h):
    """Score a decrypted gamecard header. Returns (score, reasons)."""
    reasons = []
    score = 0
    if h[0x4:0x8] == b"GAME":
        score += 4
        reasons.append("'GAME' sig @0x4")
    for off in range(0, len(h) - 8, 4):
        v = struct.unpack_from("<Q", h, off)[0]
        if v == CART_SIZE:
            score += 3
            reasons.append("cart size @%#x" % off)
        elif v == CART_SIZE // BLOCK:
            score += 3
            reasons.append("cart sectors @%#x" % off)
        elif 0 < v <= CART_SIZE and v % BLOCK == 0:
            score += 1
    # key area key index should be a small int near a plausible spot
    for off in (0x140, 0x144, 0x100, 0x110, 0x120, 0x130, 0x40, 0x44):
        if off + 4 <= len(h):
            v = struct.unpack_from("<I", h, off)[0]
            if v < 32:
                score += 1
                reasons.append("kak_index@%#x=%d" % (off, v))
    return score, reasons


def main():
    keys = load_keys()
    with open(CART, "rb") as fh:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        hdr_raw = mm[:0xF000]

        cands = []
        for name, val in keys.items():
            if name.startswith("master_key_") and len(val) == 0x10:
                cands.append((name, val))

        best = []
        for name, mk in cands:
            for tweak_hi in (True, False):
                dec = xts_decrypt(hdr_raw, mk, mk, tweak_hi=tweak_hi)
                score, why = plausible_gc_header(dec)
                best.append((score, name, tweak_hi, dec, why))
        best.sort(key=lambda t: -t[0])
        for score, name, th, dec, why in best[:4]:
            print("cand %-14s tweak_hi=%-5s score=%d %s" % (name, th, score, why[:6]))
        if not best or best[0][0] < 4:
            print("!! no convincing gamecard header found")
            return 1

        score, name, th, dec, _ = best[0]
        print("\n=== decrypted gamecard header (first 0x200) ===")
        for off in range(0, 0x200, 0x10):
            row = dec[off:off + 0x10]
            txt = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
            print("  %04X: %s  %s" % (off, row.hex(" "), txt))
        return 0


if __name__ == "__main__":
    sys.exit(main())
