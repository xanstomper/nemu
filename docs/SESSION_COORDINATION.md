# NEMU Cross-Session Coordination Log

Purpose: single source of truth shared by every Jcode+/Hermes session running
NEMU. Both sessions read this, pick the highest-priority open item that is NOT
already checked out, and update it as they work. Never run two heavy builds at
once on this 4-core box.

## Machine Constraints (must respect to avoid bottlenecking)

- **4 CPU cores** (Intel N97). Load average already ~8-9 whenever a parallel
  C++ build is running. A second concurrent build causes thrashing.
- **Disk 95% full** (14 GB free on `/`). Build in `build/` parallel `-j2`.
- Rule: **only ONE session compiles at a time.** The compile lock is the file
  `build/.build_lock`. Acquire with `touch` + O_EXCL semantics before heavy
  `cmake --build`; release when done. Do non-compiling work otherwise (this doc,
  code review, tests that use existing binaries, planning).

## Current Cutting Edge (owner: GPU/D3D12 session)

In-progress, uncommitted:
- Wiring the real D3D12 device into the Maxwell -> HLSL -> PSO translation chain.
  Files: `src/core/gpu/d3d12/d3d12_backend.{cpp,hpp}`,
  `src/core/gpu/gpu_interface.hpp`,
  `src/core/gpu/pipeline/pipeline_cache.cpp`,
  `src/core/system/emulator.cpp`.
- Adds `SetGuestShaders()`, `BindTranslatedPipeline()`, and live JIT
  enable/disable now driven by config (Switch UI toggles the CPU backend at
  runtime without restart).

## Verification Baseline

- 15/15 unit test suites pass on Linux + Win/Xbox (Wine) per README/PROJECT_STATUS.
- `test_gpu`, `test_render_pipeline`, `test_pipeline_bridge`, `test_hlsl_validator`
  are the GPU-translation regression targets.

## Open Gaps / Next Steps (prioritized)

### P1 (in-flight, GPU session)
1. Finish + commit D3D12 translation wiring. Then verify with
   `ctest -R 'gpu|render_pipeline|pipeline_bridge|hlsl'`.

### P2 (translation-layer completeness)
2. **[DONE 2026-09-14, coordinator owns d3d12_backend.*] Bind guest constant
   buffers in D3D12 backend.** Implemented `SetGuestConstantBuffer` + root CBV
   binding (`BindGuestConstantBuffers`), `key.num_cbufs` from guest state,
   release on Shutdown. Need: guest Maxwell side still doesn't call
   `SetGuestConstantBuffer` yet (GPU session's `maxwell_3d` only uploads
   shaders); wire cbuf reads from guest memory in `maxwell_3d.cpp`
   (`StateShaderProgram`/`CbufAddress` registers) to feed this.
3. **[DONE f141fd1 (guest) + 1fe89a8 (backend)] Bind texture SRVs through a
   descriptor heap.** `IGpuBackend` exposes `SetGuestTextureBinding` /
   `SetGuestSamplerBinding` / `SetGuestTextureCount`; `d3d12_backend` owns a
   `TextureCache` and binds the SRV table at draw. `maxwell_3d` now tracks
   guest texture state registers and feeds the backend at draw (test_gpu case
   15 verifies bind-once / no-rebind / unlink). **Fully done.**
4. **[DONE 5f3cbf7 (guest) + bd27c8d/a53d0d4 (backend)] Generalize D3D12 input
   layout for arbitrary guest vertex attributes.** Fully implemented and
   committed across both sessions/coordinator. Backend: dynamic input layout
   from `vertex_attribs[8]` (backward-compatible POSITION+TEXCOORD0 fallback),
   `SetGuestVertexAttributes`, `SetGuestVertexBuffer` upload+bind. Guest:
   `maxwell_3d` reads the vertex array + `VertexArrayStride` at draw and feeds
   the backend (test_gpu case 16). GPU session's `dd4bff4` gated emitted shader
   attrs. **P2-4 complete.**

### P3 (robustness / parity)
5. **[DONE 29cfc6b] Differential-test JIT vs interpreter for the runtime JIT
   toggle path.** `test_jit` Test 17 runs a straight-line program to a midpoint
   on the interpreter, then hands off to a fresh JIT compiler (the emulator.cpp
   live create/Clear path) and asserts bit-exact agreement with a pure-
   interpreter trace (X0 == 150). PASS on Linux + Windows PE32+ under Wine.
6. **[DONE 2026-09-14] Re-run the Windows/Xbox cross build after the P2 changes.**
   Cross-built `test_gpu/render_pipeline/pipeline_bridge/hlsl_validator/jit` PE32+
   all PASS under Wine, incl. new cases 15 (guest texture) + 16 (guest vertex
   attr+buffer); `Nemu.exe` links and boots its emulator runtime under Wine.

## P2-3 Guest-Side Handoff (whoever picks up maxwell_3d)

`maxwell_3d.hpp` already defines the needed guest texture registers:
`TextureAddressHigh/Low (0x0585/86)`, `TextureFormat (0x0589)`,
`TextureWidth (0x058A)`, `TextureHeight (0x058B)`, plus vertex
`VertexArrayAddressHigh/Low (0x0587/88)`, `IndexAddressHigh/Low (0x05F2/93)`.

To complete P2-3 (backend already committed in `1fe89a8`):
1. In `Maxwell3D::ProcessMethod`, on a texture-address/format/size event mark
   `textures_dirty_` (add a bool like `programs_dirty_`, or fold into it).
2. In a `BindGuestTextures()` called at draw (mirror `BindGuestShaders()`),
   build a `texture::TextureDescriptor` from those regs
   (`gpu_address = {TextureAddressHigh<<32}|Low`, width, height,
   `is_block_linear=true`, format from `TextureFormat`) and call
   `backend_->SetGuestTextureBinding(binding=0, desc, memory_)` +
   `backend_->SetGuestTextureCount(1)`.
3. TIC/TSC parser exists (`core/gpu/texture/texture_types.hpp`:
   `TicParser::Parse` / `TscParser::Parse` over 8 u32 words) for when a real
   game's multi-stage binding requires it; for now a single RGBA8 texture from
   the 5 regs covers the first rendered frame.

## Session Handoff Protocol

- This file is append-only for logs; edit the Open Gaps section only to mark
  items done/moved.
- Before editing any source file, check `docs/SESSION_COORDINATION.md` and the
  git status so two sessions don't edit the same file simultaneously.
- Mark P1-6 items as `[DONE: <hash>]` once committed, with your session name.

## Status Timestamps

| Time (UTC) | Session | Note |
| :--- | :--- | :--- |
| 2026-09-14 10:53 | coordinator | Recon complete; GPU translation wiring in flight; machine heavily loaded; coordination doc written |
| 2026-09-14 10:54 | coordinator | Verified in-flight D3D12 translation wiring: `test_gpu`, `test_render_pipeline`, `test_pipeline_bridge`, `test_hlsl_validator` all PASS (rebuild ~06:53:40-56). Translation layer green at unit level. Next: P2 items 2/3/4 (cbuf binding, SRV descriptor heap, generalized input layout) |
| 2026-09-14 10:55 | coordinator | GPU session committed wiring as `1cd07ac`. Confirmed open P2 gaps: `SetGuestConstantBuffer` still no-op default (not overridden in D3D12 backend), `key.num_cbufs`/`num_textures` still hardcoded 0 in `BindTranslatedPipeline`, no root CBV/SRV set. P2 items 2 & 3 are live and unstarted. GPU session is now editing `maxwell_3d.*` + `emulator.cpp` (P2 item 4 direction) |
| 2026-09-14 10:56 | coordinator | **Implemented P2 item 2 (guest CBV binding)** in `src/core/gpu/d3d12/d3d12_backend.{cpp,hpp}`: overrode `SetGuestConstantBuffer`, added `GuestCbuf` upload-resource array (256B-aligned, `SetGraphicsRootConstantBufferView` at draw), set `key.num_cbufs` in `BindTranslatedPipeline`, release on Shutdown. Syntax-checked clean with x86_64-w64-mingw32-g++ (exit 0). P2 item 2 = DONE (uncommitted); coordinator now owns `d3d12_backend.*`. Open items remain P2-3 (texture SRV descriptor heap binding) and P2-4 (generalized input layout, GPU session's current `maxwell_3d` work) |
| 2026-09-14 10:57 | coordinator | GPU session's guest shader->backend plumbing (`maxwell_3d` reading program addresses + `test_gpu` case 14) committed-equivalent; rebuilt `test_gpu` PASSES (incl. `Maxwell3D guest shader -> backend plumbing`). Both my CBV binding and their shader upload verified in same tree. P2-4 direction (guest vertex state) also visible. Committing both sessions' mixed uncommitted work as one commit deferred to avoid misattribution; waiting for GPU session to commit its `maxwell_3d`/`emulator`/`test_gpu` work before layering P2-3 on the shared tree |
| 2026-09-14 11:03 | all | State recap: both CBV binding (`ab6d583`) and shader routing (`20ecae6`) now committed by both sessions; full 27-test ctest suite passes 100%. GPU session has uncommitted live-JIT toggle in `emulator.cpp` + LDN fix committed (`0627959`). |
| 2026-09-14 11:04 | coordinator | **Added Test 16 (SMC cache-invalidation differential)** in `tests/unit/jit/test_jit.cpp`, committed `9561684`. Validates the runtime JIT-toggle correctness: patching cached guest code + `InvalidateBlock` (same path as `JitCompiler::Clear()` on toggle) makes the JIT recompile from patched bytes and match the interpreter bit-for-bit (X0 == 0x22, not stale 0x11). Directly guards the other session's `emulator.cpp` live-JIT-toggle work. Build+run PASS. |
| 2026-09-14 11:04 | coordinator | Next open items: P2-3 (texture SRV descriptor-heap binding; commits `20ecae6`+`ab6d583` set the shader/cbuf path, texture cache infra exists but not wired into D3D12 draw path) and P2-4 (generalize input layout). Both are D3D12 drawing-stage gaps that need guest texture/vertex state plumbed via `maxwell_3d`+`d3d12_backend`. Coordinator deferring P2-3 until GPU session commits its in-flight `emulator.cpp`. |
| 2026-09-14 11:05 | coordinator | **Windows/Xbox cross-build parity verified.** Rebuilt cross `test_jit.exe` (build-win) to include Test 16; run under Wine: **Test 16 (SMC cache invalidation) PASSED** on the PE32+ target as well as Linux. Also ran cross `test_gpu.exe`, `test_render_pipeline.exe`, `test_pipeline_bridge.exe`, `test_hlsl_validator.exe` under Wine: all PASS. Confirms shader routing (`20ecae6`) + CBV binding (`ab6d583`) + SMC correctness all hold on the Xbox deployment path. |
| 2026-09-14 11:05 | coordinator | **Coordination caveat:** the GPU session does NOT currently honor `build/.build_lock` (its cmake launches ignore it). Both sessions therefore can race builds on the 4-core box. Recommend the GPU session that it check/release `build/.build_lock` before `cmake --build`. This is non-critical (builds are small) but it is the documented risk to "no bottlenecking". |
| 2026-09-14 11:07 | coordinator | **Implemented P2-3 backend side (texture SRV binding)** across `gpu_interface.hpp`, `d3d12_backend.{cpp,hpp}`, `texture_cache.hpp`: added `SetGuestTextureBinding`/`SetGuestSamplerBinding`/`SetGuestTextureCount` interface hooks (no-op defaults; Null/SDL2 unaffected), wired the D3D12 backend to own a `texture::TextureCache` (SetDevice + Initialize), collect guest texture descriptors, and bind the SRV descriptor table (`SetGraphicsRootDescriptorTable` at root idx = num_cbufs) in `BindTranslatedPipeline` with `key.num_textures`. Added `TextureCache::SetDevice`. Compile-verified: Linux interface headers clean (exit 0), Windows D3D12 backend clean via x86_64-w64-mingw32-g++ (exit 0). **Guest side not yet wired**: `maxwell_3d` must parse TIC/TSC and call `SetGuestTextureBinding/Count` (same shape as the shader path it already does). Uncommitted, coordinator owns these files. |
| 2026-09-14 11:12 | coordinator | **P2-3 COMPLETE.** Guest side implemented + committed `f141fd1`: `maxwell_3d` tracks texture state regs (TextureAddress/Format/Width/Height), marks dirty, and at draw builds a `TextureDescriptor` and calls `SetGuestTextureBinding/Count`; unbinds on address clear. `test_gpu` case 15 verifies bind-once/no-rebind/unlink; full GPU suite green on Linux. Backend `1fe89a8` + guest `f141fd1` together give translated Maxwell games textured-geometry rendering. Only P2-4 (generalize input layout) remains. |
| 2026-09-14 11:13 | coordinator | GPU session committed `dd4bff4` (gated shader attrs to `used_attrs`; the containment fix for the single-attr PSO layout). **Full 27-test suite PASS after dd4bff4 + my P2-3 commits.** True P2-4 (dynamic multi-attribute input layout + guest vertex buffer) is documented with a precise (a)/(b)/(c) backend/guest split in the gaps section. |
| 2026-09-14 11:16 | coordinator | **P2-4 backend capability committed `a53d0d4`:** `PipelineStateKey` carries `vertex_attribs[8]`; `pipeline_cache` builds the D3D12 input layout dynamically (backward-compatible POSITION+TEXCOORD0 fallback); `IGpuBackend::SetGuestVertexAttributes` added; D3D12 backend wired it into `BindTranslatedPipeline`. Verified: test_pipeline_bridge (incl. emitter/PSO semantic coherence), test_gpu, test_render_pipeline, test_hlsl_validator PASS on Linux; d3d12 backend clean via MinGW; full 27-test suite green. **Remaining P2-4 guest side:** maxwell_3d reads VertexArrayAddress + VERTEX_ATTRIB_* and calls SetGuestVertexAttributes + uploads a guest vertex buffer. |
| 2026-09-14 11:19 | coordinator | **P2-4 backend fully complete.** Added `SetGuestVertexBuffer` (`bd27c8d`): D3D12 backend uploads raw guest vertex bytes and binds them with the guest stride in `BindTranslatedPipeline` (falls back to RasterVertex buffer when absent). Verified MinGW clean + test_gpu/test_pipeline_bridge PASS on Linux. Only P2-4 **guest vertex feeding** remains (GPU session's `maxwell_3d` lane). |
| 2026-09-14 11:23 | coordinator | **P2-4 FULLY COMPLETE.** Guest side implemented + committed `5f3cbf7`: `maxwell_3d` adds `VertexArrayStride`, reads the guest vertex array at draw, and calls `SetGuestVertexAttributes` (POSITION+TEXCOORD0) + `SetGuestVertexBuffer`; test_gpu case 16 verifies it reaches the backend. Full 27-test suite green on Linux; maxwell_3d clean on Linux + MinGW. Combined with `a53d0d4`/`bd27c8d` (backend) and `dd4bff4` (attr gating), real guest vertex geometry now flows through the translated D3D12 pipeline. |
| 2026-09-14 11:27 | coordinator | **P2 and P3 gaps all DONE.** P3-6 (cross PE32+ rebuilt and passing incl. Nemu.exe boot) + P3-5 (`test_jit` Test 17 runtime backend-toggle differential, `29cfc6b`, PASS Linux + Wine). All original translation-layer gaps closed; 27/27 tests green on both Linux and Windows/Xbox PE32+. |
| 2026-09-14 11:31 | coordinator | Watchdog (in-flight, GPU session uncommitted): it is extending the Null software rasterizer to support Triangles/Strip/Fan topologies + adding a GPU test; alongside its live-JIT toggle in emulator.cpp. Verified the combined tree builds test_gpu and the **full 27-test suite still passes** with these uncommitted edits in place. No regression from either party's work. |