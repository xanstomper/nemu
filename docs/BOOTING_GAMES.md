# Booting Commercial Games (NCA / NSP / XCI)

Nemu's graphical, CPU, and loader subsystems can `Load` and **execute guest code**
from real Switch titles. This document explains exactly what is wired, how to
sideload a title onto the console, what cryptographic keys you must supply,
and the clean-room rules around them.

---

## 1. What is already implemented

The boot pipeline from a commercial title down to executable ARM64 code is in
place and verified by end-to-end automated tests:

```
Game file (.nca / .nsp / .xci)
   -->
Nsp / Xci unpack  (Pfs0Archive / Hfs0)
   -->
Program NCA identify  (NcaReader: NCA0/2/3 magic, content type)
   -->
Decrypt ExeFS section 0
      . Title Key path:  Rights ID (0x230) -> title.keys / prod.keys
      . Key Area path:   Key Area Key unwrap
   -->
ExeFS PFS0
   -->
Modular NSO modules loaded in real order: rtld, main, subsdk0..9, sdk
   -->
R_AARCH64_RELATIVE relocations applied (MOD0 / DT_RELA)
   -->
Recompiled & executed by the JIT (or interpreter)
```

Supported container inputs for `TitleLoader::LoadFromMemory`:

| Input | Path |
| :--- | :--- |
| Raw `.nso` | 3-segment, LZ4 or uncompressed |
| `.nro` | homebrew |
| **`.nca`** | Program -> ExeFS -> modules |
| **`.nsp`** | largest `.nca` -> Program -> ExeFS -> modules |
| **`.xci`** | `secure` HFS0 -> largest `.nca` -> ExeFS -> modules |
| `.nso` packaged in raw ExeFS PFS0 | direct |

**Module loading order** (`TitleLoader::LoadExeFS`):

1. `rtld` (the Switch dynamic loader) is loaded first, 64 KiB-aligned, and its
   entry point becomes the primary boot entry (as on real hardware).
2. `main`, then `subsdk0..9`, then `sdk`.
3. Any other file that carries NSO0 magic is appended in filesystem order.

**Relocations** (`NsoLoader::ApplyRelocations`): each module's rodata is scanned
for the `MOD0` marker, the ELF dynamic table (`DT_RELA`, `DT_RELASZ`,
`DT_RELAENT`) is located, and every `R_AARCH64_RELATIVE` entry (type 1027) is
patched to `base + addend`. This runs inside `NsoLoader::Load`, so absolute
pointers are correct before the module executes.

---

## 2. Sideloading a title

Place game files where the VFS can reach them. The loader is invoked from the
CLI with a path/`vfs:` target. Files can be dropped onto the SD card layout that
Nemu mounts at startup (`sdmc:/`, `save:/`).

Recommended layout on the Xbox / dev host working directory:

```
<workdir>/sdmc/<title>.nsp      or  <title>.xci  or  <title>.nca  or  <title>.nso
<workdir>/save/prod.keys            (keys, see below)
<workdir>/save/title.keys
```

Launch from the CLI:

```
Nemu sdmc:/MyGame.nsp
Nemu sdmc:/MyGame.nca
Nemu sdmc:/MyGame.nso
```

> **Diagnostics:** loading is verbose under `Loader` and `Cpu` log categories.
> If the title fails to decrypt, the loader will report exactly which key was
> missing. Export `NEMU_KEYS_PATH=/path/to/prod.keys` to point key loading at a
> non-default location.

---

## 3. Cryptographic keys (you must supply these yourself)

Nemu is **clean-room** and **does not bundle, ship, or auto-derive** any
Nintendo proprietary key. Retail games are AES-encrypted; the header is
XTS-encrypted with `header_key`, and section data is CTR-encrypted with either a
title key (retail) or a key-area-unwrapped key.

### 3.1 Key files

Keys live in standard text files:

| File | Looked up in |
| :--- | :--- |
| `prod.keys` | `NEMU_KEYS_PATH`, `./prod.keys`, `./keys/`, `./switch/`, `./save/keys/`, `~/.switch/`, `%USERPROFILE%/.switch/` |
| `title.keys` | same search locations |

`KeyStore::LoadDefaultKeys()` probes `NEMU_KEYS_PATH` first, then the fixed
relative and home-directory locations above.

### 3.2 Title keys vs key-area keys

- **`title.keys`** maps a 32-hex-char **Rights ID** to a 128-bit AES key.
  Used for retail (production) titles that carry a Rights ID in the NCA header
  (`GetTitleKey` accepts the bare hex, `title_key_<hex>`, or `titlekey_<hex>`).
- **`prod.keys`** provides the master Key Store (KAK master keys, header key,
  and key area keys). Used for development titles and the Key-Area unwrap path.

These files use the standard `name = hexkey` format already parsed by
`KeyStore::LoadFromText`.

> The keys are legal to use to play **your own** dumped/owned software. Nemu does
> not verify ownership, does not phone home, and does not contain any keys.

---

## 4. Honest limits on booting real games today

Loading and decrypting a Real ExeFS and producing executable, relocated modules
works. **Reaching gameplay** also requires:

1. **Verified commercial binaries on the host** — beyond the synthetic
   NCA/ExeFS used in unit tests, no real retail PAK has been run here because
   (a) no game files are available on this host and (b) on-console D3D12 needs
   real Xbox hardware.
2. **GPU shader translation** for display — Nemu rasterizes software-rendered
   guest frames today; full NVN -> D3D12 shader translation is a roadmap item.
3. **Horizon services** commercial games call at boot.
4. **On-console D3D12 validation** — the D3D12 pipeline compiles/links but must
   be validated on real Xbox hardware.

So "boot a real commercial game to menu" is **not yet reached**, but the
byte-loading/decryption/execution chain that a game needs is now wired and unit-
verified from `.nca`/`.nsp`/`.xci` down to relocated ARM64 entry.

---

## 5. Clean-room boundary

- Only **techniques** (container formats, crypto algorithm selection,
  relocation semantics, module loading order) are reimplemented here from public
  specs and reverse-engineering documentation — never GPLv3 source from Eden or
  yuzu is copied into Nemu.
- No Nintendo keys, firmware files, or copyrighted assets are committed.
- Everything exercised by CI is synthesized in-memory by the test suite; real
  game files and keys remain the user's own responsibility and are never needed
  to keep the build green.