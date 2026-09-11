# Nemu Architecture Decision Records (ADR)

## ADR-001: Selection of C++20 for Implementation
* **Status:** Accepted
* **Context:** Emulating modern systems hardware requires fine-grained control over memory layouts, bit manipulation, SIMD intrinsics, zero-overhead abstractions, and cross-platform compilation targeting both Linux and Windows/Xbox.
* **Decision:** Adopt C++20 across all Nemu components.
* **Consequences:** Provides access to `std::span`, concepts, `std::bit_cast`, high-performance designated initializers, and structured concurrency primitives while maintaining full compatibility with GCC 13, Clang 18, and MSVC 2022.

---

## ADR-002: Direct3D 12 as Native Graphics API for Xbox
* **Status:** Accepted
* **Context:** The Nintendo Switch uses an NVIDIA Maxwell GPU executing NVN/Vulkan-like graphics commands. Other open-source Switch emulators target Vulkan. However, Xbox Series S/X Developer Mode provides **no Vulkan ICD**; it exposes Direct3D 12 Feature Level 12_1/12_2.
* **Decision:** Build a native Direct3D 12 backend for Maxwell 3D commands rather than attempting to introduce non-existent Vulkan layers.
* **Consequences:** Requires custom translation of Maxwell states, shader bytecode, and pipeline configurations to D3D12 root signatures, PSOs, and descriptor heaps. Guarantees 100% native execution on Xbox Series hardware.

---

## ADR-003: Two-Tier CPU Architecture (Interpreter First, Dynarec Second)
* **Status:** Accepted
* **Context:** Building an advanced JIT directly without a correctness baseline results in intractable debugging cycles when guest titles crash due to subtle instruction encoding or flag differences.
* **Decision:** Implement a strictly validated, cycle-accurate reference interpreter before activating the x86-64 dynamic recompiler.
* **Consequences:** The interpreter serves as the ground truth for differential testing against the JIT, guaranteeing instruction-level fidelity.

---

## ADR-004: Horizon OS High-Level Emulation (HLE)
* **Status:** Accepted
* **Context:** Emulating the Tegra X1 bootROM, TrustZone (EL3), and microkernel scheduler at a low level (LLE) requires copyrighted Nintendo firmware and proprietary cryptographic keys.
* **Decision:** High-Level Emulate (HLE) Horizon OS supervisor calls (SVCs) and IPC services in user-space.
* **Consequences:** Eliminates legal infringement, avoids proprietary firmware dependencies, simplifies debugging, and enhances performance by executing kernel primitives directly on host OS threads.

---

## ADR-005: Preservation of Storage & Non-Destructive USB Strategy
* **Status:** Accepted
* **Context:** The attached USB storage `/dev/sdb` contains existing user data, and the host backup was discovered to contain a truncated file (`Mario Kart Wii` 2GB truncate).
* **Decision:** Cease any automatic disk formatting or wiping. Develop the clean Nemu core repository within `/home/jewboy420/nemu` until explicit user confirmation is obtained for any drive repartitioning.
* **Consequences:** Prevents catastrophic data loss while keeping the engineering workflow unblocked.
