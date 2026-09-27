# NEMU Porting Research — Verified Upstream Architecture Reference

**IMPLEMENTATION STATUS (2026-09-27):** most of this document is now IMPLEMENTED in
NEMU. See the ✅ markers per section. Remaining unported items are marked ⬜.

**Sources:** ~60 files downloaded from GitHub mirrors of Ryujinx (MIT) and yuzu
(GPL-2.0/3.0-or-later), verified on disk under `/tmp/nemu-research/`. Mirrors used:
`NextendoNetwork/Ryujinx-Nextendo`, `alula/Ryujinx` (Ryujinx MIT);
`shadowdelsus/yuzu`, `gmh5225/yuzu-mirror`, `cobaltgit/yuzu-preserved` (yuzu GPL).
Note: `Ryujinx/Ryujinx` and `Ryubing/Ryujinx` 404 on GitHub raw; `git.ryujinx.app` and
`git.eden-emu.dev` are blocked by this environment's TLD approval gate.

> **Licensing:** NEMU is GPL-3.0-or-later. Ryujinx is **MIT** ("Copyright (c) Ryujinx
> Team and Contributors", LICENSE.txt verified on disk). yuzu is **GPL-2.0-or-later**
> (engine core, 2018-era files) / **GPL-3.0-or-later** (newer files, SPDX headers).
> Both are legally inbound-portable into NEMU with attribution. Nintendo-proprietary
> material (keys, firmware, assets) remains off-limits.

---

## 1. Maxwell 3D method surface (Tier-A2) — ✅ implemented (boot defaults + expanded surface; ⬜ full 0xE00-table port remains)

**yuzu** (`maxwell_3d.h/.cpp`): `NUM_REGS = 0xE00`; register offsets are asserted via
`ASSERT_REG_POSITION(field, byte_offset)` — **345 asserted offsets captured** in the
local copy. Key byte offsets (÷4 for method index):

| Field | Byte offset | Method |
|---|---|---|
| viewport_transform | 0x0A00 | 0x280 |
| vertex_buffer | 0x0D74 | 0x35D |
| depth_mode | 0x0D7C | 0x35F |
| clear_color | 0x0D80 | 0x360 |
| clear_depth | 0x0D90 | 0x364 |
| scissor_test | 0 **0x0E00** | 0x380 |
| stencil_back_ref/mask/func_mask | 0x0F54/58/5C | 0x3D5/56/57 |
| depth_bounds | 0x0F9C | 0x3E7 |
| zeta (depth/stencil RT) | 0x0FE0 | 0x3F8 |
| clear_control | 0x10F8 | 0x43E |
| depth_bias_control | 0x1110 | 0x444 |

(Zero-prefix typo guard: scissor_test = **0x0E00**, method 0x380 — matches NEMU's
existing constants.)

Method dispatch flow (yuzu `maxwell_3d.cpp::CallMethod`):
`CallMethod → method ≥ 0xE00 = macro registers → ProcessMacro (entry =
((method-0xE00)>>1) % macro_positions.size()) → ProcessShadowRam →
ProcessDirtyRegisters (per-register dirty flags) → ProcessMethodCall switch`
(0x0110 wait_for_idle, 0x01B0 launch_dma, 0x01B4 inline_data, const-buffer inline
writes, sync_info 0x02C8, clear_surface, 0x0DE0 fragment_barrier; default →
`draw_manager->ProcessMethodCall`).

**Ryujinx** splits the same surface across `ThreedClass.cs` (dispatch), `ThreedClassState.cs`
(full state struct), `DrawManager.cs` (DrawArrays/DrawIndexed/DrawTexture + instanced),
`StateUpdater.cs` / `StateUpdateTracker.cs` (dirty-flag application). Struct sizes asserted
in yuzu: `VertexAttribute=4B, RenderTargetConfig=0x40B, ViewportTransform=0x20B, Buffer=0x20B`.

`InitializeRegisterDefaults()` (yuzu) gives required boot defaults: depth range 0..1,
blend Add/One/Zero, stencil Keep/Always, rasterize_enable=1.

**NEMU action:** extend the existing `MaxwellMethod` namespace with the byte/4 offsets
above; port `InitializeRegisterDefaults()` first — real games expect these defaults at boot.

---

## 2. Buffer cache (Tier-A4) — ✅ implemented (dirty-range merge + partial uploads; ⬜ 64B-page tracker upgrade + selective readback)

**yuzu** (`buffer_cache.h`, 73KB, GPL-3.0+): dirty tracking is a per-buffer
**memory_tracker** (CPU/GPU write bits per 64B page) plus per-channel
`dirty_uniform_buffers` bitset (`.fill(~u32{0})` on flush = all-dirty). Key entry points:
`CachedWriteMemory(device_addr, size)` → `IsRegionRegistered()` check → mark dirty or
skip; `FlushCachedWrites()` → `memory_tracker.FlushCachedWrites()`. Downloads funnel
through `DownloadMemory(gpu_addr, size)` which only pulls **dirty** 64B pages.

**Ryujinx** (`BufferCache.cs`, 44KB): buffer groups keyed by range overlaps
(`BufferCacheEntry` per binding), partial `Flush`/`Modified` ranges with an underlying
`RangeSet`. `SnapshotPartialOverrides` pattern for split uploads.

**NEMU action:** the new `BufferCache` (commit 9dc1c2c) already does dirty-range merge +
partial upload keyed by GPU address. Upgrade path: adopt yuzu's 64B-page tracker inside
each Entry instead of coarse range list, and add `DownloadMemory`-style selective
readback (needed for transform feedback / render-to-texture readback later).

---

## 3. GMMU (Tier-A5) — ✅ implemented (uniform 64KiB pages; ⬜ big/small split at 1<<34 if PTE kinds needed)

**yuzu** `memory_manager.{h,cpp}`: 40-bit VA, **big-page bits = 16 (64 KiB)** +
small-page bits = 12 (4 KiB), split at 1<<34 (`split_address`). Page-table walk
`template <bool is_big_pages> PageEntryIndex(gpu_addr)`; `Map(span, PTEKind, is_big_pages)`
and `MapSparse` for sparse regions.

**Ryujinx** `MemoryManager.cs`: 2-level page table, `PtLvl0Bits=14`, `PtLvl1Bits=14`,
`PageSize = 1<<PtPageBits` (4 KiB), `AddressSpaceBits = 12+14+14 = 40`. Sparse `ulong`
bitmaps per level; multi-page transfers handled with aligned page loops.

**NEMU action:** current `GpuMemoryManager` uses uniform 64 KiB pages — sufficient for
correctness. If/when per-PTE-kind handling (compression, target kinds) is needed, adopt
yuzu's big/small split at 1<<34.

---

## 4. Compute dispatch (Tier-A3) — ✅ registers + backend hook; ⬜ real 0xAF/QMD layout + compute PSO translation

**yuzu** `kepler_compute.{h,cpp}`: `launch_description` register block with
`launch_desc_loc` + `launch` method at **0xAF** (also upload 0x60, exec_upload 0x6C,
data_upload 0x6D, tsc 0x557, tic 0x55D). Const-buffer binding via
`const_buffer_selector`/`const_buffer_stage_selector`. Dispatch executes through
`draw_manager`'s `LaunchDescription` → shader compile of `Car.EMIT`-style programs.

**Ryujinx** `ComputeClass.cs` + `ComputeClassState.cs` + `ComputeQmd.cs` (launch QMD =
"queue meta descriptor": grid dims, block dims, shared memory size, shader address +
constants).

**NEMU action:** re-point NEMU's `ComputeLaunchDesc`/`DispatchCompute` methods to the
real offsets (launch method 0xAF on the compute subchannel; QMD layout from
ComputeQmd.cs), and translate the compute program with the (now expanded) shader
decoder — stage `ShaderStage::Compute` already exists in the decoder enum.

---

## 5. Shader decoding (Tier-A1) — ✅ COMPLETE: 41-opcode fast path + full 279-encoding/162-family SASS table (sass_identifier) + predicate file + exact LOP3 LUT; ⬜ IR-level translation of remaining families (PSET/TEXS variants → HLSL) is the last depth layer

**yuzu pipeline:** `frontend/maxwell/decode.cpp` → `maxwell.inc` opcode table (17KB —
full instruction name/property tables) → `translate_program.cpp` → IR defined in
`frontend/ir/opcodes.inc` (83KB IR opcode list) → `structured_control_flow.cpp`
(goto elimination) → GLSL/DXBC-like emission.

**Ryujinx pipeline:** `Decoders/Decoder.cs` + `InstTable.cs` (54KB decode table) +
`InstDecoders.cs` (224KB per-opcode decoders) → `Instructions/InstEmit*.cs` emitters →
`StructuredIr/` (goto elimination) → `Translator.cs` → IL/CodeGen.

**NEMU action:** NEMU's decoder now covers 41 opcodes. Next expansions, in payoff
order: (1) `maxwell.inc` full opcode-name table port (MIT/GPL table of ~200 names),
(2) predicate register file + `@P0`-style predicated execution (NEMU already parses
predicate bits — emit `if`), (3) full LOP3 LUT emission (`(a&b)|(c&~b)` style from
the 8-bit LUT), (4) TEXS dest-mask variants (`TEXS.F32.F32` etc.), (5) bindless
texture descriptors via TIC entries.

---

## 6. IPC service reply encodings (Tier-B) — ✅ ALL IMPLEMENTED: hid CreateAppletResource chain, RingLifo<NpadCommonState> shared memory, GetCurrentTimePoint (0x18 reply), NotifyRunning u8, RomFS-root data storage

## 6b. Texture recompression (Tier-B1) — ✅ COMPLETE: Bc1Encoder + wired into TextureCache ASTC upload path (8x host-memory reduction, D3D12 GPU-side decode)

**hid IAppletResource** (`IAppletResource.cs`, verified): `[CommandCmif(0)]
GetSharedMemoryHandle() -> handle<copy>` — returns the hid shared KSharedMemory as a
copy handle. **This matches exactly what NEMU now implements** (CreateAppletResource →
IAppletResource → cmd 0 = GetSharedMemoryHandle; commit in tree, test green).

**time ISteadyClock** (`ISteadyClock.cs`): cmd 0 `GetCurrentTimePoint() ->
nn::time::SteadyClockTimePoint` (writes the struct via `ResponseData.WriteStruct` —
layout: u64 clock offset + u128 steady time point = **0x18 bytes**), cmd 2 GetTestOffset,
3 SetTestOffset, 100 GetRtcValue, 200/201 Get/SetInternalOffset. NEMU's
`TimeClockService` should add `GetCurrentTimePoint` (0x18-byte reply) and map
`GetStandardSteadyClock` sessions to it.

**am IApplicationFunctions** (`IApplicationFunctions.cs`): cmd 20 `EnsureSaveData(Uid)
-> u64 requiredSize` (NEMU returns 0 — fine), cmd 21 `GetDesiredLanguage() ->
LanguageCode (u64)`, cmd 40 `NotifyRunning() -> b8 = true` (NEMU writes u32 1 — should
be **u8 1** at offset 4), cmd 50 `GetPseudoDeviceId() -> Uuid` — **Ryujinx-Nextendo
notes real HW returns a NON-ZERO per-title Uuid; an all-zero id is rejected by some
online/lobby code**. NEMU already writes a fixed non-zero value — keep it.

**fs IFile** (`IFile.cs`): cmd 0 Read(offset s64, size s64, [buffer 0x46] out bytes)
-> (bytes read), cmd 1 Write, cmd 2 Flush, cmd 3 SetSize(s64), cmd 4 GetSize -> s64.
Matches NEMU's implementation.

---

## 7. Attribution practice (for ports)

- Ryujinx: MIT, "Copyright (c) Ryujinx Team and Contributors" — preserve the license
  text in `third_party/` NOTICE and per-file headers on ported code.
- yuzu: SPDX headers per file (GPL-2.0-or-later engine core, GPL-3.0-or-later newer);
  copy the SPDX line + copyright line verbatim into the ported NEMU file's header.
- Record provenance in `docs/PORT_AUDIT.md` alongside each port: source repo, commit/
  branch, file, license, port date.

## 8. Local reference cache

`/tmp/nemu-research/ryujinx/` (~42 files: ThreedClass*, DrawManager, BufferCache,
MemoryManager, ComputeClass*, Decoder/InstTable/InstDecoders/Translator, IFile,
IFileSystem, IApplicationFunctions, ISteadyClock, IAppletResource, IHidServer, Hid,
RingLifo, SteadyClock*, SystemClockContext...) and `/tmp/nemu-research/yuzu/` (~25
files: maxwell_3d.*, buffer_cache.*, memory_manager.*, kepler_compute.*, decode.*,
maxwell.inc, opcodes.inc, translate_program.cpp, structured_control_flow.cpp,
texture_fetch.cpp, draw_manager.*). `/tmp` is volatile — commit this document and
re-fetch as needed.
