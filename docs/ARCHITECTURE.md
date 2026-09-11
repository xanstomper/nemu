# Nemu Architecture Specification

## 1. System Overview

Nemu is structured into three primary architectural tiers:
1. **Nemu.Core**: Hardware and operating-system emulation logic (platform-agnostic C++20).
2. **Nemu.Platform**: Host OS abstraction layer (Linux host for development/testing; Windows/UWP for Xbox Dev Mode).
3. **Nemu.Frontend**: User presentation, gamepad navigation, game library management, and developer diagnostics.

```
┌─────────────────────────────────────────────────────────────────┐
│                          Nemu.Frontend                          │
│     (Xbox Controller Navigation, Game Browser, Settings)        │
└────────────────────────────────┬────────────────────────────────┘
                                 │
┌────────────────────────────────▼────────────────────────────────┐
│                           Nemu.Core                             │
│ ┌───────────────┐ ┌───────────────┐ ┌─────────────────────────┐ │
│ │  ARM64 CPU    │ │ Memory (MMU)  │ │   Horizon Kernel HLE    │ │
│ │ (Interp/JIT)  │ │ (Page Tables) │ │ (Threads/Sync/IPC/SVC)  │ │
│ └───────┬───────┘ └───────┬───────┘ └────────────┬────────────┘ │
│         │                 │                      │              │
│ ┌───────▼───────┐ ┌───────▼───────┐ ┌────────────▼────────────┐ │
│ │  Maxwell GPU  │ │  Audio Core   │ │   HID & Controller      │ │
│ │  (Command/3D) │ │ (Render/DSP)  │ │ (Npad/Vibration/Motion) │ │
│ └───────┬───────┘ └───────┬───────┘ └────────────┬────────────┘ │
│         │                 │                      │              │
│ ┌───────▼─────────────────┴──────────────────────▼────────────┐ │
│ │                  Virtual Filesystem (VFS)                   │ │
│ │          (PFS0/NSP, NRO, NSO, RomFS, Isolated SaveFS)       │ │
│ └──────────────────────────────┬──────────────────────────────┘ │
└────────────────────────────────┼────────────────────────────────┘
                                 │
┌────────────────────────────────▼────────────────────────────────┐
│                         Nemu.Platform                           │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │                      Xbox Series S / X                      │ │
│ │ Direct3D 12  │   XAudio2   │ Windows.Gaming.Input │ Win32/UWP│ │
│ ├─────────────────────────────────────────────────────────────┤ │
│ │                     Linux Host (Test)                       │ │
│ │   Vulkan/GL  │    Pulse    │      evdev / SDL2    │  POSIX   │ │
│ └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Subsystem Boundaries & Invariants

### 2.1 CPU Subsystem (`src/core/cpu`)
* **Guest Architecture:** ARMv8.0-A (64-bit AArch64 little-endian).
* **State Representation:**
  * 31 General-purpose 64-bit registers (`X0`..`X30`, with `W0`..`W30` views).
  * Program Counter (`PC`) and Stack Pointer (`SP_EL0`).
  * Process State (`PSTATE` / `NZCV`).
  * 32 128-bit Vector/FP registers (`V0`..`V31` with `Q`, `D`, `S`, `H`, `B` views).
  * System registers (`TPIDRRO_EL0`, `TPIDR_EL0`, `CNTFRQ_EL0`, `CNTPCT_EL0`).
* **Execution Engines:**
  * **Reference Interpreter:** Pure deterministic C++20 switch/table-driven instruction interpreter. Strictly validates condition flags, arithmetic overflows, and memory barriers. Used for regression testing and differential verification.
  * **Dynarec JIT:** Translates guest basic blocks into host x86-64 machine code via direct translation or an intermediate representation. Employs a thread-safe Code Cache with hash-indexed block lookup.

### 2.2 Memory Subsystem (`src/core/memory`)
* **Guest Address Space:** 48-bit Virtual Addressing, supporting standard 32-bit (4 GB) and 36-bit / 39-bit (6 GB / 8 GB) layouts.
* **Page Management:** 4 KiB and 64 KiB page granularities.
* **Page Table Design:** Multi-level radix tree indexing page descriptors:
  * Bit 0: Valid/Present
  * Bit 1: Readable
  * Bit 2: Writable
  * Bit 3: Executable
  * Host backing pointer (or null for unmapped).
* **Fastmem Support:** Employs a continuous 64-bit virtual reservation where host memory operations directly map to `GuestBase + GuestVA`, backed by OS signal/structured-exception handlers for memory fault trapping.

### 2.3 Kernel & OS Subsystem (`src/core/kernel`)
* **Emulation Type:** High-Level Emulation (HLE) of Nintendo Horizon OS (microkernel microservices).
* **Core Kernel Objects:**
  * `KProcess`: Owns the virtual address space, handle table, and process memory regions.
  * `KThread`: Guest thread state, user stack, priority, and TLS pointer.
  * `KSynchronizationObject`: Base for `KEvent`, `KMutex`, `KConditionVariable`, `KSemaphore`.
  * `KSharedMemory`: Maps shared physical memory across processes/services.
* **Supervisor Calls (SVC):**
  * `svcSetHeapSize`, `svcAllocateMemory`, `svcMapMemory`, `svcUnmapMemory`.
  * `svcCreateThread`, `svcStartThread`, `svcExitThread`, `svcSleepThread`.
  * `svcWaitSynchronization`, `svcSignalEvent`, `svcClearEvent`.
  * `svcCloseHandle`, `svcResetSignal`.
  * `svcConnectToNamedPort`, `svcSendSyncRequest`.

### 2.4 GPU Subsystem (`src/core/gpu`)
* **Guest Hardware:** NVIDIA Tegra X1 (Maxwell 3D architecture, GM20B).
* **Host API:** Direct3D 12 (Agility SDK / Feature Level 12_1 on Xbox Series S/X).
* **Architecture:**
  * **FIFO / Host1x:** Ingests GPU pushbuffers and subchannel commands.
  * **Maxwell 3D Engine:** Tracks state machine (vertex buffers, index buffers, shader stages, rasterizer state, blend state, depth/stencil).
  * **Buffer Cache:** Manages guest CPU/GPU synchronized buffers, staging uploads, and dirty-range tracking.
  * **Texture Cache:** Tracks Maxwell surface descriptors, format translation (e.g. ASTC, BC1-BC7, RGBA8), mipmaps, and render target transitions.
  * **Presentation:** Manages D3D12 swapchain flip model with hardware VSync pacing.

### 2.5 Audio Subsystem (`src/core/audio`)
* **Guest Service:** `audren:u` (Audio Renderer) and `audout:u` (Audio Out).
* **Host Engine:** XAudio2 2.9 (Xbox native) and WASAPI / PulseAudio (host development).
* **Processing:** Multi-channel PCM mixing (48 kHz 16-bit stereo/5.1 surround), double/ring buffering, and drift correction.

### 2.6 Input Subsystem (`src/core/hid`)
* **Guest Service:** `hid` (Human Interface Device).
* **State Management:** Emulates standard Switch Pro Controller / Joy-Con pairs. Updates shared memory ring buffers consumed by guest titles.
* **Host Mapping:** Native `Windows.Gaming.Input` / XInput integration with configurable analog stick deadzones, trigger thresholds, and button mapping.

### 2.7 Virtual Filesystem (`src/core/filesystem`)
* **Formats Supported:**
  * `NRO`: Nintendo Relocatable Object (homebrew executables).
  * `NSO`: Nintendo Shared Object (executable modules).
  * `PFS0 / NSP`: Partition Filesystem packages.
  * `RomFS`: Embedded read-only resource filesystem.
  * `SaveFS`: Atomic, title-isolated user save directories.
* **Security & Isolation:** Filesystem accesses are strictly rooted within emulated sandbox directories. No path traversal escapes the root.

---

## 3. Threading & Synchronization Model

* **Emulation Thread (CPU):** Dedicated thread per emulated guest core running interpreter or dynarec blocks.
* **GPU Thread:** Dedicated command processing thread executing Maxwell 3D draw translation and D3D12 command list recording.
* **Audio Thread:** Real-time periodic callback thread updating audio mix buffers.
* **Frontend / Main Thread:** Dispatches window messages, input sampling, UI rendering, and orchestration.
