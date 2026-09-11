# NEMU: Nintendo Switch Emulator for Xbox Series S/X

Nemu is a high-performance Nintendo Switch emulation platform engineered specifically for **Xbox Series S and Xbox Series X running Developer Mode**, with full support for desktop development, testing, and continuous validation.

## Mission & Principles

- **Xbox Series S/X Native**: Designed directly for the Xbox Developer Mode environment (UWP full-trust / Win32 container, Direct3D 12, XAudio2, Windows.Gaming.Input).
- **Correctness First**: Strict modular architecture separating guest CPU, memory, kernel services, GPU, audio, HID, and virtual filesystem.
- **Dual Execution Engine**: Deterministic reference ARM64 interpreter for correctness verification paired with an Xbox-compatible x86-64 dynamic recompiler (JIT).
- **Direct3D 12 Graphics**: Native Xbox-compatible D3D12 rendering pipeline avoiding unsupported desktop APIs.
- **Legitimate & Clean-Room**: Intended strictly for legitimate homebrew execution, preservation, and testing of user-owned software. Zero proprietary Nintendo encryption keys or firmware copyrighted files bundled.

## Subsystems

| Subsystem | Architecture | Status |
| :--- | :--- | :--- |
| **CPU** | ARMv8-A (AArch64) Interpreter + Dynarec JIT | **Implemented** — interpreter + JIT, FP/vector/atomic supported |
| **Memory** | 4GB/6GB/8GB Virtual Memory Space, Page Tables | **Implemented** — page tables, permission enforcement |
| **Kernel** | Horizon OS HLE (SVC dispatcher, KProcess, KThread, Synchronization) | **Implemented** |
| **GPU** | NVN / Maxwell 3D Command Processor -> Direct3D 12 | **Implemented** — D3D12 + Null backends, GOB swizzle |
| **Audio** | Audio Renderer (NVNFlinger, AudioOut) -> XAudio2 | **Implemented** — XAudio2 + Null backends, SPSC ring |
| **Input** | Switch HID -> Xbox Controller (Windows.Gaming.Input / XInput) | **Implemented** |
| **Filesystem**| VFS (RomFS, PFS0/NSP, NRO, NSO, SaveFS) | **Implemented** — NRO/NSO/LZ4, PFS0/NSP, NCA loaders |
| **Frontend** | Xbox-native Game Browser & Settings UI | **Implemented** |

> **Status legend:** all 8 subsystems are implemented and verified. **15/15 automated
> test suites pass** on native Linux (GCC 13) and Windows/Xbox PE32+ (MinGW-w64 under
> Wine), and a validated Xbox Developer Mode APPX package is produced for sideloading.
> WIP roadmap items tracked in [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Documentation

Full architectural specifications, Xbox capabilities, roadmap, and design documents are available in [`docs/`](docs/):

- [`docs/INITIAL_AUDIT.md`](docs/INITIAL_AUDIT.md) — Environment, toolchain, hardware, and storage audit
- [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) — Current state, active gate, and subsystem milestones
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — Full technical architecture and system boundaries
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — Phased milestones and gating criteria
- [`docs/XBOX_CAPABILITIES.md`](docs/XBOX_CAPABILITIES.md) — Detailed Xbox Dev Mode runtime capabilities & constraints
- [`docs/XBOX_BUILD.md`](docs/XBOX_BUILD.md) — Packaging, build pipeline, and side-loading procedures
- [`docs/CPU_DESIGN.md`](docs/CPU_DESIGN.md) — ARM64 decoder, register state, and interpreter design
- [`docs/MEMORY_DESIGN.md`](docs/MEMORY_DESIGN.md) — Virtual memory, paging, address translation, and synchronization
- [`docs/GPU_DESIGN.md`](docs/GPU_DESIGN.md) — NVN/Maxwell 3D pipeline, command processing, and D3D12 translation
- [`docs/JIT_DESIGN.md`](docs/JIT_DESIGN.md) — Recompilation, block cache, and Xbox executable memory execution
- [`docs/KERNEL_DESIGN.md`](docs/KERNEL_DESIGN.md) — Horizon OS HLE syscalls, thread scheduling, and IPC
- [`docs/AUDIO_DESIGN.md`](docs/AUDIO_DESIGN.md) — Audio core, mixing, buffers, and low-latency output
- [`docs/INPUT_DESIGN.md`](docs/INPUT_DESIGN.md) — Xbox controller integration, deadzones, and HID mapping
- [`docs/TESTING.md`](docs/TESTING.md) — Unit, integration, differential, and regression testing strategy
- [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md) — Metrics, budgets, profiling hooks, and frame pacing
- [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md) — Active limitations, edge cases, and tracked bugs
- [`docs/DECISIONS.md`](docs/DECISIONS.md) — Architecture Decision Records (ADR) log
- [`docs/AGENT_HANDOFF.md`](docs/AGENT_HANDOFF.md) — Hermes <-> AGY collaboration handoff log

## Building Nemu

See [`docs/XBOX_BUILD.md`](docs/XBOX_BUILD.md) for build instructions on Linux host and Xbox deployment.
