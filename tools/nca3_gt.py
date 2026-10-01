#!/usr/bin/env python3
"""Independent NCA3 header ground-truth parser.

Reimplements AES-128-XTS (0x200 data unit) from scratch with pycryptodome so
results do not depend on any C++ loader code.
"""
import sys, binascii
from Crypto.Cipher import AES

BLOCK = 0x200
HDR = 0xC00

def xts_decrypt(data, key1, key2, unit=BLOCK):
    assert len(data) % unit == 0, (len(data), unit)
    out = bytearray(len(data))
    k2 = AES.new(key2, AES.MODE_ECB)
    for idx in range(len(data) // unit):
        tweak = bytearray(16)
        # Switch/Nintendo NCA convention: 64-bit sector index BIG-ENDIAN in the
        # UPPER 8 bytes of the tweak (matches NEMU's DecryptXts).
        tweak[8:16] = idx.to_bytes(8, 'big')
        t = bytearray(k2.encrypt(bytes(tweak)))
        base = idx * unit
        for j in range(unit // 16):
            blk = data[base + j*16 : base + j*16 + 16]
            x = bytes(a ^ b for a, b in zip(blk, t))
            p = AES.new(key1, AES.MODE_ECB).decrypt(x)
            out[base + j*16 : base + j*16 + 16] = bytes(a ^ b for a, b in zip(p, t))
            # multiply t by x in GF(2^128) (little-endian convention)
            carry = t[15] >> 7
            for i in range(15, 0, -1):
                t[i] = ((t[i] << 1) | (t[i-1] >> 7)) & 0xFF
            t[0] = (t[0] << 1) & 0xFF
            if carry:
                t[0] ^= 0x87
    return bytes(out)

def load_keys(path):
    keys = {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        k, v = line.split('=', 1)
        k, v = k.strip(), v.strip()
        try:
            keys[k] = binascii.unhexlify(v) if len(v) % 2 == 0 else None
        except Exception:
            keys[k] = None
    return keys

def decrypt_header(nca, header_key):
    return xts_decrypt(nca[:HDR], header_key[:16], header_key[16:32])

def hx(b):
    return ' '.join('%02x' % x for x in b)

def main():
    keys = load_keys('games/prod.keys')
    hk = keys['header_key']
    assert hk and len(hk) == 32, 'header_key missing'
    rom = open('games/Terraria.xci', 'rb').read()
    payloads = []
    for line in open('/tmp/payloads.txt'):
        if line.startswith('[') or ' ' not in line:
            continue
        try:
            name, off, size = line.split()
            payloads.append((name, int(off, 16), int(size, 16)))
        except ValueError:
            continue
    print('# loaded %d payloads' % len(payloads))

    which = sys.argv[1] if len(sys.argv) > 1 else 'all'
    for name, off, size in payloads:
        if not name.endswith('.nca'):
            continue
        nca = rom[off:off+size]
        h = decrypt_header(nca, hk)
        if h[0x200:0x204] != b'NCA3':
            continue
        if which != 'all' and which not in name:
            continue
        print('=' * 78)
        print('NAME %s  size=%d (0x%X)  catoff=0x%X' % (name, size, size, off))
        print('magic   0x200 = %s' % h[0x200:0x204])
        print('0x204: ver=%d mkr=%d content=%d crypto=%d keygen=%d kaek=%d 0x20a..0x20f=%s'
              % (h[0x204], h[0x205], h[0x206], h[0x207], h[0x208], h[0x209],
                 hx(h[0x20a:0x210])))
        cs = int.from_bytes(h[0x210:0x218], 'little')
        print('content_size @0x210 = 0x%X (%d)   [nca file size = 0x%X]' % (cs, cs, size))
        tid = int.from_bytes(h[0x218:0x220], 'little')
        print('title_id   @0x218 = 0x%016X' % tid)
        print('rights_id  @0x220 = %s' % hx(h[0x220:0x230]))
        print('0x230..0x240 = %s' % hx(h[0x230:0x240]))
        print('-- section entries @0x240 (4 x 0x10) --')
        for s in range(4):
            e = h[0x240 + s*0x10 : 0x240 + (s+1)*0x10]
            mo, me, fo, fe = [int.from_bytes(e[i*2:i*2+2], 'little') for i in range(4)]
            print('  sec%d raw=%s  media=0x%X..0x%X (0x%X..0x%X B)  file=0x%X..0x%X (0x%X..0x%X B)'
                  % (s, hx(e), mo, me, mo*BLOCK, me*BLOCK, fo, fe, fo*BLOCK, fe*BLOCK))
        print('-- 0x280 (file entry area) --')
        print('  %s' % hx(h[0x280:0x2C0]))
        print('  0x28C section/file count byte = %d' % h[0x28C])
        print('-- 0x2C0..0x300 --')
        print('  %s' % hx(h[0x2C0:0x300]))
        print('-- 0x300..0x400 (key area / encrypted KAK) --')
        for o in range(0x300, 0x400, 0x10):
            print('  0x%03X: %s' % (o, hx(h[o:o+0x10])))
        print('-- SEGMENT REGION TABLE 0x400..0xC00 --')
        for o in range(0x400, 0xC00, 0x10):
            print('  0x%03X: %s' % (o, hx(h[o:o+0x10])))

# ---------------------------------------------------------------------------
# Cross-NCA correlation: which header byte is the content type?
# ---------------------------------------------------------------------------
def correlate():
    keys = load_keys('games/prod.keys')
    hk = keys['header_key']
    rom = open('games/Terraria.xci', 'rb').read()
    rows = []
    for name, off, size in payloads_list():
        if not name.endswith('.nca'):
            continue
        nca = rom[off:off+size]
        h = decrypt_header(nca, hk)
        if h[0x200:0x204] != b'NCA3':
            rows.append((name, size, None))
            continue
        rows.append((name, size, h))
    good = [r for r in rows if r[2] is not None]
    print('# NCAs total=%d  NCA3-decrypted=%d' % (len(rows), len(good)))

    # For each header byte 0x204..0x20F, report the distinct values seen and
    # whether the value for .cnmt.nca files is constant (Meta => type 1).
    print('\n%-6s %-28s %-28s' % ('off', 'distinct values (cnt)', 'value for .cnmt.nca'))
    cnmt_vals = {}
    for o in range(0x204, 0x210):
        vals = {}
        for name, size, h in good:
            v = h[o]
            vals[v] = vals.get(v, 0) + 1
        top = sorted(vals.items(), key=lambda kv: -kv[1])[:6]
        s = ' '.join('%d:%d' % kv for kv in top)
        cset = set(h[o] for n, sz, h in good if n.endswith('.cnmt.nca'))
        print('0x%03X  %-28s %s' % (o, s, sorted(cset)))

    # Search every 8-byte LE window for the exact NCA file size (content size).
    print('\n# offsets where an 8-byte LE value equals the NCA file size')
    hits = {}
    for name, size, h in good:
        for o in range(0x200, 0xC00 - 8):
            if int.from_bytes(h[o:o+8], 'little') == size:
                hits.setdefault(o, []).append(name)
    for o, names in sorted(hits.items()):
        print('  0x%03X  matched %d/%d NCAs' % (o, len(names), len(good)))
    if not hits:
        print('  (none)')

    # Same for title id: correlate 8-byte window at 0x218 vs 0x210 etc.
    print('\n# cnmt NCAs (Meta expected) header bytes 0x200..0x240')
    for name, size, h in good:
        if name.endswith('.cnmt.nca'):
            print('  %-44s size=%-10d 0x204..0x20F=%s' % (name, size, hx(h[0x204:0x210])))

    print('\n# all NCAs: name / size / 0x204..0x20F')
    for name, size, h in sorted(good, key=lambda r: -r[1])[:24]:
        print('  %-44s %-11d %s' % (name, size, hx(h[0x204:0x210])))


def payloads_list():
    out = []
    for line in open('/tmp/payloads.txt'):
        if line.startswith('[') or ' ' not in line:
            continue
        try:
            n, o, s = line.split()
            out.append((n, int(o, 16), int(s, 16)))
        except ValueError:
            pass
    return out


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'correlate':
        correlate()
    else:
        main()
