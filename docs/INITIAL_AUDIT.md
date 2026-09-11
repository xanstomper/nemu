# Initial Audit Report: Nemu

**Date:** 2026-09-11  
**Target Platform:** Xbox Series S / Xbox Series X (Xbox Developer Mode)  
**Host Development Environment:** Linux Mint 22.3 (Ubuntu 24.04 Noble) x86_64  
**Audit Conducted By:** Antigravity (AGY) & Hermes  

---

## 1. Executive Summary

This audit establishes the baseline technical assessment for **Nemu**, a Nintendo Switch emulator designed for Xbox Series S/X Developer Mode. The project is initiating clean-room development under `/home/jewboy420/nemu`, targeting a correctness-verified modular C++20 architecture.

---

## 2. Host Development Environment & Hardware

| Component | Specification | Operational Constraint |
| :--- | :--- | :--- |
| **OS** | Linux Mint 22.3 (Zena) / Ubuntu 24.04 LTS (Noble) | Standard modern Linux development base |
| **Kernel** | Linux 6.14.0-37-generic x86_64 SMP PREEMPT_DYNAMIC | High-precision timers, user namespaces, modern syscalls |
| **CPU** | Intel(R) N97 (4 cores, 4 threads @ up to 3.60 GHz) | Alder Lake-N architecture: AVX2, SHA-NI, BMI2, VAES, VNNI |
| **RAM** | 11.0 GiB Physical (5.4 GiB free, 8.0 GiB available), 2.0 GiB Swap | Moderate RAM capacity: multi-core C++ compilation requires `-j2` or `-j3` to prevent OOM |
| **Root Disk (`/dev/sda2`)** | 234 GiB Total, 216 GiB Used, **6.2 GiB Available (98% full)** | **CRITICAL CONSTRAINT:** Root partition cannot host multi-gigabyte build artifacts or debug symbols |

---

## 3. Storage & USB Device Inspection

A comprehensive storage hardware audit was executed via `lsblk`, `mount`, and `dmesg`:

* **Device Path:** `/dev/sdb` (child partition `/dev/sdb1`)
* **Hardware Model:** PNY USB 3.2.1 FD (`Direct-Access PMAP`)
* **Capacity:** 57.8 GiB (121,145,344 512-byte logical sectors / 62.0 GB)
* **Bus / Transport:** USB 3.2 Gen 1 (`usb-storage` / SCSI generic `sg1`)
* **Removable:** Yes (`RM=1`, `RO=0` read/write enabled)
* **Filesystem:** NTFS (`fuseblk` via `ntfs-3g` / `udisks2`)
* **Mount Point:** `/media/jewboy420/XBOXEMU` (49 GiB free of 58 GiB)
* **Existing Contents:**
  * `Citron-Emulator/` (~105MB clone of citron-neo)
  * `ENHANCE.md`
  * `Mario Kart Wii (USA) (En,Fr,Es).rvz` (2.78 GB)
  * `Mario Kart Wii (USA) (EnFrEs).zip` (2.78 GB / 2,784,551,724 bytes)
  * `Microsoft.VCLibs.x64.14.00.appx` (859 KB)
  * `New Super Mario Bros. (USA).nds` (33.5 MB)
  * `retroarch/` and `retroarch-config/`
  * `retroarch-installer.appx` (516 MB)
  * `ROMs/`

### Critical USB Integrity Findings

1. **NTFS Filesystem Corruption:**
   Attempting to inspect directory trees inside `/media/jewboy420/XBOXEMU/Citron-Emulator/src/emulator/src/` resulted in `ls: reading directory 'src/hid_core': Input/output error`. Kernel logs confirm recurring NTFS MFT fixup failures (`ntfs-3g: ntfs_mst_post_read_fixup_warn: magic: 0x... Invalid argument`). The fuseblk NTFS driver is fundamentally flawed for deep C++ trees with high file counts and symlinks.
2. **Incomplete Host Backup & Data Loss Risk:**
   Inspection of the local backup at `/home/jewboy420/usb-backup/` revealed that `'Mario Kart Wii (USA) (EnFrEs).zip'` was truncated at exactly 2,144,694,272 bytes (2 GiB boundary) instead of the 2,784,551,724 bytes present on `/dev/sdb1`.
   **MANDATE:** In strict adherence to Rule 3, **NO DESTRUCTIVE FORMATTING OF `/dev/sdb` WILL OCCUR** until the user explicitly reviews and confirms this action after backing up any critical data.

---

## 4. Development Toolchains & Compilers

* **Native C++ Compiler:** GCC 13.3.0 (`/usr/bin/g++`, supports full C++20 and standard C++23 features)
* **Cross C++ Compiler:** MinGW-w64 GCC 13 (`/usr/bin/x86_64-w64-mingw32-g++`, targets Windows x86-64 / PE32+)
* **Build Systems:**
  * CMake 3.28.3 (`/usr/bin/cmake`)
  * Ninja 1.13.0 (`/home/jewboy420/.local/bin/ninja`)
* **Archivers / Packaging:** `zip`, `unzip`, `7z` (p7zip 23.01), `tar`
* **Python Runtime:** Python 3.12.3

---

## 5. Xbox Series S / Series X Target Capabilities

| Subsystem | Xbox Series S | Xbox Series X | Developer Mode Capability |
| :--- | :--- | :--- | :--- |
| **CPU** | AMD Zen 2 (8C/16T @ 3.6 GHz) | AMD Zen 2 (8C/16T @ 3.8 GHz) | Full x86-64-v3 + AVX2 + BMI2 support |
| **GPU** | AMD RDNA 2 (20 CUs @ 1.565 GHz, 4 TFLOPS) | AMD RDNA 2 (52 CUs @ 1.825 GHz, 12 TFLOPS) | Direct3D 12 Feature Level 12_1/12_2. **NO VULKAN DRIVER PRESENT.** |
| **RAM** | 10 GB GDDR6 (Dev Mode: ~5.0 GB available) | 16 GB GDDR6 (Dev Mode: ~11.0 GB available) | Standard UWP: 1-2 GB; With `expandedResources`: up to 5 GB (Series S) / 11 GB (Series X) |
| **Executable Memory** | Supported | Supported | `VirtualAlloc` with `PAGE_EXECUTE_READWRITE` / `PAGE_EXECUTE_READ` permitted in Developer Mode Full Trust |
| **Audio** | Hardware spatial / DSP | Hardware spatial / DSP | XAudio2 2.9, WASAPI |
| **Input** | Xbox Wireless Controller | Xbox Wireless Controller | `Windows.Gaming.Input`, XInput 1.4 |
| **Packaging** | APPX / MSIX | APPX / MSIX | Sideload via Xbox Device Portal (Port 11443 HTTPS) |

---

## 6. Technical Risks & Engineering Strategy

1. **The Graphics Bottleneck (D3D12 Requirement):**
   Prior PC Switch emulators are almost exclusively built around Vulkan. Xbox Developer Mode does **not** expose a Vulkan ICD. Nemu must implement a native Direct3D 12 backend for Maxwell 3D / NVN, mapping guest shaders (Maxwell NVN bytecode) through an intermediate representation (SPIR-V or custom IR) to HLSL/DXIL.
2. **JIT / Executable Memory:**
   Dynarec must handle guest ARM64 to host x86-64 translation while respecting cache line invalidation and page protection changes.
3. **Deterministic Verification:**
   Before running complex JIT code, a cycle-accurate reference interpreter and full unit-test suite must validate ARM64 instruction correctness against established guest behavior.
