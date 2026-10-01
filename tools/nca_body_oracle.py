#!/usr/bin/env python3
"""Ground-truth NCA section decryption oracle for NEMU.

Reproduces hactool's ExeFS decrypt for games/Terraria.xci with pure pycryptodome,
independent of NEMU's C++ loader. This is the definitive reference for what
`src/core/loader/nca.cpp` must produce.

VERIFIED FINDINGS (2026-09-30, against the base-game NCA 09dd2f0b…):
  1. Header byte 0x206 (read by NEMU as `key_generation_`) = 2, but the NCA's
     effective KAK generation is **7** (hactool: "Master Key Revision 0x7").
     The wrong KAK (key_area_key_application_02) unwraps key-area slot 2 to
     097323a0… ; the correct one (key_area_key_application_07) yields
     **a55c8f182443fe589954b4314798d1ae**.
  2. hactool derives the KAK as keyset.key_area_keys[crypto_type][kaek_ind]
     where crypto_type = effective revision (7), kaek_ind = 0.
  3. Section 0 (ExeFS) is AES-CTR. With section key a55c8f18… and CTR base
     171a6a93888d55341693cbacedae15a5 the first block decrypts to PFS0 magic
     5046533005000000… . NEMU's current offset-based CTR does NOT match.
  4. The stored section_ctr at header 0x280 and the plain (real_off>>4) scheme
     do NOT reproduce that CTR base; the exact base derivation is the remaining
     blocker (see TERRARIA_BOOT.md).

Usage:
    python3 tools/nca_body_oracle.py   # runs the full oracle on Terraria.xci
"""
import os
import struct
import sys

from Crypto.Cipher import AES

CART = sys.argv[1] if len(sys.argv) > 1 else "games/Terraria.xci"
KEYS = "games/prod.keys.clean" if os.path.exists("games/prod.keys.clean") else "games/prod.keys"
BLOCK = 0x200
HEADER = 0xC00


def load_keys(path):
    k = {}
    for line in open(path):
        line = line.split("#")[0].strip()
        if "=" in line:
            n, _, v = line.partition("=")
            try:
                k[n.strip()] = bytes.fromhex(v.strip())
            except ValueError:
                pass
    return k


def xts_decrypt(data, key1, key2, tweak_hi=True, unit=BLOCK):
    out = bytearray(len(data))
    k2 = AES.new(key2, AES.MODE_ECB)
    for idx in range(len(data) // unit):
        tw = bytearray(16)
        if tweak_hi:
            tw[8:16] = idx.to_bytes(8, "big")
        else:
            tw[0:8] = idx.to_bytes(8, "big")
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


def parse_container(data, base_abs):
    """Parse an HFS0/PFS0 container at data[base_abs:]. Returns list of
    {name, offset (rel to container data base), size, data_start_abs}."""
    if data[base_abs:base_abs + 4] not in (b"HFS0", b"PFS0"):
        return []
    cnt, strtab = struct.unpack_from("<II", data, base_abs + 4)
    stride = 0x40  # this cart (Terraria) uses 0x40
    entries_off = base_abs + 0x10
    strtab_base = entries_off + cnt * stride
    files = []
    for i in range(cnt):
        e = entries_off + i * stride
        f_off, f_size, f_str = struct.unpack_from("<QQI", data, e)
        nl = data.index(b"\x00", strtab_base + f_str)
        name = data[strtab_base + f_str: nl].decode("ascii", "replace")
        files.append({"name": name, "offset": f_off, "size": f_size})
    header_size = 0x10 + cnt * stride + strtab
    for f in files:
        f["data_start_abs"] = base_abs + header_size + f["offset"]
    return files


def main():
    K = load_keys(KEYS)
    if "header_key" not in K:
        print("no header_key in", KEYS)
        return 1

    rom = open(CART, "rb").read()
    tlb = parse_container(rom, 0xF000)
    secure = next((p for p in tlb if p["name"] == "secure"), None)
    if not secure:
        print("no 'secure' partition")
        return 1
    sec_files = parse_container(rom, secure["data_start_abs"])
    target = None
    for f in sec_files:
        if f["name"].endswith(".nca") and "cnmt" not in f["name"] and f["size"] > 0x100000:
            if not target or f["size"] > len(target["data"]):
                target = {"name": f["name"],
                          "data": rom[f["data_start_abs"]: f["data_start_abs"] + f["size"]],
                          "off": f["data_start_abs"]}
    if not target:
        print("no program NCA found in secure")
        return 1
    print(f"target NCA: {target['name']} ({len(target['data'])} bytes) @0x{target['off']:x}")

    hk = K["header_key"]
    k1, k2 = hk[:16], hk[16:]
    hd = xts_decrypt(target["data"][:HEADER], k1, k2)
    if hd[0x200:0x204] != b"NCA3":
        print("header did not decrypt to NCA3")
        return 1
    print("header OK: NCA3")

    media_start = struct.unpack_from("<I", hd, 0x240)[0]
    real_off = 0x200 * media_start
    enc_key2 = hd[0x300 + 2 * 16: 0x300 + 2 * 16 + 16]
    print(f"sec0 media 0x{media_start:x} real file off 0x{real_off:x}  enc key2 {enc_key2.hex()}")

    # KAK generation sweep -> correct section key.
    target_sk = bytes.fromhex("a55c8f182443fe589954b4314798d1ae")
    sk = None
    for gen in range(0x10):
        n = f"key_area_key_application_{gen:02x}"
        if n not in K:
            continue
        d = AES.new(K[n], AES.MODE_ECB).decrypt(enc_key2)
        if d == target_sk:
            print(f"  KAK gen {gen:02x} ({n}) -> CORRECT section key {d.hex()}")
            sk = d
            break
    if sk is None:
        print("  no KAK generation produced the known section key a55c8f18…; keyset incomplete")
        return 1

    # Verify the full first ExeFS sector decrypts to PFS0 with the known CTR base.
    ECB = AES.new(sk, AES.MODE_ECB)
    base = int.from_bytes(bytes.fromhex("171a6a93888d55341693cbacedae15a5"), "big")
    out = bytearray()
    for blk in range(0x200 // 16):
        ks = ECB.encrypt((base + blk).to_bytes(16, "big"))
        seg = target["data"][real_off + blk * 16: real_off + blk * 16 + 16]
        out[blk * 16: blk * 16 + 16] = bytes(a ^ b for a, b in zip(seg, ks))
    ok = out[:4] == b"PFS0"
    print("  first 0x200 of ExeFS:", "PFS0 OK" if ok else out[:8].hex())
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())