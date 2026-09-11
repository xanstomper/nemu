# Nemu Kernel & System Services Architecture (Horizon OS HLE)

## 1. System Overview

Nintendo Switch software runs atop Horizon OS, a proprietary microkernel operating system. Rather than emulating low-level ARM exception levels (EL1/EL2/EL3), Nemu implements High-Level Emulation (HLE) of the Horizon OS user-space interface.

Guest executables communicate with the kernel through **Supervisor Calls (SVC)** using the ARM64 `SVC #imm16` instruction.

---

## 2. Kernel Object Model

Every kernel primitive derives from a reference-counted `KAutoObject`:

```
                  KAutoObject
                       │
       ┌───────────────┼───────────────┐
       ▼               ▼               ▼
    KProcess        KThread   KSynchronizationObject
                                       │
                ┌──────────────┬───────┴──────────────┐
                ▼              ▼                      ▼
             KEvent          KMutex             KSemaphore
```

### 2.1 Handle Table
Each `KProcess` owns a `KHandleTable` mapping 32-bit handle values (`Handle`) to `std::shared_ptr<KAutoObject>`. Handles are validated on every SVC entry.

### 2.2 Thread Management (`KThread`)
* Guest priorities range from 0 (highest) to 63 (lowest).
* Each thread owns:
  * Guest stack pointer (`SP`) and program counter (`PC`).
  * Thread Local Storage (TLS) page located at `0x00_C000_0000 + (ThreadID * 0x200)`.
  * Execution state: `Initialized`, `Ready`, `Running`, `Waiting`, `Terminated`.

---

## 3. Essential Supervisor Calls (SVC Matrix)

| SVC ID | Name | Description | Status |
| :--- | :--- | :--- | :--- |
| `0x01` | `svcSetHeapSize` | Dynamically expand/contract process heap | Designed |
| `0x02` | `svcSetMemoryPermission` | Change page protection bits | Designed |
| `0x03` | `svcSetMemoryAttribute` | Set memory cache attributes | Designed |
| `0x04` | `svcMapMemory` | Map mirror of address space | Designed |
| `0x05` | `svcUnmapMemory` | Unmap address mirror | Designed |
| `0x06` | `svcQueryMemory` | Query page status and memory layout | Designed |
| `0x07` | `svcExitProcess` | Terminate current process | Designed |
| `0x08` | `svcCreateThread` | Allocate new guest thread context | Designed |
| `0x09` | `svcStartThread` | Transition thread to runnable | Designed |
| `0x0A` | `svcExitThread` | Terminate calling thread | Designed |
| `0x0B` | `svcSleepThread` | Yield CPU or sleep for nanoseconds | Designed |
| `0x18` | `svcWaitSynchronization` | Wait on array of handles (events, timers) | Designed |
| `0x19` | `svcCancelSynchronization` | Cancel pending sync wait | Designed |
| `0x1F` | `svcConnectToNamedPort` | Connect to system service by string name | Designed |
| `0x21` | `svcSendSyncRequest` | Dispatch IPC message to service session | Designed |
| `0x26` | `svcBreak` | Debug break and crash handler | Designed |
| `0x27` | `svcOutputDebugString` | Forward guest debug strings to Nemu log | Designed |

---

## 4. Service Manager (`sm:`) & IPC Dispatcher

Switch services use the **HIPC (Horizon Inter-Process Communication)** protocol:
1. Guest prepares command buffer in its TLS:
   * Header: Command type, input handles, output handles, buffer descriptors.
   * Payload: Request ID, arguments.
2. Guest executes `svcSendSyncRequest(session_handle)`.
3. Nemu kernel dispatches to the corresponding HLE service implementation:
   * `sm:` (Service Manager: queries and acquires service handles)
   * `set:sys` (System Settings: language, region, time format)
   * `time:u` (Time service: POSIX epoch and steady clocks)
   * `fsp-srv` (Filesystem service: mounting RomFS, SaveFS, SD card)
   * `hid` (Human Interface Device: gamepad states and touch inputs)
   * `vi:m` / `nvnflinger` (Visual Interface: display layers and buffers)
4. Response payload and output handles are written back into the thread's TLS buffer before returning execution to guest code.
