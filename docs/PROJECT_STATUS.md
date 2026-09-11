# Project Status: Nemu

**Current Milestone:** Phase 0 & Phase 1 (Environment Baseline & Architectural Design)  
**Active Gate:** Gate 0 — Environment & Toolchain Baseline  
**Date:** 2026-09-11  

---

## 1. Milestone Tracking

- [x] **Milestone 0: Environment + Repository Audit**
  - Host hardware, compiler, and OS inspection complete.
  - Storage & USB device audit complete (disk preserved, no destructive format).
  - Xbox Series S/X capabilities, memory models, and API availability documented.
- [x] **Milestone 1: Architecture & Technical Specifications**
  - Repository structure initialized with strict subsystem segregation.
  - Complete architectural design documents written for all 8 subsystems.
- [ ] **Milestone 2: Minimum Bootable Core & Reference Interpreter**
  - ARM64 register file, execution state, and instruction decoder.
  - Core arithmetic, logical, control flow, load/store, and system instructions.
  - Unit test harnesses for CPU correctness.
- [ ] **Milestone 3: Memory Subsystem**
  - Guest virtual memory address space (48-bit VA, 4GB/6GB/8GB address spaces).
  - Page table mapping, permissions (`PAGE_READ`, `PAGE_WRITE`, `PAGE_EXECUTE`), and fastmem.
- [ ] **Milestone 4: Kernel & System Services Foundation**
  - Horizon OS HLE: KProcess, KThread, KEvent, KSharedMemory.
  - SVC dispatcher (system calls) and IPC service infrastructure.
- [ ] **Milestone 5: JIT Dynamic Recompiler (x86-64)**
  - Translation block cache, IR, x86-64 code generation, and invalidation handling.
- [ ] **Milestone 6: Filesystem & Content Loader**
  - VFS: PFS0/NSP, NRO, NSO, RomFS, and isolated SaveFS.
- [ ] **Milestone 7: GPU Foundation & Direct3D 12 Backend**
  - Maxwell 3D command processor, DMA channels, buffer caches, and swapchain presentation.
- [ ] **Milestone 8: Shader Translation Pipeline**
  - Maxwell shader bytecode decoding, translation to HLSL, and DXIL runtime compilation.
- [ ] **Milestone 9: Audio Subsystem**
  - Horizon audio renderer emulation, buffer queue, mixing, and XAudio2 output.
- [ ] **Milestone 10: Input Subsystem**
  - Switch HID state mapping, deadzone calculation, and Xbox controller support.
- [ ] **Milestone 11: Xbox Frontend & UI**
  - Native controller-navigated interface, game browser, and settings management.
- [ ] **Milestone 12: Xbox Packaging & Deployment**
  - UWP APPX/MSIX generation, manifest with `runFullTrust`, and Dev Portal sideloading.
- [ ] **Milestone 13: Homebrew Compatibility Testing**
  - Execution of verified open-source Nintendo Switch homebrew binaries.

---

## 2. Gate Verification Status

| Gate | Description | Status | Evidence |
| :--- | :--- | :--- | :--- |
| **Gate 0** | Toolchain & Host Environment | **PASSED** | GCC 13.3.0, MinGW GCC 13, CMake 3.28.3, Ninja 1.13.0 verified |
| **Gate 1** | CPU Reference Interpreter | Pending | Implements instruction-level verification suite |
| **Gate 2** | Virtual Memory & Paging | Pending | Passes memory permission and fastmem stress tests |
| **Gate 3** | Horizon Kernel Services | Pending | Minimum SVC set boots guest process |
| **Gate 4** | JIT Dynamic Recompiler | Pending | JIT differential test matches interpreter bit-for-bit |
| **Gate 5** | Direct3D 12 Graphics Engine | Pending | Clears framebuffer and renders basic 2D/3D geometry |
| **Gate 6** | Audio & Input Subsystems | Pending | Low-latency audio playback and Xbox gamepad input |
| **Gate 7** | Real Switch Homebrew Boot | Pending | Executes homebrew NRO to main loop |
| **Gate 8** | Xbox Hardware Deployment | Pending | Sideloads and launches on Xbox Series S/X Dev Mode |

---

## 3. Subsystem Implementation Health

* **Core (`src/core/cpu`)**: Designing CPU register state and ARM64 instruction decoder.
* **Memory (`src/core/memory`)**: Virtual memory manager architecture designed.
* **Kernel (`src/core/kernel`)**: SVC call table mapped out.
* **Graphics (`src/core/gpu`)**: D3D12 translation strategy defined.
* **Input / Audio (`src/core/hid`, `src/core/audio`)**: State interfaces defined.
* **Platform (`src/platform`)**: Platform abstraction layer separating Linux host and Xbox host.
