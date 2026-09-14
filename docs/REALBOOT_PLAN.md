# NEMU Real-Boot Research: Minimal Homebrew Boot Path

Goal: establish a *demonstrable, guaranteed* boot of a real homebrew NRO (and go
as far toward a real game as legally possible) on Xbox Dev Mode. This is distinct
from passing synthetic unit tests.

## 1. What a real NRO boot requires (libnx crt0 contract)

A Switch homebrew NRO, under libnx, performs the following at startup. Each step
must be answered correctly by the emulator or the homebrew crashes/locks.

| Order | Operation | SVC / service | NEMU status |
| :--- | :--- | :--- | :--- |
| 1 | Bss cpy, stack/TLS setup (crt0) | `__nx_appletInit` bootstrap | interpreter/JIT run |
| 2 | `svcSetHeapSize` | 0x01 | implemented |
| 3 | `svcSetMemoryPermission` / `svcQueryMemory` | 0x02 / 0x06 | implemented |
| 4 | `svcGetInfo(InfoType_ProcessMemory)` | 0x29 | implemented (verify fidelity) |
| 5 | `smInitialize` → connect to `sm:` port | `svcConnectToPort` 0x2B + sm IPC | implemented |
| 6 | `sm: GetServiceHandle(appletOE / ...)` | sm service | implemented |
| 7 | `appletInitialize` (report init / ...) | appletOE service | partial (some stubbed) |
| 8 | `hidInitialize` / `fsInitialize` / `timeInitialize` etc. | hid / fsp-srv / time:u | partial (some stubbed) |
| 9 | `svcExitProcess` / `svcBreak` | 0x07 / 0x26 | implemented |
| 10 | Render frame via `nvn`/`nvdrv` | nvdrv, nvnflinger | partial |

**Key insight:** the boot-critical SVC set (heap, memory, threads, IPC, ports,
events) is present. The risk is *fidelity* (exact return encodings, handles, buffer
descriptors) and *IPC service answers*. A real NRO is the only way to know which is
wrong.

## 2. Toolchain reality

- **devkitPro / libnx is NOT installed** (no official Switch homebrew toolchain).
- `aarch64-linux-gnu-gcc` IS present (can build a bare ARM64 ELF, but a working
  NRO needs the libnx startup runtime).
- **Network is available** (GitHub reachable).

Two options to obtain a genuine NRO:
  1. Install devkitPro (`pacman`/installer) and build an open-source homebrew
     sample into an `.nro`. Heavy but self-contained and reproducible.
  2. Fetch a freely-redistributed open-source homebrew `.nro` release binary and
     run it through the loader. Faster; must pick a permissively-licensed one and
     keep provenance.

## 3. "Guarantee boot" definition (verifiable)

A boot is guaranteed/verified when ALL hold:
1. A real `.nro` (not synthetic bytes) parses via `TitleLoader`.
2. It reaches its entry point on the interpreter or JIT.
3. It completes crt0 init services without an unhandled SVC / wrong IPC reply.
4. NEMU renders at least one frame (Null or SDL2/D3D12) without crashing.
5. On Xbox: the APPX boots NEMU into its library and loads the same NRO.

## 4. Next actions (ranked)

1. Obtain a genuine NRO (prefer building one with devkitPro for full provenance;
   fall back to a permissively-licensed release binary).
2. Add a `--run <path.nro>` headless mode to Nemu (or test harness) that loads +
   executes it and logs the first unhandled SVC / failing IPC.
3. Fix the first real failure, iterate until crt0 boot completes.
4. Audit permissively-licensed open components (Separate Milestone).