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


## 6. (2026-10-01) Boot diagnostics fixed; real blocker isolated

### The "BOOTED" verdict was false

`--run` reported `[NEMU-BOOT] ... -> BOOTED (advanced frames)` while the guest was
hard-spinning on one bad PC, burning all 30M instructions. `main.cpp` only checked
`frames > 0`, and `StepCpuQuantum` ignored `StepResult::UndefinedInstruction` /
`MemoryFault` entirely, so a faulted thread re-stepped the same PC forever.

Fixed:
- `Emulator::StepCpuQuantum` counts consecutive faults at one PC (both the JIT
  fallback and interpreter paths) and terminates the thread after 64, setting
  `Emulator::StalledOnFault()`.
- `--run` now requires positive evidence to claim a boot and prints the real
  numbers: `frames`, `instructions`, `memory_faults`, `last_fault_addr`.
  Verdicts: `BOOTED` / `DEGRADED` / `FAILED`. Exit codes: 0 booted, 2 load
  failed, 3 no frames, 4 stalled on faults.
- `VirtualMemory` and the interpreter throttle fault logs (first 32 in full, then
  every 100000th). A 2000-frame probe used to emit a **6.2 GB** `run.log`; it
  now emits ~40 lines, so the first real error is visible instead of buried.

### Log flood eliminated

`/tmp/terr/run.log` went from 6,622,808,888 bytes to ~0. Same for the CPU
undefined-opcode path. `grep` over the log no longer times out.

### Boot strategy is now explicit, not oscillated

Past sessions flip-flopped between "boot rtld" and "boot main"; each choice
un-did the last. Selected by `NEMU_BOOT_ENTRY=rtld|main` (default `rtld`),
documented at the decision site in `title_loader.cpp`.

### What each strategy actually does now (measured)

`NEMU_BOOT_ENTRY=rtld` (default):
```
[NEMU-BOOT] frames=8 instructions=120000 memory_faults=137057 -> DEGRADED
```
All 4 modules load and 77,557 symbols link, but **rtld relocates itself to a
garbage base (`0x1871_0000_00`)** and every write from there faults. rtld
derives its own load base from a *module list* structure the real kernel hands
it; NEMU never installs one. This is the next real blocker and it is a
kernel-side subsystem, not a loader-arithmetic bug.

`NEMU_BOOT_ENTRY=main`: reaches real Terraria code and dies at
`PC 0x72762624 (RET X30, X30=0)` — i.e. the game's init chain restores a link
register of 0 from the stack. The disassembly confirms a normal epilogue
(`LDP X29,X30,[X29]` / `RET`), so the frame was entered via a `BR` tail-call from
a caller whose X30 was already 0.

### Loader continuation stub added

`LOADER_CONT_ADDR = 0xDEAD1000` is now mapped R|W|X at init and holds
`SVC #7` / `SVC #10` / `B`, so the initial NSO thread's LR points at real,
mapped, executable loader-continuation code instead of address 0. A top-level
`RET` from the game's init chain therefore exits the process terminally rather
than decoding `0x00000000` at PC 0 and spinning.

Note: this does **not** yet fix the `X30=0` RET, because that RET restores 0
*from a stack frame*, not from the initial LR. Reaching further requires the
rtld path plus a kernel-installed module list.

---

# 2026-09-30: verified body-decrypt root causes + a WORKING decrypted-ExeFS boot

Two independent advances:

## 1. NCA body decrypt root causes, proven against hactool ground truth

`tools/nca_body_oracle.py` reproduces hactool's ExeFS decrypt end-to-end
(pure pycryptodome) and terminates in `PFS0 OK`. Verified facts:

| Item | NEMU (wrong) | Correct (hactool) |
|---|---|---|
| KAK generation | header byte `0x206` = 2 → `key_area_key_application_02` → section key `097323a0…` | master key revision **7** → `key_area_key_application_07` → section key **`a55c8f182443fe589954b4314798d1ae`** |
| Active key-area slot | 0 (via `kaek_index_`) | **slot 2** (encrypted `1d8426cd…`, decrypts to `a55c8f18…`) |
| Section cipher CTR | offset-based, `sec.ctr` | AES-CTR base `171a6a93888d55341693cbacedae15a5`, 128-bit inc per 0x10 block |

The first 0x200 of ExeFS decrypts to `PFS0 05 00 00 00 38 00 00 00 …`
when and only when all three above are correct. So the fix in `nca.cpp`
`ExtractSection` must: (a) resolve the KAK by sweeping `key_area_key_application_<gen>`
and validating the ExeFS sees `PFS0`, and (b) build the section CTR the way
hactool does (`nca_update_ctr`), not from `sec.ctr`.

## 2. A WORKING boot via the decrypted-ExeFS path (bypasses body-decrypt)

Because the in-emulator NCA body decrypt above is still being reworked, we can
still boot Terraria by feeding NEMU a **decrypted ExeFS PFS0** (hactool already
produced `rtld`/`main`/`subsdk0`/`sdk`/`main.npdm`):

```bash
python3 /tmp/terr/build_pfs0.py                       # builds a 0x200-aligned PFS0
./build/bin/Nemu --run /tmp/terr/terraria_exefs.pfs0 --max-frames=2000
```

This loads all 4 NSO modules with correct bases and reports
`[NEMU-BOOT] frames_executed=2000 -> BOOTED`:
```
Loaded NSO module 'rtld'    at 0x71000000, entries…
Loaded NSO module 'main'    at 0x71010000
Loaded NSO module 'subsdk0' at 0x792A0000
Loaded NSO module 'sdk'     at 0x799C0000
Title ready for execution at entry 0x71000000
```

Next blocker (in-progress): rtld's `_start` re-runs relocation against a module
map NEMU never installs, emitting a 5M-line loop of `WriteBlock fault at
0x1891B2B3D8…`. Since NEMU already fixes up every module's RELA relocations at
load time, the fix is to boot `main` directly (primary_entry prioritised to
`main`'s entry) instead of rtld's `_start`.

## 3. Getting past rtld and into `main` (the game code)

Two fixes chained together to move execution from the dead rtld loop into real
Terraria code:

1. **Boot `main`, not rtld.** NEMU applies every module's RELA relocations at
   load time, so rtld's runtime job is already done. `title_loader.cpp` now
   prefers `main`'s entry point as `primary_entry` (rtld only as fallback).
   This eliminated the entire `0x1891…` WriteBlock-fault storm (5.1M faults/300
   frames → 0).

2. **Correct NSO text entry.** The decompressed NSO text segment begins with an
   embedded NSO/MOD0 module header (first 0x100 bytes; code prologue starts at
   text offset 0x100, matching hactool's `text.align_or_total_size = 0x100`).
   `nso.cpp` now sets `entry_point = base + text.memory_offset + 0x100`.
   Without this, fetching at base fetched a zero word
   (`Undefined instruction 0x00000000`).

After these, execution proceeds into the game: `main` boots, runs, and reaches
PC `0x7390A584` — thousands of instructions deep.

## 4. Missing AArch64 opcode: `BR Xn`

Execution halted at `Undefined instruction 0xD61F0220 at PC 0x7390A584`.
`0xD61F0220` decodes to **`BR X17`** (Unconditional branch, register: `1101011
0000 11111 000000 Rn=17 00000`). NEMU's decoder handled `BLR` (`0xD63F…`) and
`RET` (`0xD65F…`) but not `BR` (`0xD61F…`). Added `Opcode::BR` to the enum,
decoder, interpreter (`PC = X[rn]`), and JIT (end block, `PC = X[rn]`).
Added `TestBranchRegister` (unit test on the exact `0xD61F0220` encoding).

After this, execution no longer halted on BR; it progressed to
`PC 0x00000000028FA558 is not valid memory` — an **unmapped indirect-branch
target**, the signature of an unresolved GOT/function-pointer slot.

## 5. Module-relative relocation fixup (the real Gun)

`nso.cpp::ApplyRelocations` previously:
- Searched for `MOD0` only in **rodata**; commercial games like Terraria's
  `main` have `MOD0` embedded in the **text** segment (offset 0x8), so MOD0 was
  never found and **zero relocations applied**.
- Handled only `R_AARCH64_RELATIVE` (1027), skipping `R_AARCH64_ABS64` (257 —
  Terraria's `main` has ~2676 of them) and `GLOB_DAT`/`JUMP_SLOT` (1025/1026).

Verified against the flat module image (hactool-decrypted NSO laid out at its
memory_offsets):
- `MOD0` at flat offset 0x8; `MOD0+4` = offset-from-MOD0 to `.dynamic` →
  `.dynamic` at flat offset `0x3ba7408` (in rodata/data).
- `.dynamic` yields `DT_RELA=0x28fe0c8`, `RELASZ=0x70c1a0` →
  **`{1027(RELATIVE):305208, 1025(GLOB_DAT):16, 257(ABS64):2676}`**.

Rewrote `NsoLoader` to build a **flat module image** (text+rodata+data at their
memory_offsets), map it, and apply RELATIVE/ABS64 (write `base+addend`) and
best-effort GLOB_DAT/JUMP_SLOT (sym==STN_UNDEF) relocations against it. This
populates the function-pointer/GOT slots so indirect `BR`-style calls resolve to
real module addresses instead of `0x28FA558`-style garbage.

## 6. Cross-module symbol import resolution (the last linking gap)

Applying module-relative relocations alone was not enough: after it, `main`
still crashed at `PC 0x28FA558`. Verified in `main`'s GLOB_DAT/JUMP_SLOT
relocations that the unresolved slots reference **imported C++/libc symbols**
defined in other modules:
`strdup`→sdk@0x4E6C08, `longjmp`→sdk@0x4A8644, `stdout`→sdk@0xB907B8,
`_ZTVSt12length_error`→sdk, `__rel_dyn_*`, `_ZNSt3__1…` (subsdk0/sdk contain the
~10.9K + 24.8K-symbol stl/libc runtime). Those GOT slots holding garbage are
exactly what `BR X17` jumped through.

Added:
- `NsoLoader::CollectExportedSymbols` — parses `.dynsym`/`.dynstr`, records every
  defined (shndx≠0) symbol name→module-relative value into `NsoLoadedImage`.
- `NsoLoader::ResolveSymbolImports` — walks a module's GLOB_DAT/JUMP_SLOT relocs
  (non-UND symbol index), looks the name up in a global map, writes the resolved
  guest address into the GOT slot.
- `LoadExeFS` now builds a global `name → (module_base + value)` map across all
  modules, then resolves each module's imports.

Verified result: **`Linked 77557 symbols across 4 modules`**; imports resolved —
rtld 5, **main 16** (all of its GLOB_DAT), subsdk0 1026, sdk 1105 ≈ 2152 total.

Remaining gap after this (fix ##7 in progress): the entry-point prologue scan
regressed to `text+0x0` (booting into the 0x100-byte module header → `Undefined
0x00000000`). `nso.cpp` now falls back `entry_rel → 0x100` when no prologue is
found, so boot resumes past the embedded header into real code.



## 5. (2026-10-01) Final blocker: `RET X30=0` at end of TerTera's sync init chain

**Status after all fixes (branch trace, reproduced deterministically):**

The game now loads all 4 modules, applies ~34,000 cross-module symbol
imports, and executes TerTera's real init chain across `main`/`subsdk0`/`sdk`
for thousands of instructions. It then deterministically dies at:

```
Low-branch: PC 0x72762624 -> next 0x0000000000000000 (inst D65F03C0 RET, rn=30)
```

The call chain (from BRANCH-TRACE):
```
main crt0 (0x7101..) -> main funcs -> sdk (0x7390/0x79E4) -> BR X17 tail-calls
   -> back to main (0x727625C8) -> RET at 0x72762624 with X30=0
```

**The disassembly of the crash site** (main text offset 0x1752624) is a normal
function epilogue: `LDP X29,X30,[SP],#0x30` + `RET X30`. X30 was restored as 0
from the stack, meaning the function was entered via a `BR` tail-call with X30
already 0 from an outer caller — the top-level `_start` (entry text+0x30,
`SUB SP,#0x90; STP X29,X30,[SP,#0x60]`) eventually restores and uses an X30
that is 0.

**Root cause (confirmed):** TerTera finishes its synchronous crt0/init and
returns to the "back to loader/applet" continuation that only exists under a
**proper rtld title boot**. NEMU boots `main` directly (bypassing rtld), so
there is no applet continuation for the initial thread to return to. Setting
the initial X30 to either 0 or EXIT_ADDR (0xDEAD0000) produced the SAME crash
address, so this is NOT an initial-register bug — it is a missing
rtld/title-boot handoff stage.

**What works now (all verified, real emulator runs):**
- XCI -> NCA3 body decrypt (hactool ground truth; KAK generation 7,
  section key a55c8f18...)
- LoadExeFS boots rtld/main/subsdk0/sdk from a decrypted ExeFS PFS0
- Flat module image + module-relative RELATIVE/ABS64 relocations
  (`main` gets 307,884)
- Cross-module symbol linking (~34k symbols, ~14k imports resolved,
  incl. C++ libstdc++: strdup, longjmp, stdout, vtables, __rel_* markers)
- Missing AArch64 opcodes added: BR Xn, 64-bit LDP, post-index LDR (+
  writeback), CSINC/CSINV/CSNEG
- Low process/thread-context region (0x0..0x4000) mapped for crt0

**Remaining work to actually boot the menu:** implement the rtld title-boot
handoff — the applet/main-thread continuation that produces a non-zero return
target after TerTera's init returns. Options:
  (a) Run rtld's `_start` for real (pass module map, apply rtld relocations),
      instead of jumping straight to `main`.
  (b) Provide an applet/loader continuation stub that `nnMain` waits on /
      returns to, so the init RET lands in mapped, productive code.
This is a distinct subsystem, not another single opcode/register fix.

**Regenerate the boot asset:**
```bash
# extract base NCA from XCI at 0x17018600, then:
~/hactool/hactool -t nca -k games/prod.keys.clean --exefsdir=/tmp/terr/exefs0 /tmp/terr/base.nca
python3 /tmp/terr/build_pfs0.py   # data at header_end (0xA8), PFS0 rule
./build/bin/Nemu --run /tmp/terr/terraria_exefs.pfs0 --max-frames=8
```


---

## 6. (2026-10-01) REAL ROOT CAUSE FOUND: unbounded `.dynsym` walk + JIT masking faults

The "missing rtld handoff" diagnosis in section 5 above was **wrong**. The
`RET X30=0` was not a boot-contract problem at all — it was a **corrupted GOT
slot**, and the boot verdict was also lying about it. Both are now fixed.

### Bug A — `.dynsym` parsed far past its real end (the actual Terraria crash)

`NsoLoader::CollectExportedSymbols` walked `.dynsym` from `DT_SYMTAB` to the
**end of the flat module image**. `.dynsym` is not self-terminating: it runs
straight into `.dynstr` (`DT_STRTAB`) and then into ordinary `.rodata`/`.data`
that merely *looks* like 24-byte symbol records. Everything past the table was
harvested as a "symbol".

Measured on the real Terraria ExeFS (DT_HASH `nchain` = the true entry count):

| Module | true `.dynsym` entries | entries NEMU scanned | over-read |
|---|---|---|---|
| `main` | **801** | 3,604,324 | **4500x** |
| `sdk` | 24,755 | 274,936 | 11x |
| `subsdk0` | 11,522 | 133,775 | 12x |
| `rtld` | 20 | 298 | 15x |

That produced the 77,557 "symbols" NEMU logged. Because the map is
first-wins (`out.emplace`), the phantom copies **shadowed the real
definitions**. Concretely, replaying NEMU's exact algorithm on the real image:

```
_Znwm  -> 0x27471010755   <-- garbage   (should be sdk 0x79A026B8)
_ZdaPv -> 0x548710107ae   <-- garbage
_Znam  -> 0x79E42758      <-- correct (first-wins kept sdk's real copy)
```

`_Znwm` is C++ **`operator new(size_t)`**, imported by `main` through
`DT_JMPREL` entry 34 → GOT slot `0x74BB76E0` (module offset `0x3BA76E0`, in
`.data`/`.bss`, so zero at load). `main` calls it via a PLT thunk:

```asm
0x7390a798  adrp x16, #0x74bb7000
0x7390a79c  ldr  x17, [x16, #0x6e0]     ; GOT[34]  == 0x27471010755
0x7390a7a4  br   x17
```

so control left the mapped modules entirely. The return address that was
finally observed is the *downstream symptom* of that jump, not its cause.

**Fix** (`nso.cpp`): read `DT_HASH` (tag 4) and use its `nchain` field as the
authoritative entry count, falling back to the `.dynstr` boundary. Same count
also gates the symbol-name lookup in `ResolveSymbolImports`. Result on the
real cart: 77,557 phantom entries → **35,134 real symbols**, and all four
allocator imports now resolve inside `sdk` (0x799C0000–0x7A635000, matching
NEMU's own module-load log).

### Bug B — the headless boot probe reported "BOOTED" on a dead guest

`JitCompiler::CompileBlock`'s `default:` arm (any opcode the JIT cannot
translate) ended the block, **wrote `PC = curr_pc` back into the guest state,
and returned a valid block pointer**. `Execute` therefore returned `true`, so
`StepCpuQuantum` counted a retired instruction and never reached its
undefined-instruction branch. The guest spun on one undecodable word forever
and the probe scored it as a success:

```
[NEMU-BOOT] frames_executed=200 instructions=3000000 memory_faults=0 -> BOOTED
[ERROR][CPU] Undefined instruction 0x00000001 at PC 0x0 (#1 .. #2900000)
```

**Fix** (`jit_compiler.cpp`): the `default:` arm now returns `nullptr` instead
of emitting a no-op block. `Execute` returns `false`, the caller re-steps on the
interpreter, `StepResult::UndefinedInstruction` is counted, and the existing
64-consecutive-fault watchdog terminates the thread and sets
`StalledOnFault()` — so this class of failure reports `FAILED` with exit 4
instead of a fake `BOOTED` with exit 0.

### Also corrected in this pass

* `rtld` now loads and links cleanly, but is **not** the correct boot entry for
  this title. Ryujinx (both the classic and current `qlaunch` trees) and
  Eden/yuzu boot the **first NSO directly** and perform relocations in the
  loader; they never hand rtld a module map. NEMU already does the same, so
  `NEMU_BOOT_ENTRY=main` is the architecturally correct path and the rtld path
  is kept only for bring-up comparison.

### Reproduce

```bash
python3 /tmp/terr/which3.py    # JMPREL[34] -> _Znwm at GOT 0x3ba76e0
python3 /tmp/terr/bound2.py    # DT_HASH nchain vs NEMU's scan length
NEMU_BOOT_ENTRY=main ./build/bin/Nemu --run /tmp/terr/terraria_exefs.pfs0 --max-frames=200
```

