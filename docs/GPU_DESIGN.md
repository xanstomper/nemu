# Nemu GPU Subsystem Architecture & Direct3D 12 Translation

## 1. Guest Hardware & Host Target

* **Guest GPU:** NVIDIA GM20B (Maxwell Generation 2 architecture, 256 CUDA cores, Tegra X1).
* **Target Graphics API:** Direct3D 12 (Agility SDK / Feature Level 12_1/12_2 on Xbox Series S/X).
* **Rationale:** As established in the Xbox Capabilities Audit, Xbox Developer Mode provides **no Vulkan support**. Nemu's graphics engine is designed specifically around a Direct3D 12 pipeline.

---

## 2. Maxwell 3D Architecture Pipeline

```
Guest CPU Pushbuffer (NVN / Libnx commands)
                   │
                   ▼
       Host1x Channel Processing
                   │
                   ▼
         Maxwell 3D State Machine
  (Rasterizer, Shaders, Blend, Depth/Stencil, Viewports)
                   │
         ┌─────────┴─────────┐
         ▼                   ▼
    Buffer Cache       Texture Cache
 (Vertex/Index/CBuf)  (Surfaces/Samplers)
         │                   │
         └─────────┬─────────┘
                   ▼
        Direct3D 12 Backend
 (Root Signatures, PSOs, Command Lists)
                   │
                   ▼
          Presentation / Swapchain
           (Flip Model, 60 Hz)
```

---

## 3. Direct3D 12 Translation Engine

### 3.1 Device & Queue Management
* Creates `ID3D12Device` with minimum Feature Level 12_1.
* Three distinct queues:
  * **Direct Command Queue:** Executes rasterization and compute workloads.
  * **Copy Command Queue:** Asynchronous DMA buffer/texture uploads from CPU to GPU VRAM.
  * **Compute Command Queue:** Asynchronous compute shaders and post-processing.

### 3.2 Pipeline State Object (PSO) Caching
Direct3D 12 requires immutable `ID3D12PipelineState` objects encompassing:
* Root Signature (CBV, SRV, UAV, Sampler descriptor tables)
* Compiled Vertex & Pixel Shaders (DXIL)
* Blend State (`D3D12_BLEND_DESC`)
* Rasterizer State (`D3D12_RASTERIZER_DESC`)
* Depth/Stencil State (`D3D12_DEPTH_STENCIL_DESC`)
* Input Layout (`D3D12_INPUT_ELEMENT_DESC`)
* Render Target Formats (`DXGI_FORMAT`)

Nemu implements a fast hash-indexed PSO cache to minimize runtime state compilation stalls.

---

## 4. Texture Cache & Surface Translation

### 4.1 Maxwell Tiled Memory Deswizzling
Maxwell GPUs organize textures in block-linear memory layouts (gob, blocks of tiles) to maximize memory cache locality. Nemu implements hardware/compute shader deswizzling translating guest tiled memory to standard pitch-linear or DXGI tiled surfaces.

### 4.2 Format Mapping Table
| Maxwell Format | DXGI / D3D12 Format | Conversion Strategy |
| :--- | :--- | :--- |
| `RGBA8_UNORM` | `DXGI_FORMAT_R8G8B8A8_UNORM` | Direct 1:1 copy |
| `BGRA8_UNORM` | `DXGI_FORMAT_B8G8R8A8_UNORM` | Direct 1:1 copy |
| `RGB565_UNORM` | `DXGI_FORMAT_B5G6R5_UNORM` | Direct 1:1 copy |
| `BC1_RGBA` | `DXGI_FORMAT_BC1_UNORM` | Direct 1:1 copy |
| `BC2_RGBA` | `DXGI_FORMAT_BC2_UNORM` | Direct 1:1 copy |
| `BC3_RGBA` | `DXGI_FORMAT_BC3_UNORM` | Direct 1:1 copy |
| `BC7_UNORM` | `DXGI_FORMAT_BC7_UNORM` | Direct 1:1 copy |
| `ASTC_2D_4X4` | `DXGI_FORMAT_R8G8B8A8_UNORM` | Decompressed via DirectCompute shader |
| `Z24S8` | `DXGI_FORMAT_D24_UNORM_S8_UINT` | Direct 1:1 copy |

---

## 5. Buffer Cache & CPU-GPU Synchronization

* **Constant Buffers:** Mapped through dynamically ring-allocated upload buffers committed per draw call.
* **Vertex / Index Buffers:** Tracked via range maps. If the guest CPU modifies a vertex buffer, the dirty range is detected and queued for asynchronous DMA transfer before the next draw submission.
* **Fences & Semaphores:** Direct mapping of Maxwell syncpoints and semaphores to `ID3D12Fence` objects, signaling completion across CPU and GPU timelines.
