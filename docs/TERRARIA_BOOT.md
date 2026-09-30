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

## Correction: the keys were never the blocker

An earlier revision of this document claimed the NCA header key was not
derivable and that `Terraria.xci` might not be a valid dump. **That was wrong**,
and it came from a search that never tried the right input.

NEMU's own log shows the header decrypting correctly:

```
[NCA header decrypt: raw_magic=0x2C58589B after_XTS=0x3341434E]
```

`0x3341434E` is `"NCA3"`. The header key in `games/prod.keys` works, the title
ID comes back correct (`0x0100E46006708000`), and the section table parses to
self-consistent values (section 0 ends exactly at the NCA file size). The
earlier sweep failed because it only tried master keys and titlekek-derived
values and never `header_key` itself.

The actual failure is in NEMU's NCA layer, downstream of the header.

## Where the boot actually stops

Running the real binary:

```
$ ./build/bin/Nemu --run games/Terraria.xci --max-frames=2000
[Loader] Partition 'secure' contributed 10 payloads
[Loader] Partition 'update' contributed 213 payloads
[Crypto] NCA header decrypt: raw_magic=0x2C58589B after_XTS=0x3341434E
[Loader] Failed to parse ExeFS PFS0 archive      <-- repeats for every NCA
[Loader] No loadable payload found inside XCI cart
[System] Failed to load title from: games/Terraria.xci
```

Header decrypts; the **section body decrypts to garbage**, so the ExeFS PFS0
never parses and no title ever loads. The `.cnmt.nca` "failed" lines are
expected noise (Meta NCAs have no ExeFS).

### Confirmed bug: `Aes128::DecryptCtr` loses the counter nonce

`src/core/crypto/aes.cpp` read the starting counter from bytes `[8..15]` and
wrote each increment back into those same bytes. Two consequences:

1. Bytes `[0..7]` — which carry the NCA section nonce — were read as part of the
   counter on the first pass and then overwritten.
2. A carry out of the low half was **discarded**, so the counter stopped
   incrementing correctly past `0xFFFF...FF`.

Demonstrated against the pre-fix code:

```
low half = 0xFF..FF, upper = nonce
blk1 old: 71f7238b4675b7a70af75eea2fc5aa7b
blk1 new: b12048cb6b99cf4fc1f7f1f10812b018
carry case same: NO
```

The fix treats the counter as a full 128-bit big-endian value and increments
the whole block. Covered by new tests in `test_loader.cpp` (non-zero nonce
round-trip, distinct `block_offset` keystream, carry survival).

### Still open after the CTR fix

Header decrypts correctly and the section table is correct, but the ExeFS still
does not decrypt to a `PFS0` magic. A sweep of 32,000 combinations
(key-area offset across the whole header x key index 0-3 x Key-Area-Key source
x CTR layout x candidate media offsets) found no match, so the remaining gap is
in how the per-section key is derived, not in the container or the header.

Candidate causes still to check:

- `nca.cpp` reads the key area at header offset **0x300**, but 0x300 holds the
  *encrypted* Key-Area-Key. The encrypted section keys live elsewhere and the
  Key-Area-Key must first be unwrapped (`aes_key_generation_source` /
  `aes_kek_generation_source` are both present in `prod.keys`).
- `NcaReader::HEADER_SIZE` is `0x400`, but a real NCA3 header is `0xC00`; the
  segment region table at `0x400` is therefore never decrypted.
- `content_size` is read from `0x208` (actually `key_blob_index`), and
  `rights_id` from `0x230` (actually the meta-data hash). Both need `0x220`.

Decoded header for reference (`09dd2f0b…nca`, the 159 MB base-game NCA):

```
0x200 magic/dist/ct/keygen/kaek = NCA3, 1, 0(Program), 2, 0
0x210 program_id = 0x0100E46006708000
0x240 sec0: media 0x3CEA0..0x4F980  -> file 0x79D4000..0x9F30000
0x250 sec1: media 0x00E0..0x3CEA0  -> file 0x1C000..0x79D4000
0x260 sec2: media 0x0020..0x00E0  -> file 0x4000..0x1C000
```

Section 0 ends at `0x9F30000`, exactly the NCA's file size — confirming the
section table parses correctly and is not the problem.


