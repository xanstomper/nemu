# Nemulator — System Overview

This document is a full tour of the **Nemulator** emulator: what it does, how it
is architected, and how it fits commercial Switch games inside the Xbox
Developer Mode **5 GiB memory budget**.

For the component-level design specs, see the sister docs:
[`ARCHITECTURE.md`](ARCHITECTURE.md), [`GPU_DESIGN.md`](GPU_DESIGN.md),
[`KERNEL_DESIGN.md`](KERNEL_DESIGN.md), [`CPU_DESIGN.md`](CPU_DESIGN.md),
[`MEMORY_DESIGN.md`](MEMORY_DESIGN.md).

---

## 1. The Problem

A Switch game ships as an **NCA** (encrypted) or **NSP/XCI** container. To run
it you must:

1. **Decrypt** the container (AES-XTS header + CTR sections) using the
   console's keyset.
2. **Decompress + relocate** the executable (NSO: LZ4 + `R_AARCH64_RELATIVE`).
3. **Execute** ARM64 code, trapping into the space of the Horizon OS kernel
   (system calls, IPC to services).
4. **Translate** every GPU shader + draw from Maxwell 3D to the host GPU.
5. Do all of the above **inside a 5 GiB UWP process cap** (Xbox Dev Mode),
   while a real game sees ~2.5 GiB of guest RAM on hardware.

Nemulator solves all five, with a desktop validation pipeline that keeps every
layer testable before you ever touch the console.

---

## 2. Architecture at a Glance

```
┌────────────────────────────────────────────────────────────────┐
│  Nemu.Frontend  (Xbox-native HOME / game browser / settings)   │
└───────────────┬────────────────────────────────────────────────┘
                │   frontend ↔ system API
┌───────────────▼────────────────────────────────────────────────┐
│                     Nemu.Core  (C++20, platform-agnostic)       │
│                                                                │
│  ┌──────────────┐  ┌─────────────────┐  ┌───────────────────┐  │
│  │  ARM64 CPU   │  │  Kernel HLE     │  │  GPU (Maxwell3D)  │  │
│  │  JIT+Interp  │  │  SVC / IPC/NSVC │  │  → D3D12 / Null    │  │
│  └──────┬───────┘  └───────┬─────────┘  └─────────┬─────────┘  │
│         │                  │                      │            │
│  ┌──────▼───────┐  ┌───────▼─────────┐  ┌─────────▼─────────┐  │
│  │  Memory      │  │  Loader         │  │  Audio / HID / VFS│  │
│  │  fastmem+5GB │  │  NCA/NSO/NRO/    │  │  XAudio2 / ADPC M │  │
│  │  governor    │  │  PFS0/RomFS/tik │  │  Npad / SaveFS    │  │
│  └──────────────┘  └─────────────────┘  └───────────────────┘  │
└───────────────┬────────────────────────────────────────────────┘
                │   Nemu.Platform  (Xbox UWP D3D12/XAudio2/WGInput, Linux test)
```

---

## 3. The Commercial-Game Pipeline (load → translate → render)

### 3.1 Loading (`src/core/loader/`)

| Format | Loader | Capability |
| :--- | :--- | :--- |
| **NCA** | `nca.cpp` | **AES-XTS** header decrypt + **CTR** section decrypt via `crypto::Aes128` + `KeyStore` |
| **NSO** | `nso.cpp` | LZ4 decompress, segment load (.text/.rodata/.data), BSS, **AArch64 RELA relocations** |
| **NRO** | `nro.cpp` | homebrew executables |
| **PFS0 / ExeFS** | `pfs0.cpp` | `.nsp` / ExeFS containers |
| **RomFS** | `romfs.cpp` | game read-only filesystem |
| **title** | `title_loader.cpp` | format sniffing, `.tik` ticket load for key generation |

Keys come from the user's own `prod.keys` / `title.keys` (never bundled).
`LoadTitle` finds a `.tik` beside the title and applies it.

### 3.2 CPU Execution (`src/core/cpu/`)

- **AArch64 interpreter** — deterministic reference for correctness.
- **x86-64 JIT** — a dynamic recompiler with a **block cache** and **fastmem**
  (guest accesses compile to a direct host-pointer add, with a fault handler
  for unmapped pages).
- **Horizontal syscalls** — the JIT traps into `SvcDispatcher::Dispatch`.

### 3.3 Kernel / Horizon OS HLE (`src/core/kernel/`)

- **49 system calls** across the surface commercial games actually use:
  memory (`SetHeapSize`, `QueryMemory`, `Lock/UnlockProcessMemory`),
  threads (`Create/Start/ExitThread`, `Get/SetThreadPriority`,
  `GetCurrentProcessorNumber`), synchronization (events, arbiters, process-wide
  keys), handles (`CloseHandle`, `DuplicateHandle`), and IPC
  (`ConnectToPort`, `SendSyncRequest`).
- **54 IPC services** registered at boot: `sm`, `set`, `time`, `acc`, `hid`,
  `applet`, `fsp-srv`, `nvdrv`, `nvhost`, `vi`, `audren`, `audout`, `pl`,
  `nifm`, `bsd`, `ldn`, `bpc`, `caps`, `pctl`, `friend`, `aoc`, `apm`, `prepo`.

### 3.4 GPU / Shader Translation (`src/core/gpu/`)

- **166 opcode cases** covering **all 162 `maxwell.inc` SASS families**: the
  fast-path map (MOV/ALU/FMA/…), the extended emitter (IMAD/XMAD/SETP/MUFU/
  half-float/texture/flow-control), and auditable diagnostic passthrough for
  the 40 rarest.
- **Predicated branches** — `BRA.P` decodes its predicate + invert sense.
- **Multi-render-target (MRT)** — `ColorTargetMrtEnable` → N target PSO with a
  D32 depth-stencil heap; the fragment emitter writes `SV_Target0..7`.
- **Compute** — Queue-Meta-Descriptor (`ComputeQmd`) parsing + D3D12 compute
  PSO dispatch.
- **GMMU** — 40-bit GPU VA space, 64 KiB sparse big pages.
- **BufferCache** — dirty-range partial uploads + **covering-buffer dedup**.
- **TextureCache** — ASTC→BC1 recompress (8÷1 memory), **byte-budget LRU**,
  **BC1 alpha punch-through**.
- **PipelineBridge** → `ShaderTranslator` → `PipelineCache` → D3D12 PSO, with
  dynamic CBV/vertex upload and SRV binding at the emitter's `t0/s0`.

### 3.5 Memory Budget (`src/core/memory/`)

Games fit the 5 GiB cap by design:

| Budget line | Size |
| :--- | :--- |
| Guest RAM (fastmem, commit-on-demand) | ~2.5 GiB |
| Resident textures (byte-budget LRU default) | 1.5 GiB (tunable via `--texture-budget`) |
| JIT code cache (16 MiB reservable) | ~0.5 GiB with overhead |
| **Total** | **~4.5 GiB < 5 GiB cap** |

`MemoryBudget` surfaces live **`RAM Used / Peak (5 GiB cap)`** in Diagnostics,
and the texture cache feeds its resident bytes into the governor.

---

## 4. Input, Audio, Saves

- **Input** (`src/core/hid/`): an `XboxControllerDriver` polls pads (UWP
  `xinputuap`), maps A/B/X/Y/bumpers/triggers/LS/RS/D-pad to Switch button
  masks, and feeds the guest HID `RingLifo<NpadCommonState>`.
- **Audio** (`src/core/audio/`): XAudio2 + Null backends over an SPSC ring
  buffer; the **Nintendo DSP ADPCM** codec decodes compressed audio.
- **Saves** (`src/core/save/`): atomic per-title save store under `save:/`, plus
  **USB backup / restore** (`BackupSavesTo` / `RestoreSavesFrom`) so progress
  survives reinstall.

---

## 5. Frontend

`src/frontend/` is an Xbox-native HOME experience: a game library/browser, a
7+-category **Settings** app, **Diagnostics** (including the live 5 GiB budget
row), and Switch-style HOME menus — all navigable by gamepad, styled for the
Xbox UWP shell.

---

## 6. Validation

- **29 automated test suites** (`tests/unit/`) covering every subsystem.
- Both the **Linux (GCC)** and **Windows/Xbox (MinGW-w64)** builds are kept
  green; key PE32+ suites run under Wine for parity.
- **Headless boot probe** (`--run <nro> --max-frames=N`) prints a BOOT verdict
  on all platforms — the CI gate and the on-device bring-up signal.
- **`scripts/xbox_bringup.sh`** packages, deploys via the Xbox Device Portal,
  and runs the probe against real hardware, asserting D3D12 init + BOOTED.

---

## 7. Honest Known Limits

- The **D3D12 pixel-exact render proof** and per-title calibration must happen
  on a real Dev Mode Xbox — no desktop test substitutes for the hardware gate.
- Retail **XCI** boot additionally needs the correct user-supplied keys for the
  specific title.
- The 40 rarest SASS families (atomics, surface/video ops) are auditable
  passthrough, not executed — rare in shipped shaders.

---

*Next: [`BOOT_READINESS_AUDIT.md`](BOOT_READINESS_AUDIT.md) for the verified
end-to-end chain, or [`OPTIMIZATION_PLAYBOOK.md`](OPTIMIZATION_PLAYBOOK.md) for
the 5 GiB engineering.*