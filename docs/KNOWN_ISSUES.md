# Nemu Known Issues & Platform Limitations

## 1. Tracking Status

This document tracks all currently identified bugs, platform limitations, unsupported APIs, and active engineering hurdles. No issue is hidden or minimized.

---

## 2. Active Platform Limitations

| ID | Subsystem | Description | Impact | Mitigation / Status |
| :--- | :--- | :--- | :--- | :--- |
| **ISS-001** | Storage | Host root drive `/dev/sda2` is 98% full (6.2 GB free). | High-parallelism C++ debug builds risk exhausting disk space on `/`. | Direct intermediate build caches and object outputs carefully; limit parallel jobs (`-j2`). |
| **ISS-002** | Storage | USB drive `/dev/sdb1` NTFS filesystem has MFT fixup warnings from prior clone. | `fuseblk` NTFS read errors on deep directories (`src/hid_core`). | Active Nemu development repository initialized cleanly on local host (`/home/jewboy420/nemu`). USB format paused awaiting user confirmation. |
| **ISS-003** | Graphics | Xbox Developer Mode lacks Vulkan drivers. | Cannot reuse Vulkan-only backends from other emulators without full rewrite. | Architecture committed to native Direct3D 12 Maxwell translation engine. |
| **ISS-004** | Graphics | Maxwell ASTC texture decoding on D3D12. | Direct3D 12 does not natively support mobile ASTC texture formats on desktop/Xbox hardware. | Implement compute shader deswizzler and ASTC software/compute decompression pass. |
| **ISS-005** | Memory | Xbox UWP Default Memory Ceiling. | Standard UWP sandbox terminates processes exceeding ~1-2 GB RAM. | App package manifest must enforce `<rescap:Capability Name="expandedResources" />` and trigger game mode. |

---

## 3. Unimplemented Horizon Kernel Services (Tracking List)

* `nvnflinger`: Display buffer acquisition and presentation queues (Designed, implementation pending Gate 5).
* `audren:u`: Audio renderer voice processing (Designed, implementation pending Gate 6).
* `bcat:u`: Background content delivery (Stubbed / Unsupported).
* `friends`: Social / friend presence services (Stubbed / Unsupported).
* `nifm:u`: Network interface manager (Stubbed / Unsupported).
