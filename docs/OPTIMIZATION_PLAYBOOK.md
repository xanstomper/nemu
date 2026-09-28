# NEMU 5 GiB-Budget Optimization Playbook (Xbox Series S/X)

**Source research:** yuzu-mirror (`/tmp/nemu-research/yuzu`, GPL-2.0) +
Ryujinx (`/tmp/nemu-research/ryujinx`, MIT). Techniques below are the exact
mechanisms real Switch emulators use to fit a game under a memory budget. The
Xbox Dev Mode cap is 5 GiB; a Switch game sees ~2.5 GiB guest RAM, so the
emulator must keep its own overhead tight.

Each section: what upstream does, why it matters, and (✅ implemented /
⬜ TODO) in NEMU.

---

## 1. Buffer/texture memory dedup (RYUJINX `BufferCache.cs`)

**Technique:** guest buffers **page-align and share** the guest address space;
a host buffer is only allocated when first needed, only re-uploaded on
`ForceDirty` (guest signaled a CPU write), and a *clean* buffer is served
directly (zero-copy, no re-read) via `CheckModified`. Overlapping buffers are
coalesced into fewer, bigger ones (`CreateBufferAligned` → `FindOverlapsAsSpan`
merge).

```
ForceDirty(gpuVa,size)   -> mark guest-visible write; invalidate cached buffer
CheckModified(gpuVa,size)-> if buffer not modified, alias guest memory (0-copy)
```

**Why it matters under 5 GiB:** streaming games touch the same vertex/texture
ranges every frame. Naive re-upload = bandwidth + transient allocations that
pressure the cap. Dedup keeps one host buffer per guest region and skips
uploads the guest never invalidated.

- ✅ NEMU: `BufferCache::FindCovering` coalescing (dedup_saves), dirty-range
  partial uploads, LRU eviction.
- ⬜ TODO: full `CreateBufferAligned` overlap **merge** (replace multiple
  smaller buffers with one bigger that contains them). NEMU keeps separate
  entries; merging would cut entry count + fragmentation further.

## 2. GPU address-space bookkeeping (YUZU `memory_manager.cpp`)

**Technique:** split page table into **big pages (128 KiB) + small pages (4 KiB)**
(`BigPageTableOp` / `PageTableOp`). Big pages cover most of the address space
in few entries; only sparse/unmapped regions fall back to small-page tables.
Also `GetPointer()` returns a direct host pointer for GPU VA hits (the CPU and
GPU share physical, so no copy).

**Why it matters:** a uniform 64 KiB entry table for the full 40-bit GPU VA is
fine, but the big/small split means most address space is a handful of big
entries → tiny bookkeeping and faster page-table walks.

- ✅ NEMU: uniform 64 KiB sparse page table.
- ⬜ TODO: big/small split at a high VA boundary to shrink the table + speed
  walks. Low priority — table memory is already modest; the surer win is #3.

## 3. Texture compression on upload (NEMU `Bc1Encoder` / Ryujinx ASTC)

**Technique:** ASTC (hardware texture format on Switch) is decompressed once and
re-compressed to **BC1** (D3D12-native) so the host stores 8:1 vs RGBA8 and the
GPU decodes at sample. Ryujinx uploads `astc`→ RGBA → driver-compressed block.

- ✅ NEMU: `Bc1Encoder` wired into `TextureCache` ASTC upload path (8× memory
  reduction). **This is the single biggest 5 GiB lever** — a 2048×2048 ASTC
  texture is ~1.4 MB RGBA vs ~0.7 MB BC1, and atlases multiply it.
- ✅ NEMU: BC1 **3-color + 1-transparent punch-through** preserves UI/sprite
  alpha with zero extra memory (was `⬜ BC1 1-bit-alpha / BC3` — done).

## 4. Present-path optimization pipeline (NEMU `GraphicsOptimizer`)

**Technique:** FSR 1.0/2.0 upscale, Bicubic, FXAA/SMAA AA, MSAA resolve, AFMF
2× framegen — run in the present path, live from Settings.

- ✅ NEMU: all wired + running (was dead code; activated).
- **Why:** lets games render at lower native res (less D3D12 bandwidth) and
  upscale — directly cuts GPU load on the Series S.

## 5. JIT code-cache + fastmem (NEMU `cpu/jit`)

**Technique:** ARM64→x86-64 block JIT with a **16 MiB reservable code cache**
(commit on demand) + **fastmem**: the guest 4 GiB is MEM_RESERVE'd virtually
(≈0 physical) and only MEM_COMMIT'd pages on demand; JIT memory accesses are a
direct host pointer add.

- ✅ NEMU: fastmem reserve/commit + JIT code cache; the 5 GiB RAM-budget
  governor (`MemoryBudget`) tracks committed fastmem + subsystem bytes live in
  Diagnostics.
- **Why:** the 4 GiB guest address space costs ~0 physical until paged in, so
  games fit the 5 GiB cap with ~1 GiB headroom.

## 6. GMMU sparse/on-demand commit

**Technique:** return 0 / fault-on-unmapped for untouched GPU pages rather than
pre-committing the whole image.

- ✅ NEMU: GpuMemoryManager faults on unmapped; committed pages only.
- **Why:** a game that reserves a large texture/stream region but only writes a
  few MB doesn't eat 5 GiB.

---

## Recommended next work (highest value under 5 GiB)

| Order | Item | Effort | Payoff |
|---|---|---|---|
| 1 | ✅ Buffer dedup (done) | small | big (cut redundant uploads) |
| 2 | ✅ BC1 alpha punch-through (done) | medium | big (correct alpha, no BC3 cost) |
| 3 | ✅ Texture byte-budget LRU (done) | small | **big — caps resident textures (the 5 GiB OOM defense)** |
| 4 | ✅ Texture bytes feed 5 GiB governor (done) | small | visibility — Diagnostics shows true combined usage |
| 5 | ✅ Budget tuned to 1.5 GiB textures (done) | small | keeps total ~4.5 GiB < 5 GiB cap |
| 6 | Buffer overlap **merge** in FindCovering | small-medium | medium (covering-reuse already captures most) |
| 7 | Big/small GMMU page split | medium | small-medium (already 64 KiB big-page sparse) |

Items 6-7 remain but are lower-value than the shipped set: covering-reuse already
delivers most of #6's win without stale-id risk, and the GMMU is already a sparse
64 KiB big-page table. Item 7 matters only if profiling shows GPU page-table
memory dominating — profile on-device first.

On-device seatbelt: `scripts/xbox_bringup.sh` + the live
`RAM Used / Peak (5 GiB cap)` Diagnostics row (now showing textures + guest RAM).