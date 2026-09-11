# Nemu Memory Subsystem Design

## 1. Overview & Address Spaces

The Nintendo Switch utilizes a 64-bit ARMv8 virtual memory management unit (MMU) configured for 36-bit to 39-bit physical/virtual addressing depending on kernel firmware version.

### Address Space Layout (64-Bit Process)
* `0x00_0800_0000` - `0x00_7FFF_FFFF`: Code / Executable Region (Standard NRO/NSO base)
* `0x00_8000_0000` - Heap Region (Dynamically expanded via `svcSetHeapSize`)
* `0x00_C000_0000` - Alias / Stack Region (Stack allocations, thread stacks)
* `0x10_0000_0000`+ : Shared Memory / GPU Virtual Mappings

---

## 2. Multi-Level Page Table Architecture

To manage guest virtual addresses efficiently on both host Linux and Xbox Developer Mode, Nemu implements a 2-level or 3-level Radix Page Table:

```
Guest Virtual Address (48-bit)
┌──────────────┬──────────────┬──────────────┬──────────────────┐
│ Level 1 (9b) │ Level 2 (9b) │ Level 3 (9b) │ Page Offset (12b)│
└──────┬───────┴──────┬───────┴──────┬───────┴─────────┬────────┘
       │              │              │                 │
       ▼              ▼              ▼                 ▼
   L1 Table  ──>  L2 Table  ──>  L3 Entry     Physical/Host Backing
                                (PageEntry)
```

### Page Entry Definition
Each page entry is a 64-bit pointer or descriptor:
```cpp
struct PageEntry {
    uintptr_t host_pointer : 48; // Host memory address or null if unmapped
    uint16_t permissions  : 4;  // Read = 1, Write = 2, Execute = 4
    uint16_t state        : 4;  // Free, Reserved, Allocated, Shared
    uint16_t attributes   : 8;  // Device, Normal, Cached
};
```

---

## 3. Fastmem (Direct Virtual Address Mapping)

Software-interpreted address translation (looking up every load/store in the page table) incurs substantial CPU overhead. Nemu supports a **Fastmem** architecture:

1. On initialization, reserve a 64-bit contiguous virtual address range on the host:
   * Host reservation: `HostBase = VirtualAlloc(nullptr, 1ULL << 39, MEM_RESERVE, PAGE_NOACCESS);` (Windows/Xbox)
   * Or `mmap(nullptr, 1ULL << 39, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);` (Linux)
2. When the guest maps a page at `GuestVA`:
   * Commit physical host backing at `HostBase + GuestVA` with requested permissions.
3. JIT instructions translate guest loads/stores to direct host memory accesses:
   * ARM `LDR X0, [X1]` becomes x86-64 `mov rax, [r14 + rbx]` where `r14` is fixed `HostBase`.
4. Invalid accesses trigger host hardware page faults:
   * Trapped by **Structured Exception Handling (SEH) / Vectored Exception Handling (VEH)** on Xbox.
   * Trapped by `SIGSEGV` / `sigaction` on Linux.
   * The handler inspects faulting address, maps pages if legitimate, or logs guest fatal diagnostics.

---

## 4. Exclusive Monitor & Atomics

ARMv8 requires exclusive load/store instructions (`LDXR`, `STXR`, `LDAXR`, `STLXR`) for lock-free concurrency.

Nemu implements a per-core Exclusive Monitor:
* Each thread/core tracks `exclusive_address` and `exclusive_state`.
* `LDXR` marks the address as exclusively monitored.
* `STXR` succeeds (returns 0) only if the address has not been modified by another core since the exclusive load; otherwise it fails (returns 1).
* Host implementation leverages host C++20 `std::atomic` and atomic compare-exchange (`lock cmpxchg` on x86-64) for lock-free safety across threads.
