# NEMU Engineering Specification

## 1. Target Environment
- **Hardware Targets**: Microsoft Xbox Series S and Xbox Series X consoles
- **Execution Mode**: Xbox Developer Mode (Universal Windows Platform / UWP Win32 Full Trust Container)
- **Host CPU**: AMD Zen 2 8-core / 16-thread x86-64 CPU
- **Host GPU**: AMD RDNA 2 GPU with Direct3D 12 Feature Level 12_1 / 12_2
- **Resource Constraints**:
  - Requires UWP `<rescap:Capability Name="runFullTrust" />` and `<rescap:Capability Name="expandedResources" />`
  - When configured as a "Game" in Xbox Device Portal: access to 6-8 dedicated CPU cores and up to 5-13 GB usable RAM
  - When running as an "App": limited to 4 CPU cores and ~2 GB RAM (deprecated configuration)

---

## 2. Emulated Guest Architecture
- **Guest CPU**: Quad-core ARM Cortex-A57 (ARMv8-A 64-bit AArch64)
- **Guest Virtual Memory**: 48-bit virtual address space, 4 KiB page granularity
- **Guest Operating System**: Nintendo Horizon OS High-Level Emulation (HLE)
  - Process management (`KProcess`, `KThread`)
  - Handle tables (`KHandleTable`, `KAutoObject`)
  - Synchronization primitives (`KEvent`, `KSynchronizationObject`)
  - Horizon Supervisor Call (SVC) dispatcher
- **Guest GPU**: NVIDIA Tegra X1 GM20B (Maxwell 3D architecture)
  - Method-based command pushbuffers
  - 16-GOB block-linear texture formatting
- **Guest Audio**: 48,000 Hz, 16-bit signed stereo PCM audio
- **Guest Input**: Nintendo Switch Pro Controller and Joy-Con HID shared memory ring buffers

---

## 3. Host Subsystem Implementations

### 3.1 CPU Execution Engine
- **Reference Interpreter (`src/core/cpu/interpreter.cpp`)**:
  - Deterministic step-by-step instruction execution
  - Serves as the ground-truth correctness oracle for testing
- **Dynamic Recompiler / JIT (`src/core/cpu/jit/`)**:
  - Compiles ARM64 basic blocks directly to native x86-64 machine code
  - 16 MiB executable code cache allocated via `VirtualAlloc(PAGE_EXECUTE_READWRITE)` on Xbox and `mmap` RWX on Linux
  - Complies with Microsoft x64 and System V AMD64 calling conventions
  - Differential validation against interpreter ensures 100% bit-exact register matching

### 3.2 Graphics Subsystem
- **Direct3D 12 Backend (`src/core/gpu/d3d12/`)**:
  - Native Direct3D 12 pipeline (`ID3D12Device`, `ID3D12CommandQueue`, swap chain, fences)
  - Xbox Dev Mode does NOT support Vulkan ICD; D3D12 is the primary native graphics API
- **Block-Linear Texture Pipeline**:
  - Hardware-accurate GM20B deswizzling for block-linear render targets and textures
- **Null Backend (`src/core/gpu/null_backend.cpp`)**:
  - Fast headless execution for continuous integration and automated verification

### 3.3 Audio Subsystem
- **Pipeline (`src/core/audio/`)**:
  - Lock-free Single-Producer Single-Consumer (SPSC) circular ring buffer
  - `XAudio2Backend`: Native hardware mastering voice and source voice rendering for Xbox Dev Mode and Windows
  - `NullAudioBackend`: Headless simulation backend for Linux CI

### 3.4 Input Subsystem
- **Mapping Engine (`src/core/hid/`)**:
  - Physical Xbox Wireless Controller inputs mapped to Switch Pro Controller shared memory ring buffers
  - Configurable face button layouts (`NintendoStandard` vs `XboxMirrored`)
  - Radial deadzone filtering with inner and outer deadzone threshold scaling
  - Supports up to 8 concurrent players

### 3.5 Storage & Filesystem
- **Sandboxed Virtual File System (`src/core/filesystem/`)**:
  - Sandboxed path routing: `sdmc:/`, `save:/`, `romfs:/`
  - Traversal attack protection rejecting `../` path escapes
- **Save Data Subsystem (`src/core/save/`)**:
  - Isolated per-title save storage (`save:/<title_id>/<filename>`)
  - Atomic staged writes via `.tmp` files
  - 64-bit FNV-1a checksum verification footers
  - Automatic `.bak` backup rotation and transparent recovery from corrupted primary saves
- **Configuration Management (`src/core/config/`)**:
  - Persistent INI format at `save:/config.ini`
  - Configurable resolutions, VSync, audio volume, deadzones, and CPU execution backend (JIT vs Interpreter)

### 3.6 Diagnostics & Frontend
- **Crash Reporting (`src/core/debug/`)**:
  - Automated post-mortem crash diagnostics (`save:/crashes/crash_<timestamp>.txt`) recording PID, TID, faulting virtual address, fault reason, and complete guest CPU register dump
- **Frontend UI (`src/frontend/`)**:
  - Xbox controller navigable interface: Homebrew Library browser, Settings toggles, Diagnostics view, and GPU UI rendering

---

## 4. Build, Packaging & Verification Targets
- **Dual-Target Native / Cross-Compilation**:
  - Linux Host: GCC 13+ / Clang 17+, CMake 3.28+, Ninja
  - Windows / Xbox: MinGW-w64 GCC 13+ (x86-64), CMake 3.28+, Ninja
- **Packaging Container**:
  - UWP APPX container: `build-win/Nemu_1.0.0.0_x64.appx`
- **Validation**:
  - 13 comprehensive unit test suites passing 100% on both Linux and Windows/Xbox PE32+ (Wine)
