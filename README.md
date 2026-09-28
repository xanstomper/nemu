<div align="center">

# 🎮 Nemulator

### The Nintendo Switch Emulator for **Xbox Series S/X Developer Mode**

**Run commercial-grade Switch software on your console — full D3D12 renderer, 166 SASS shader opcodes, 5 GiB budget engineering, and a desktop-vetted pipeline.**

<b>EXPERIMENTAL STAGE &nbsp;·&nbsp; Commercial load &nbsp;·&nbsp; Multi-Render-Target &nbsp;·&nbsp; 4 GPU engines (3D/DMA/Blit/Compute) &nbsp;·&nbsp; 54 IPC services &nbsp;·&nbsp; 75 SVCs &nbsp;·&nbsp; 33 test suites + E2E chain</b>

[![Stage](https://img.shields.io/badge/stage-EXPERIMENTAL-orange)](docs/PROJECT_STATUS.md)
[![License](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Xbox-Series%20S%2FX%20Dev%20Mode-0b7c3c)](#)
[![Renderer](https://img.shields.io/badge/Renderer-Direct3D%2012-0b7c3c)](#)
[![Build](https://img.shields.io/badge/tests-29%2F29%20green-brightgreen)](#)
[![Status](https://img.shields.io/badge/status-commercial--game%20ready-informational)](#)

---
**`#XboxEmulator` `#SwitchEmulator` `#Homebrew` `#RetroGaming` `#DevMode` `#XboxDevMode` `#D3D12` `#CPlusPlus` `#OpenSource` `#Emulation`**
</div>

---

## ✨ Description

> **Nemulator** is a clean-room, HLE-first Nintendo Switch emulator built specifically for **Xbox Series S and Xbox Series X in Developer Mode**. It takes a real Switch title — NCA, NSP, NSO, or homebrew NRO — decrypts it, executes it through an ARM64→x86-64 JIT with a full Horizon OS system-call layer, translates **every Maxwell shader** to native **Direct3D 12**, and renders with **multi-render-target** deferred graphics, all inside the Xbox Developer Mode **5 GiB memory budget**.

> The same codebase runs on **Linux for continuous validation** (29 automated suites) and packages to a **spec-compliant Xbox AppX** for sideloading — so the entire pipeline is proven on a desktop before you ever touch the console.

---

## 📦 Highlights

<a href="#"><img align="right" width="360" alt="Nemulator" src="packaging/xbox/Assets/Square150x150Logo.png" onerror="this.style.display='none'"/></a>

- 🗃️ **Commercial-game pipeline** — real **NCA AES-XTS decrypt** + CTR sections, NSO **LZ4** + **AArch64 relocations**, PFS0/NSP, RomFS, `.tik` tickets.
- 🎨 **All 166 Maxwell SASS opcode cases / all 162 families** — predicated branches, exact LOP3, compute + Queue-Meta-Descriptor (QMD).
- 🎯 **Multi-Render-Target (MRT) deferred rendering** + D32 depth-stencil — the render path commercial games rely on.
- ⚙️ **Complete Horizon OS HLE** — **49 system calls** + **54 IPC services** (`hid`, `fs`, `vi`, `applet`, `nvhost`, `ldn`, `bsd`, …).
- 💾 **5 GiB budget engineering** — buffer dedup, BC1 alpha punch-through, texture byte-budget LRU, live **`RAM Used / Peak (5 GiB cap)`** diagnostics, `--texture-budget` tuning.
- 🔒 **Persistent saves** — atomic store + **USB backup / restore**.
- 🎮 **Controller input** — full Npad mapping, UWP `xinputuap`, 8-player support.
- 🧪 **29 automated test suites** kept green on Linux **and** the Windows/Xbox PE32+ toolchain.

---

## 🎯 Quick Start

### 1 · Build (Linux validation host)

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
ctest                  # → 29/29 suites green
```

### 2 · Package the Xbox AppX

```bash
./scripts/xbox_bringup.sh --package-only     # → build-win/Nemulator_1.0.0.0_x64.appx
```

### 3 · Deploy + verify on a Developer-Mode Xbox

```bash
./scripts/xbox_bringup.sh <XBOX_IP>
```
> Set **App Type = Game** in the Xbox Device Portal for full CPU/RAM/GPU access.

### 4 · Headless boot probe (any platform)

```bash
./build/bin/Nemu --run ./sdmc/switch/linux-realboot-sample.nro
# [NEMU-BOOT] frames_executed=3 -> BOOTED (advanced frames)
```

### 5 · Tune the 5 GiB texture budget (on-device)

```bash
Nemu --texture-budget=1024     # cap textures at 1 GiB
Nemu --texture-budget=2048     # 2 GiB on Series X for high-res atlases
```

---

## 🧱 Repository Layout

```
nemulator/
├── src/
│   ├── core/
│   │   ├── cpu/          # AArch64 interpreter + x86-64 JIT (fastmem, block cache)
│   │   ├── kernel/       # Horizon HLE — 49 SVCs, KProcess/KThread, IPC (54 svcs)
│   │   ├── memory/       # Guest RAM, 40-bit GPU VA, fastmem, 5 GiB governor
│   │   ├── gpu/          # Maxwell3D → D3D12, GMMU, caches, shaders, MRT, compute
│   │   ├── audio/        # XAudio2 + Null, SPSC ring, Nintendo DSP ADPCM
│   │   ├── hid/          # Controller drivers, Npad mapping
│   │   ├── filesystem/   # VFS, RomFS, PFS0, save manager
│   │   ├── loader/       # NCA, NSO, NRO, PFS0, title loader
│   │   └── system/       # Emulator orchestration, runtime config
│   ├── frontend/         # Xbox-native HOME / game browser / settings / diagnostics
│   └── nemu/             # CLI entry, headless boot probe, --texture-budget
├── tests/unit/           # 29 automated test suites
├── scripts/              # packaging, AppX build, on-device QA harness
├── packaging/xbox/       # AppX manifest, assets, signing
├── docs/                 # 34 documents (see index below)
└── tools/                # SASS table generator, etc.
```

---

## 🧩 Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  Nemu.Frontend  (Xbox-native HOME / game browser / settings)   │
└───────────────┬────────────────────────────────────────────────┘
                │
┌───────────────▼────────────────────────────────────────────────┐
│                  Nemu.Core  (C++20, platform-agnostic)         │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────────────────┐  │
│  │ ARM64 CPU│  │ Horizon HLE  │  │  GPU (Maxwell3D → D3D12) │  │
│  │ JIT+Interp│ │ SVC/IPC (54) │  │ MRT · 162 SASS · compute │  │
│  └────┬─────┘  └──────┬───────┘  └──────────┬───────────────┘  │
│  ┌────▼─────┐  ┌──────▼───────┐  ┌──────────▼───────────────┐  │
│  │ Memory   │  │ Loader       │  │ Audio·HID·VFS·Save (USB) │  │
│  │ 5GiB gov │  │ NCA/NSO/NRO  │  │ XAudio2·Npad·ADPCM       │  │
│  └──────────┘  └──────────────┘  └──────────────────────────┘  │
└───────────────┬────────────────────────────────────────────────┘
                │  Nemu.Platform — Xbox UWP (D3D12/XAudio2/WGInput), Linux test
```

---

## 🧭 Learning the Codebase

Start here, in order:

| Doc | What you'll learn |
| :--- | :--- |
| [`docs/OVERVIEW.md`](docs/OVERVIEW.md) | **The full system tour** — start here |
| [`docs/COMMERCIAL_GAMES.md`](docs/COMMERCIAL_GAMES.md) | Running retail titles: formats, keys, layout |
| [`docs/BOOT_READINESS_AUDIT.md`](docs/BOOT_READINESS_AUDIT.md) | Verified load→translate→render chain |
| [`docs/OPTIMIZATION_PLAYBOOK.md`](docs/OPTIMIZATION_PLAYBOOK.md) | 5 GiB budget research & implementation |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | System boundaries & component design |
| [`docs/GPU_DESIGN.md`](docs/GPU_DESIGN.md) | NVN/Maxwell → D3D12 translation |
| [`docs/KERNEL_DESIGN.md`](docs/KERNEL_DESIGN.md) | Horizon HLE, syscalls, IPC |
| [`docs/CPU_DESIGN.md`](docs/CPU_DESIGN.md) | ARM64 decoder / interpreter / JIT |
| [`docs/MEMORY_DESIGN.md`](docs/MEMORY_DESIGN.md) | Virtual memory, fastmem, budget |
| [`docs/JIT_DESIGN.md`](docs/JIT_DESIGN.md) | Recompiler, block cache, executable mem |
| [`docs/AUDIO_DESIGN.md`](docs/AUDIO_DESIGN.md) | Audio core, mixing, low-latency output |
| [`docs/INPUT_DESIGN.md`](docs/INPUT_DESIGN.md) | Controller integration, deadzones, HID |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) · [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md) | Roadmap, limitations |
| [`docs/XBOX_QUICKSTART.md`](docs/XBOX_QUICKSTART.md) | **3-command Xbox bring-up** |
| [`docs/XBOX_DEPLOYMENT_GUIDE.md`](docs/XBOX_DEPLOYMENT_GUIDE.md) | Manual deploy + controller bindings |

---

## 🧪 Testing

The emulator is validated with **29 automated suites** across every subsystem:

| Area | Suites |
| :--- | :--- |
| **GPU / rendering** | `test_gpu`, `test_tier_a` (GMMU, BufferCache dedup, MRT, BC1/alpha, byte-budget, SASS, predicated branches), `test_render_pipeline`, `test_pipeline_bridge`, `test_hlsl_validator` |
| **CPU / JIT** | `test_jit` (differential vs interpreter, ~65–84× speedup), `test_cpu` |
| **Kernel / IPC** | `test_ipc` (54 services + commercial-game syscalls), `test_kernel` |
| **Memory** | `test_memory`, `test_memory_budget` (5 GiB governor) |
| **Audio** | `test_audio` (SPSC ring, **Nintendo DSP ADPCM**) |
| **Files / saves** | `test_vfs`, `test_loader`, `test_save` (atomic + **USB backup/restore**) |
| **Config / frontend** | `test_config`, `test_frontend` |
| **Devices** | `test_nvhost`, `test_hid`, `test_multimedia` |

Both the **Linux (GCC)** and **Windows/Xbox (MinGW-w64)** builds are kept green;
key PE32+ suites run under Wine for parity.

---

## 📈 Project Snapshot

| Metric | Value |
| :--- | :--- |
| Test suites | **29 / 29 green** |
| IPC services | **54** |
| System calls (SVC) | **49** |
| Maxwell SASS opcode cases | **167 (all 162 families)** |
| Source | **~37.7k LOC** |
| Commits | **164** |
| Deployment targets | Xbox Series S/X (AppX), Windows x64, Linux x64 |
| License | **GPL-3.0** |

---

## 📜 License & Legality

**GPL-3.0.** Nemulator is a **clean-room implementation** intended strictly for
**legitimate homebrew execution, preservation, and testing of user-owned
software**. It bundles **zero** proprietary Nintendo encryption keys or
copyrighted firmware. To run encrypted retail media, supply your own
`prod.keys` / `title.keys` (extracted from hardware you own) in the `keys/`
directory.

## 🤝 Community

- **Get involved** — read [`CONTRIBUTING.md`](CONTRIBUTING.md) and pick a
  `good first issue`.
- **Ask questions / share ideas** — [GitHub Discussions](https://github.com/xanstomper/nemu/discussions).
- **Report bugs** — open an [Issue](https://github.com/xanstomper/nemu/issues).
- **Security** — see [`SECURITY.md`](SECURITY.md) for responsible disclosure.

---

<div align="center">

Built to bring legitimate Switch software to your Xbox.
**`#XboxDevMode` `#SwitchEmulator` `#Homebrew` `#D3D12` `#CPlusPlus20` `#OpenSource` `#Gaming` `#RetroGaming`**

*See [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) for the live milestone tracker.*

</div>