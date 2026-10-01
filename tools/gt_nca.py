class Nca:
    def __init__(self, mm, name, off, size, keys):
        self.name, self.off, self.size = name, off, size
        hk = keys["header_key"]
        self.raw = mm[off:off + NCA_HEADER_SIZE]
        self.hdr = xts_decrypt(self.raw, hk[0:16], hk[16:32])
        self.magic = self.hdr[0x200:0x204]
        if self.magic != b"NCA3":
            raise ValueError(f"{name}: bad magic {self.magic!r}")
        self.mm = mm

    def b(self, o, n=1):
        return self.hdr[o:o + n]

    def u8(self, o):
        return self.hdr[o]

    def u32(self, o):
        return struct.unpack_from("<I", self.hdr, o)[0]

    def u64(self, o):
        return struct.unpack_from("<Q", self.hdr, o)[0]

    @property
    def content_type(self):
        return self.u8(0x205)

    @property
    def key_generation(self):
        return self.u8(0x206)

    @property
    def kaek_index(self):
        return self.u8(0x207)

    @property
    def crypto_type(self):
        """0x208 = content size (u64). The lead read 0x209 as crypto_type; verify."""
        return self.u8(0x209)

    @property
    def content_size(self):
        return self.u64(0x208)

    @property
    def title_id(self):
        return self.u64(0x210)

    @property
    def rights_id(self):
        return self.hdr[0x230:0x240]

    def sections(self):
        out = []
        for i in range(4):
            start, end, unk, comp = struct.unpack_from("<IIII", self.hdr, 0x240 + i * 16)
            out.append(dict(index=i, start=start, end=end, unk=unk, comp=comp,
                            offset=start * BLOCK, size=(end - start) * BLOCK))
        return out

    def keyarea(self, base=0x280):
        return self.hdr[base:base + 0x80]


def ctr_decrypt(data, key, section_index, key_generation, media_offset, layout="ryu"):
    """Nintendo AES-128-CTR section crypto (symmetric). layout variants differ
    only in how the 16-byte counter base is built."""
    n = len(data) // 16
    if n == 0:
        return b""
    c0 = bytearray(16)
    if layout == "ryu":          # BE64(media_offset/0x200) | (sec|gen<<4) at byte 8
        c0[0:8] = struct.pack(">Q", media_offset // BLOCK)
        c0[8] = (section_index | (key_generation << 4)) & 0xFF
    elif layout == "ryu_bytes":  # BE64(media offset in BYTES)
        c0[0:8] = struct.pack(">Q", media_offset)
        c0[8] = (section_index | (key_generation << 4)) & 0xFF
    elif layout == "nemu":       # NEMU today: bytes + bare section index
        c0[0:8] = struct.pack(">Q", media_offset)
        c0[8] = section_index & 0xFF
    elif layout == "sector":     # BE64 offset at [8:16], byte 8 = sec|gen<<4
        c0[8] = (section_index | (key_generation << 4)) & 0xFF
        c0[8:16] = struct.pack(">Q", media_offset // BLOCK)
    else:
        raise ValueError(layout)
    c = int.from_bytes(c0, "big")
    ecb = AES.new(key, AES.MODE_ECB)
    out = bytearray()
    for i in range(n):
        ks = ecb.encrypt((c + i).to_bytes(16, "big"))
        out += bytes(x ^ y for x, y in zip(data[i * 16:(i + 1) * 16], ks))
    return bytes(out)


def check_pfs0(b):
    """STRICT oracle: PFS0 magic AND well-formed header AND plausible entries.
    Returns (names, reason) or (None, reason)."""
    if len(b) < 0x20:
        return None, "too short"
    if b[0:4] != b"PFS0":
        return None, "no PFS0 magic"
    nfiles, fsz, dsz = struct.unpack_from("<III", b, 0x10)
    if nfiles == 0:
        return None, "file_count == 0"
    if nfiles > 128:
        return None, f"file_count absurd ({nfiles})"
    if fsz < 0x18 + nfiles * 0x18:
        return None, f"file table size {fsz:#x} too small for {nfiles} entries"
    if dsz > len(b):
        return None, f"data size {dsz:#x} > section {len(b):#x}"
    names, p = [], 0x20
    try:
        for _ in range(nfiles):
            e = b.index(b"\0", p)
            names.append(b[p:e].decode("utf-8"))
            p = e + 1
    except (ValueError, UnicodeDecodeError):
        return None, "unparseable file names"
    return names, "OK"


def sections_of(mm, names, part="secure"):
    """Return {basename: (offset, size)} for one partition."""
    entries = parse_hfs0(mm, 0xF000)
    for name, off, size in entries:
        if name == part:
            return {n: (o, s) for n, o, s in parse_hfs0(mm, off)}
    raise KeyError(part)


def load_ncas(mm, keys, part="secure", want=None):
    files = sections_of(mm, None, part)
    out = []
    for name, (off, size) in files.items():
        if not name.endswith(".nca"):
            continue
        if want and want not in name:
            continue
        try:
            out.append(Nca(mm, name, off, size, keys))
        except ValueError:
            pass
    return out
#!/usr/bin/env python3
"""INDEPENDENT ground-truth NCA3 parser + key-derivation oracle (pycryptodome).

Shares NO code with the C++ loader, so it cannot be fooled by the same bug.

Usage:  python3 tools/gt_nca.py dump   [--n <substring>] [--part secure|update]
        python3 tools/gt_nca.py tik
        python3 tools/gt_nca.py derive [--n <substring>]
        python3 tools/gt_nca.py scan
"""
import mmap
import os
import struct
import sys

from Crypto.Cipher import AES

CART = os.environ.get("NEMU_CART", "games/Terraria.xci")
KEYS = os.environ.get("NEMU_KEYS", "games/prod.keys")

BLOCK = 0x200
NCA_HEADER_SIZE = 0xC00


def load_keys(path=KEYS):
    k = {}
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line or "=" not in line:
                continue
            name, _, val = line.partition("=")
            try:
                k[name.strip()] = bytes.fromhex(val.strip())
            except ValueError:
                k[name.strip()] = val.strip().encode()
    return k


def xts_decrypt(data, key1, key2, sector_start=0, sector_size=BLOCK):
    """Continuous XTS-AES decrypt, Switch NCA convention:
    tweak input = 8 zero bytes || BE64(sector_index), index increments across
    the WHOLE span (never reset per sector)."""
    out = bytearray(len(data))
    for s in range(len(data) // sector_size):
        tw = bytearray(16)
        tw[8:] = struct.pack(">Q", sector_start + s)
        tw = bytearray(AES.new(key2, AES.MODE_ECB).encrypt(bytes(tw)))
        off = s * sector_size
        for b in range(0, sector_size, 16):
            blk = bytes(a ^ c for a, c in zip(data[off + b:off + b + 16], tw))
            dec = AES.new(key1, AES.MODE_ECB).decrypt(blk)
            out[off + b:off + b + 16] = bytes(a ^ c for a, c in zip(dec, tw))
            carry = 0
            for i in range(16):
                nc = tw[i] >> 7
                tw[i] = ((tw[i] << 1) | carry) & 0xFF
                carry = nc
            if carry:
                tw[0] ^= 0x87
    return bytes(out)


def parse_hfs0(mm, base, stride=0x40):
    """Parse the XCI-style HFS0/PFS0 used by this cart.

    Layout here is NOT the classic inline-name HFS0: entries are a fixed 0x40
    bytes (offset u64, size u64, 0x20 hash, 0x10 name-slot) and ALL names live
    together in a string table that follows the entry array. Verified against
    the raw bytes at 0xF000: count=4, string_table_size=0xF0,
    0x10 + 4*0x40 + 0xF0 == 0x200 == header_size, and the four names
    'update','logo','normal','secure' appear back-to-back at 0xF110.
    """
    if mm[base:base + 4] != b"HFS0":
        raise ValueError(f"no HFS0 at {base:#x}: {mm[base:base+4]!r}")
    nfiles, strsz = struct.unpack_from("<II", mm, base + 4)
    entries, p = [], base + 0x10
    for _ in range(nfiles):
        off, size = struct.unpack_from("<QQ", mm, p)
        entries.append((off, size))
        p += stride
    # Header size is 0x10 + entries + string table; it is NOT always 0x200.
    # Outer XCI HFS0 -> 0x200; inner 'secure' HFS0 (10 files) -> 0x600.
    hsize = (p - base) + strsz
    # Names: split the string table on NULs, in entry order.
    tbl = mm[p:p + strsz]
    names = [s.decode("utf-8", "replace") for s in tbl.split(b"\0") if s]
    out = []
    for i, (off, size) in enumerate(entries):
        nm = names[i] if i < len(names) else f"entry{i}"
        out.append((nm, base + hsize + off, size))
    return out


def open_cart(path=CART):
    f = open(path, "rb")
    return f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

CT_NAME = {0: "Program", 1: "Meta", 2: "Control", 3: "Manual", 4: "Data", 5: "PublicData"}


def cmd_dump(args):
    f, mm = open_cart()
    keys = load_keys()
    part = "secure"
    want = None
    for i, a in enumerate(args):
        if a == "--part":
            part = args[i + 1]
        if a == "--n":
            want = args[i + 1]
    ncas = load_ncas(mm, keys, part, want)
    print(f"{len(ncas)} NCA(s) in partition '{part}'")
    for n in ncas:
        print("=" * 78)
        print(f"{n.name}  cart_off={n.off:#x} size={n.size} ({n.size/1e6:.1f} MB)")
        print(f"  magic={n.magic!r} content_type={n.content_type}({CT_NAME.get(n.content_type)}) "
              f"keygen={n.key_generation} kaek_index={n.kaek_index}")
        print(f"  0x208 content_size={n.content_size} ({n.content_size:#x})  "
              f"byte@0x209={n.crypto_type:#04x}  size_matches_file={n.content_size == n.size}")
        print(f"  title_id={n.title_id:#018x}  rights_id={n.rights_id.hex()}")
        for s in n.sections():
            if s["size"] == 0 and s["end"] == s["start"]:
                continue
            print(f"    sec{s['index']}: media {s['start']:#x}..{s['end']:#x} "
                  f"file {s['offset']:#x}..{s['offset']+s['size']:#x} size={s['size']:#x} "
                  f"unk={s['unk']:#x} comp={s['comp']:#x}")
        for base in (0x240, 0x280, 0x2C0, 0x300, 0x340, 0x380):
            kb = n.b(base, 0x40)
            z = all(c == 0 for c in kb)
            print(f"    @{base:#05x}: {'ALL ZERO' if z else kb[:0x30].hex()}")
        print(f"    @0x400..0x480: {n.b(0x400, 0x40).hex()}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd = sys.argv[1]
    {"dump": cmd_dump}[cmd](sys.argv[2:])


if __name__ == "__main__":
    main()
