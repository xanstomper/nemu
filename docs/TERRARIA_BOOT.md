# Terraria boot: cart analysis and current blocker

Working notes for getting `games/Terraria.xci` to boot. Everything here is
reproducible with the two probes in `tools/`.

## Verify the claims in this file

```bash
python3 tools/xci_probe.py games/Terraria.xci        # independent parser
g++ -std=c++20 -O1 -I src tools/cart_probe.cpp -Lbuild/lib \
    -lNemu.Loader -lNemu.Crypto -lNemu.Platform -lNemu.FileSystem \
    -lNemu.Memory -lNemu.Cpu -lzstd -lpthread -o /tmp/cart_probe
/tmp/cart_probe games/Terraria.xci
```

## What the cart actually is

| Field | Value |
|---|---|
| Size | `0x77000000` (1,996,488,704 B), padded with `0xFF` past ~`0x24579000` |
| Partition table | HFS0 at **`0xF000`**, 4 entries, **entry stride `0x40`** |
| Data base | `0xF000` + header_size `0x200` = **`0xF200`** |
| Partitions | `update`, `logo`, `normal`, `secure` |
| Title ID | `0100e4600670800000000000000000008` |

Partition extents are perfectly contiguous, which is what confirms the parse:

```
update  off=0x0        size=0x16FF0000  -> container @0xF200      data @0x14A00   (213 NCAs)
logo    off=0x16FF0000 size=0x11C00    -> container @0x16FFF200  data @0x16FFF400 (2 files)
normal  off=0x17001C00 size=0x200      -> 512-byte stub, NOT a container
secure  off=0x17008E00 size=0xD560A00  -> container @0x17018000   data @0x17018600 (10 NCAs)
```

**`normal` is an empty stub.** The base game is in `secure` (159 MB romfs NCA
`09dd2f0b…`, 55 MB program NCA `e4277e8b…`) and `update` holds 213 more NCAs.
Reading only `normal` — which is what the loader used to do — yields nothing.

## Bugs found and fixed

1. **`pfs0.cpp` entry stride was hardcoded.** HFS0 does not encode the stride.
   This cart uses `0x40`; plain HFS0 uses `0x18`; HFS1 uses `0x38`. Now probed
   and validated against the string table and extents.
2. **`data_offset_base_` ignored the stored `header_size`.** Real containers pad
   the header to `0x200`, so every `OpenFile()` was misaligned. This cart
   stores garbage in that u32, so the aligned header end is the fallback.
3. **`xci.cpp` only read `normal`.** Now walks `normal`/`secure`/`update`.
4. **`xci.cpp` fallback scanned 384 MiB in 0x200 steps** over ciphertext.
   Now a bounded 4 MiB probe.
5. **`aes.cpp DecryptXts` dropped a trailing partial sector**, leaving the tail
   as ciphertext. Also removed a comment that wrongly claimed the NCA tweak
   layout matches IEEE 1619.

Net effect: **0 → 223 payloads** from this cart.

## The remaining blocker: NCA headers will not decrypt

All 221 NCA headers are AES-XTS encrypted — no plaintext `NCA3`/`NCA2`/`NCA0`
exists anywhere in the 1.9 GB file, and `magic@0x200` is garbage for every NCA.

This is **not** a NEMU bug. It is a keys/data problem:

- `title.keys` has only ~38 entries and **no key for `0100e46006708000`**.
- A game card has no title keys. Its key area key is derived from a master key
  plus the 0xF000-byte gamecard header.
- `tools/nca_key_search.py` swept **635,904 candidates** (all 23 master keys ×
  every 16-byte application/EKS window pair in the cart header × 3 derivation
  shapes × 4 XTS tweak layouts, checked against 4 independent NCAs).
  **No hit.**

So either the card needs key material that is not in public `prod.keys`, or
`Terraria.xci` is not a standard retail dump. Either way no amount of loader
work will get past it.

To confirm the XTS primitives themselves are correct (they are), see the
known-answer vector in `tests/unit/loader/test_loader.cpp`, generated
independently with pycryptodome.

## What "boots" would still require

Decrypting the header is only step 2 of a long chain:

1. NCA header XTS decrypt — **blocked on keys**
2. Segment region table parse
3. Section 0/2 AES-CTR via the key area key
4. `NSO0` detection in section 2
5. NPDM + NSO segment load
6. ARM64 execution from the entry point
7. HLE services the title actually calls (nns, loader, fs, hid, nifm, audren…)
8. NVN graphics + shaders for a MonoGame/XNA title

Step 8 is the long pole: no real game has ever run in NEMU, and Terraria is a
custom-rendered XNA/MonoGame title, so the GPU path would have to work on the
first try.

