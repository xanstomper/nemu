# Nemu: Real-Game-on-Xbox Guarantee — Research-Backed Gap Assessment

Goal: take Nemu from "synthetic unit tests pass" to **real Switch games are
confirmed to work on Xbox Series S/X Developer Mode**, using engineering
patterns proven by mature Switch emulators (Yuzu, Ryujinx) and porting whatever
the license permits.

> **Licensing (see `docs/PORT_AUDIT.md`):** Nemu is GPL-3.0-or-later. That means
> inbound **MIT/Apache/BSD** (e.g. **Ryujinx is MIT**) and **GPL-3.0** (e.g.
> **Strato/hikari**) code may be **legally ported into Nemu** with attribution.
> This drastically lowers the cost of closing the Tier-A gaps below — the full
> GPU shader compiler, kernel/IPC service fidelity, and JIT backend do not have
> to be built from scratch. Only Nintendo-proprietary material (keys/firmware/
> assets) is off-limits; keys are user-provided at runtime (gitignored).

## 1. Reality check: what "works" means vs. what Nemu is today

Nemu's committed stack (CPU interpreter+JIT, HLE kernel+IPC, loader for
NRO/NSO/NCA/PFS0/RomFS, and a D3D12 GPU translation path that renders
synthesized/software-fed geometry) is a **solid foundation**, but it is not yet
a *Switch-game-capable* emulator. The gap is not a single bug; it is a
different fidelity tier. The proven emulators got there through years of a
specific set of subsystems Nemu does not yet have in real-game form.

## 2. What actually makes a Switch game run (from Yuzu/Ryujinx architecture)

The mature renderers share a layered design Nemu already partially mirrors:

```
Game (nvn graphcs -> gpu0 pushbuffer / nvhost)
   -> NVN / nvgpu HLE + Maxwell3D state machine       [Nemu: partial]
   -> RasterizerInterface (draw/indirect/instanced/
        compute/clear)                                 [Nemu: DrawArrays/DrawIndexed only]
   -> BufferCache (vertex/index/UBO/SSBO, CPU<->GPU)   [Nemu: vertex+index only]
   -> TextureCache (decompress ASTC, format map)       [Nemu: present, validation pending]
   -> ShaderCompiler GPU -> Host (SM/decompiler)       [Nemu: minimal decoder]
   -> Host backend (D3D12/Vulkan) + StateTracker       [Nemu: D3D12 CBV/SRV/PSO present]
   -> Swapchain / presentation                          [Nemu: present]
```

**The single biggest determiner of "will a real game boot and render": a
complete Maxwell 3D command state machine + a faithful GPU shader compiler.**
Nemu's Maxwell3D defines ~39 methods and handles ~28 (of the ~30k register
space a real Maxwell exposes, with only a handful fully interpreted), and its
shader decoder covers 25 of 41 declared opcodes with a screen-2D `RasterVertex`
geometry contract. A real retail game issues hundreds of distinct Maxwell
methods, uses instanced/indirect/compute draws, reads/writes global GPU memory
(GMMU), and ships shaders using the full SM 5.0 opcode set. That is the tier
difference.

## 3. Ranked gaps to close for a guaranteed real-game boot (and how proven code does it)

> **Porting strategy (per `PORT_AUDIT.md`):** These Tier-A/B gaps map to code
> Nemu can legally *inbound-port* rather than rewrite:
> - **Ryujinx (MIT):** GPU shader decompiler + "theendianjon"/shader IR,
>   `Graphics/Common` maxwell decode, **ARMeilleure** JIT (far beyond Nemu's
>   dynarec), `HLE` service implementations, file-format/NRO-NSO-NCA parsing.
> - **Strato/hikari (GPLv3):** kernel/IPC/FS/audio, ARM64 JIT.
> - **MIT ASTC decoders, etc.:** texture codecs.
> Port smallest high-value units behind Nemu's existing interfaces, add
> `third_party/` with LICENSE/NOTICE, and rerun the per-subsystem tests — the
> same pipeline that already keeps 27/27 green.

### Tier A — Boot-blocking (a retail title will not even reach its first frame)
| # | Gap | Nemu today | Proven approach to port |
| :--- | :--- | :--- | :--- |
| A1 | **GPU shader compiler** | 25/41 opcodes, 2D output | Port Ryujinx Maxwell decoder + shader IR → HLSL/DXIL (MIT). Largest single item, and now a port, not a rewrite. |
| A2 | **Maxwell3D method surface** | ~10 methods, 2D hand-fed draws | Complete method table (0x000-0xE00): blend, depth/stencil, alpha test, MSAA, scissor multi, viewport transforms, vertex formats, rasterizer config, stream-out, compute (launch grid), indirect params. |
| A3 | **Compute / dispatch path** | absent | `DispatchCompute` + host compute PSO; games use compute for postFX, shadows, GPU particle, denoise. Without it most retail titles render black/incorrectly. |
| A4 | **Buffer cache** (UBO/SSBO/vertex/index, CPU<->GPU sync) | vertex+index only | Full buffer-cache with dirty-range tracking + streaming, as in yuzu's BufferCache. Games use uniform buffers constantly. |
| A5 | **GMMU / global GPU memory** (LDG/STG, compute) | none | GPUMemory manager mapping guest physical->global, for shader global/SSBO/compute. |

### Tier B — Correctness-and-compat (boots, may be slow/glitchy without)
| # | Gap | Nemu today | Proven approach |
| :--- | :--- | :--- | :--- |
| B1 | **ASTC decode performance** | software 4x4/... present | Decompress once to BC1/BC3 and cache (yuzu ASTC recompression); software-only 34x blowup will not hold framerate. |
| B2 | **Shader cache / PPTC** | PSO cache only | Persistent shader (re)compilation cache to avoid per-launch stutter (Ryujinx PPTC, yuzu disk shader cache). |
| B3 | **Multi-viewport / MSAA / framebuffer layers** | 1 viewport, 1 RT | Framebuffer manager supporting multiple render targets + downsample/resolve (yuzu's framebuffer cache). |
| B4 | **HLE service completeness** (fsp-srv file/romfs, vi, am, applet, ldr, timesrv, settings) | core subset | Flesh out IPC service replies for common game services; boot-critical ones first (see REALBOOT_PLAN). |

### Tier C — Can't-be-guaranteed-via-code-only (honest limits)
| # | Item | Reality |
| :--- | :--- | :--- |
| C1 | **Retail keys** | Real titles need user-provided `prod.keys`/`title.keys`. Nemu has the crypto + loader plumbing but must not bundle keys. |
| C2 | **On-device D3D12 runtime** | `D3DCompile`/`CreateGraphicsPipelineState` must be proven on real Xbox hardware (this Linux box/Wine cannot). This is the one gate no amount of desktop testing closes. |
| C3 | **RAM ceiling** | Xbox Dev Mode ~5-6 GB is tight; 34x ASTC or unbounded buffer caches can exceed it. Must enforce cache eviction + advanced-resource UWP limits. |

## 4. Porting-map: which proven-project patterns map directly

- **Layered backend + StateTracker** → Nemu's `IGpuBackend` + frontend; extend
  `RasterizerInterface`-style method surface (A2/A3).
- **Dirty-range buffer cache + staging pool** → `TextureCache`'s sibling
  `BufferCache`; yuzu/Vulkan `VkStagingBufferPool` pattern.
- **ASTC recompression + cache** → ASTC→BC1/BC3 on upload (A4/B1).
- **Persistent translation cache** → reuse `PipelineCache` keying + disk
  persistence (B2).
- **Window/context agnostic renderer** → Nemu already presents via swapchain;
  keep Null backend for headless CI (already done).

## 5. Recommended sequencing to *guarantee* a real game on Xbox

1. **Close A1 (shader compiler) + A4 (buffer cache)** — these unlock real guest
   draws. Deliverable: load a real homebrew NVN sample (deko3d/libnx) and render
   its actual frame through the D3D12 path.
2. **Close A2/A3 (Maxwell state + compute)** — enough for a first retail title to
   reach a real frame (target: a known-lightweight title booting to its menu).
3. **Close B-item** correctness/perf; get `Nemu.exe` + APPX onto a real Xbox.
4. **C2 is the unavoidable on-device gate** — no substitute; schedule a real
   hardware bring-up with a concrete NRO on the console.

### Recommended first port (do this first — smallest, highest boot-payoff)

A real game's libnx `crt0` calls `__nx_appletInit` → `appletInitialize`,
`fsInitialize`, `timeInitialize`, `setInitialize`, `hidInitialize` before its
first frame. Nemu's IPC services exist but several commands return
`IpcResult::Unimplemented`. **Verified stub distribution (2026-09-14):**
`applet_service` (3), `fsp_srv_service` (4), `time_service` (2), `hid_service`
(1); `set:sys` is essentially implemented (GetFirmwareVersion/GetLanguageCode
reply correctly) — **not** a stub target. Filling the applet/fs/time/hid
replies exactly is the **biggest single boot win** and the smallest port.

**Task:** port permissive (Ryujinx-MIT) or GPLv3 (Strato) implementations of the
*called-with-success-reply* commands in:
- `src/core/kernel/ipc/applet_service.cpp` (IApplicationFunctions / appletInit)
- `src/core/kernel/ipc/fsp_srv_service.cpp` (IFileSystem/IFile open+read+getsize,
  needed to load the game's own RomFS/assets)
- `src/core/kernel/ipc/time_service.cpp` (clock reads the CRT0 uses)
- `src/core/kernel/ipc/hid_service.cpp` (CreateAppletResource shared mem)
- `set_sys_service.cpp` is largely implemented already — only verify its
  reply encodings against a real game's expectations, no large port needed.

Contract: keep Nemu's `IpcService` interface; only replace the *reply encoding*
inside each handler; add a unit test per command mirroring the existing
`tests/unit/ipc/test_ipc.cpp` style; dead-code-free, `-Wall -Wextra -Wpedantic`
clean; buddy-build Linux + Windows cross. This is the A-line that most quickly
gets a *real* NRO past crt0, at which point the unfilled shader/GPU tier
becomes the visible next blocker instead of a mystery lock.

## 6. Verdict

Nemu is architecturally on the right track (layered backend, HLE, loader, GPU
translation present), and everything consolidated here is green on both Linux
and Windows PE32+. But "guaranteed real-game-on-Xbox" is **not yet achievable by
testing alone** — it is blocked by the Tier-A fidelity tier (shader compiler,
complete Maxwell state, compute, buffer/GMMU). Those are large, multi-week
work items that the proven emulators solved over years. The honest path is:
port the *patterns* above in the A→B order, validate against real homebrew NVN
samples, and ultimately prove it on hardware. No amount of synthetic tests
closes C2.