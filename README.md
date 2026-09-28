# Nemulator

> **A high-performance Nintendo Switch emulator engineered for Xbox Series S/X Developer Mode, complete with a full desktop development and validation toolchain.**

![Status](https://img.shields.io/badge/status-active-brightgreen)
![Tests](https://img.shields.io/badge/tests-29%20suites%20green-brightgreen)
![Platform](https://img.shields.io/badge/xbox-Series%20S%2FX%20Dev%20Mode-blue)
![License](https://img.shields.io/badge/license-GPL--3.0-informational)

---

## Overview

**Nemulator** (formerly *Nemu*) is a clean-room, HLE-first Nintendo Switch emulator targeting **Xbox Series S and Series X in Developer Mode**, with a native **Direct3D 12** rendering backend, **XAudio2** audio, and full **Windows.Gaming.Input** controller support. It is built to run commercial-grade Switch software — from **NCA / NSO decryption**, through **166 Maxwell SASS opcode families**, **multi-render-target (MRT) deferred rendering**, the **complete Horizon OS system-call surface**, all the way to **working save persistence** — all inside the Xbox Developer Mode **5 GiB memory budget**.

The same codebase runs on Linux for **continuous validation** (29 automated test suites) and produces a **specification-compliant Xbox AppX package** for sideloading.

| | |
| :--- | :--- |
| **CPU** | ARMv8-A (AArch64) interpreter + x86-64 dynamic recompiler (JIT) |
| **Renderer** | Direct3D 12 (Xbox / Windows) + Null / SDL2 backends |
| **Audio** | XAudio2 + Null backends, SPSC ring buffer, Nintendo DSP ADPCM |
| **Memory** | 40-bit GPU world + guest RAM — fastmem reserve/commit, **5 GiB budget governor** |
| **Status** | **29/29 test suites green** · Windows/Xbox PE32+ cross-build clean · deployable AppX |

---

## Highlights

- **Commercial-game boot pipeline** — real NCA **AES-XTS** decryption + CTR sections, NSO **LZ4** + **AArch64 relocations**, PFS0/NSP, RomFS, `.tik` tickets.
- **Complete GPU translation** — 166 opcode cases covering **all 162 Maxwell SASS families**, predicated branches, exact LOP3, compute + Queue-Meta-Descriptor (QMD), and **pixel-accurate D3D12 pipeline translation** with dynamic CBV/SRV binding.
- **Multi-render-target (MRT) deferred rendering** + depth-stencil — the render path commercial games rely on.
- **Full Horizon OS HLE** — 49 system calls (threading, memory, handles, IPC), 54 IPC services (`hid`, `fs`, `vi`, `applet`, `set`, `time`, `nvhost`, `ldn`, `bsd`, …).
- **5 GiB budget engineering** — buffer dedup, BC1 alpha punch-through, texture byte-budget LRU, and a live **`RAM Used / Peak (5 GiB cap)`** diagnostics readout.
- **Persistent saves** — atomic save store + **USB backup / restore**.
- **Controller input** — full Npad mapping, UWP `xinputuap` support, physical pad polling.

---

## Repository Layout

```
nemulator/
├── src/
│   ├── core/
│   │   ├── cpu/        # AArch64 interpreter + x86-64 JIT (fastmem, block cache)
│   │   ├── kernel/     # Horizon OS HLE: SVC dispatcher, KProcess/KThread, IPC
│   │   ├── memory/     # Guest RAM, GPU VA, fastmem, 5 GiB budget governor
│   │   ├── gpu/        # Maxwell3D, GMMU, buffer/texture caches, shaders, D3D12
│   │   ├── audio/      # XAudio2 + Null, SPSC ring, Nintendo DSP ADPCM
│   │   ├── hid/        # Controller drivers, Npad mapping
│   │   ├── filesystem/ # VFS, RomFS, PFS0, save manager
│   │   ├── loader/     # NCA, NSO, NRO, PFS0, title loader
│   │   └── system/     # Emulator orchestration, runtime config
│   ├── frontend/       # Xbox-native HOME/game-browser/settings UI
│   └── nemu/           # CLI entry, headless boot probe, --texture-budget
├── tests/unit/         # 29 automated test suites
├── scripts/            # packaging, AppX build, on-device QA harness
├── packaging/xbox/     # AppX manifest, assets, signing
├── docs/               # (see below)
└── tools/              # SASS table generator, etc.
```

---

## Quick Start

### Build (Linux host for validation)

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
ctest           # 29/29 suites
```

### Build the Xbox AppX

```bash
./scripts/xbox_bringup.sh --package-only   # → build-win/Nemulator_1.0.0.0_x64.appx
```

### Deploy to a Developer-Mode Xbox + run the boot probe

```bash
./scripts/xbox_bringup.sh <XBOX_IP>
```

Set **App Type = Game** in the Xbox Device Portal for full CPU/RAM/GPU access.

### Headless boot verification

```bash
./build/bin/Nemu --run ./sdmc/switch/linux-realboot-sample.nro
# [NEMU-BOOT] frames_executed=3 -> BOOTED (advanced frames)
```

### Tune the 5 GiB texture budget (on-device)

```bash
Nemu --texture-budget=1024   # cap resident textures at 1 GiB
Nemu --texture-budget=2048   # 2 GiB on Series X for high-res atlases
```

---

## Development

The emulator is validated continuously with **29 automated test suites** covering
every subsystem:

| Suite | Focus |
| :--- | :--- |
| `test_gpu` | GMMU, buffer cache, MRT detection, rasterizer state |
| `test_tier_a` | GMMU/BufferCache/Maxwell/shaders/BC1/optimizers/SASS/predicated branches |
| `test_jit` | JIT translator, blocks, fastmem codegen |
| `test_ipc` | Full Horizon IPC service surface + commercial-game syscalls |
| `test_memory_budget` | 5 GiB governor accounting |
| `test_audio` | Ring buffer, **Nintendo DSP ADPCM**, backends |
| `test_save` | Atomic save store, **USB backup/restore** |
| … and 22 more | |

Both the Linux (GCC) and Windows/Xbox (MinGW-w64) cross-builds are kept green;
key PE32+ suites run under Wine for parity.

---

## Documentation

Full architectural, design, and operational documentation lives in **[`docs/`](docs/)**. Key entry points:

| Document | Contents |
| :--- | :--- |
| [`docs/OVERVIEW.md`](docs/OVERVIEW.md) | **Start here** — full system architecture tour |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | System boundaries and component design |
| [`docs/BOOT_READINESS_AUDIT.md`](docs/BOOT_READINESS_AUDIT.md) | Commercial-game load→translate→render chain, verified |
| [`docs/OPTIMIZATION_PLAYBOOK.md`](docs/OPTIMIZATION_PLAYBOOK.md) | 5 GiB-budget optimization research & implementation |
| [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) | Current milestones and gates |
| [`docs/GPU_DESIGN.md`](docs/GPU_DESIGN.md) | NVN/Maxwell3D → D3D12 translation |
| [`docs/KERNEL_DESIGN.md`](docs/KERNEL_DESIGN.md) | Horizon HLE, syscalls, IPC |
| [`docs/XBOX_QUICKSTART.md`](docs/XBOX_QUICKSTART.md) | **3-command Xbox bring-up** |
| [`docs/XBOX_DEPLOYMENT_GUIDE.md`](docs/XBOX_DEPLOYMENT_GUIDE.md) | Manual Device Portal deploy + controller bindings |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Phased milestones |
| [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md) | Current limitations |

---

## License & Legality

**GPL-3.0-licensed.** This project is a clean-room implementation intended
strictly for **legitimate homebrew execution, preservation, and testing of
user-owned software**. It bundles **zero** proprietary Nintendo encryption keys
or copyrighted firmware. Users must supply their own `prod.keys` / `title.keys`
(extracted from consoles they own) and place them in `keys/` to run encrypted
retail media.

---

*Documentation, build, and validation pipeline maintained continuously — see [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) for the live milestone tracker.*