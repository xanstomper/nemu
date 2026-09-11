# Xbox Series S/X Developer Mode Capabilities & Architectural Constraints

## 1. Introduction

Xbox Developer Mode transforms retail Xbox Series S and Series X consoles into development platforms running the Microsoft Gaming OS (built upon Windows 10/11 OneCore). While it grants significant capabilities, it differs fundamentally from standard Win32 desktop environments and retail game development kits (GDK).

This document establishes the verified technical capabilities and constraints of the Xbox Developer Mode environment.

---

## 2. Dynamic Code Generation & JIT (Make-or-Break #1)

### The Core Question
Does Xbox Developer Mode permit the allocation and execution of dynamically generated machine code (W^X relaxation)?

### Technical Finding: YES, UNDER UWP FULL-TRUST
* **API Availability:** In standard restricted UWP app containers, `VirtualAlloc(..., PAGE_EXECUTE_READWRITE)` or converting `PAGE_READWRITE` to `PAGE_EXECUTE_READ` is blocked by Windows Code Integrity (ACG - Arbitrary Code Guard).
* **Xbox Developer Mode Exception:** Xbox Developer Mode allows applications declaring the `<rescap:Capability Name="runFullTrust" />` restricted capability to bypass ACG restrictions.
* **JIT Mechanism:**
  1. Allocate page with `VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)`.
  2. Write compiled x86-64 machine instructions into the buffer.
  3. Transition page protection to `PAGE_EXECUTE_READ` using `VirtualProtect`.
  4. Flush instruction cache via `FlushInstructionCache(GetCurrentProcess(), buffer, size)`.
  5. Execute dynamically generated code.
* **Architectural Decision:** Nemu’s JIT will use a dual-mapping or `VirtualProtect` transition scheme, ensuring strict compliance with W^X and minimizing cache invalidation overhead.

---

## 3. Graphics APIs & GPU Architecture (Make-or-Break #2)

### The Core Question
What graphics API is exposed to Developer Mode applications, and can Vulkan run on Xbox?

### Technical Finding: DIRECT3D 12 ONLY
* **Vulkan Availability:** **ZERO.** Microsoft does NOT ship a Vulkan Installable Client Driver (ICD) on Xbox OS. Any assumption that Vulkan can be initialized directly on Xbox Developer Mode is incorrect.
* **Direct3D 12:** Full native support for Direct3D 12 (`d3d12.dll`, `dxgi.dll`) with Feature Level 12_1 and 12_2.
* **DirectX 12 Agility SDK:** Fully supported on Xbox Series consoles, providing advanced features:
  * Resource Binding Tier 3
  * Raytracing Tier 1.1 (DXR)
  * Mesh Shaders / Amplification Shaders
  * Variable Rate Shading (VRS) Tier 2
  * Sampler Feedback
* **Shader Compilation:**
  * Host compilation uses the Microsoft DirectX Shader Compiler (`dxc.exe` / `dxcompiler.dll`) producing DXIL (DirectX Intermediate Language).
  * Direct runtime compilation from HLSL strings or DXIL bytecode is fully supported.
* **Architectural Mandate:** Nemu **must** implement a Direct3D 12 graphics backend. Vulkan-only architectures (e.g. unmodified Citron/Yuzu) cannot run on Xbox without a complete D3D12 translation layer.

---

## 4. Memory Allocations & Budgets

### Available RAM in Developer Mode

| Metric | Xbox Series S | Xbox Series X |
| :--- | :--- | :--- |
| **Total Physical GDDR6** | 10.0 GB (8GB fast @ 224 GB/s + 2GB standard @ 56 GB/s) | 16.0 GB (10GB fast @ 560 GB/s + 6GB standard @ 336 GB/s) |
| **OS / Hypervisor Reserved** | ~5.0 GB | ~5.0 GB |
| **Default UWP Sandbox Budget** | 1.0 GB - 2.0 GB (Application Mode) | 1.0 GB - 2.0 GB (Application Mode) |
| **Expanded Resources Budget** | **Up to ~5.0 GB** | **Up to ~11.0 GB** |

### Critical Requirement: `expandedResources`
To allocate more than 1-2 GB of RAM, the app package manifest MUST declare:
```xml
<rescap:Capability Name="expandedResources" />
```
And during runtime initialization, the application must query and request expanded resources via `Windows.System.MemoryManager`. Without this, the OS will terminate the application with an out-of-memory error when emulating games with 4GB+ Switch RAM.

---

## 5. Filesystem & Storage Access

* **Sandboxed App Storage:**
  * `Windows.Storage.ApplicationData.Current.LocalFolder` (`ms-appdata:///local/`): Writable persistent internal storage for configuration, shader cache, and save data.
* **External USB Storage:**
  * By declaring `<rescap:Capability Name="broadFileSystemAccess" />`, Nemu can access connected USB drives (typically mounted as `D:\`, `E:\`, etc.) formatted with NTFS or FAT32.
  * Essential for loading user game dumps and homebrew binaries without filling the console's internal flash storage.

---

## 6. Threading, Synchronization, and CPU Core Topology

* **Xbox Series S/X CPU:** AMD Custom Zen 2 (8 Cores, 16 Threads).
* **Developer Mode Allocation:**
  * In standard mode: Up to 4 CPU cores are dedicated to UWP.
  * In Game/Expanded Mode: 6 full physical CPU cores (12 hardware threads) are available for application workloads.
* **Threading Primitives:** Standard Win32 threading APIs (`CreateThread`, `std::jthread`, `SRWLock`, `ConditionVariable`, `WaitOnAddress`, C++20 atomics) operate with full performance.

---

## 7. Audio & Controller Input APIs

* **Audio Output:** XAudio2 2.9 (`xaudio2_9.dll`) and WASAPI are standard on Xbox OS. Both support low-latency multichannel PCM and spatial audio.
* **Controller Input:**
  * `Windows.Gaming.Input`: Native UWP gamepad API supporting up to 8 wireless Xbox controllers, trigger rumble, battery status, and low input latency (< 4ms).
  * XInput 1.4: Fully supported for standard 4-controller setups.

---

## 8. Packaging, Deployment, and Security Model

* **Package Format:** APPX / MSIX.
* **Signing:** Developer Mode requires packages to be signed with a test certificate (generated via `New-SelfSignedCertificate` or OpenSSL).
* **Deployment Protocol:** Sideloaded remotely via Xbox Device Portal (HTTPS on port 11443) or deployed via Visual Studio remote debugger.
* **Dependencies:** Visual C++ Universal Runtime package (`Microsoft.VCLibs.x64.14.00.appx`) must be installed as a dependency.
