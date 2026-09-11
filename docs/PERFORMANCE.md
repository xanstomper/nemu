# Nemu Performance Budgets & Profiling Architecture

## 1. Measurable Performance Targets

Rather than relying on vague optimization claims, Nemu defines explicit, measurable engineering budgets:

| Metric | Xbox Series S Target | Xbox Series X Target | Diagnostic Tolerance |
| :--- | :--- | :--- | :--- |
| **Steady-State Frame Rate** | 60.0 FPS | 60.0 FPS | Minimum 58.5 FPS |
| **Target Frame Time** | 16.67 ms | 16.67 ms | Maximum 17.1 ms |
| **Frame Time Variance (Jitter)** | < 1.5 ms | < 1.0 ms | Zero dropped frames under steady workload |
| **CPU Emulation Budget** | < 8.0 ms / frame | < 5.0 ms / frame | Zen 2 core headroom |
| **GPU Submission Budget** | < 6.0 ms / frame | < 3.5 ms / frame | D3D12 command list recording |
| **RAM Footprint (Series S)** | < 4,500 MiB Total | < 4,500 MiB Total | Hard ceiling before OS termination |
| **RAM Footprint (Series X)** | < 8,000 MiB Total | < 8,000 MiB Total | Expanded mode budget |
| **Audio Latency** | < 30 ms | < 30 ms | Underrun-free buffer queue |
| **Input Latency** | < 5 ms | < 5 ms | From gamepad poll to HID shared memory |

---

## 2. Profiling Subsystem (`nemu::core::profiler`)

The core engine embeds low-overhead telemetry hooks measuring execution times via hardware timestamp counters (`rdtsc` / `QueryPerformanceCounter`):

```cpp
struct FrameTimingStats {
    double cpu_time_ms;
    double gpu_record_time_ms;
    double gpu_execute_time_ms;
    double jit_compile_time_ms;
    double audio_mix_time_ms;
    double frame_time_ms;
    double frame_time_variance_ms;
    uint32_t pso_compile_count;
    uint32_t jit_block_compile_count;
};
```

---

## 3. Anti-Stutter & Pacing Strategies

1. **Fastmem Direct Mapping:** Eliminates software page-table lookups on guest load/store instructions, reducing CPU memory emulation overhead by 60-80%.
2. **Asynchronous Shader & PSO Compilation:** Shaders encountering cache misses compile in background worker threads while a lightweight fallback material renders temporarily, preventing presentation stutter.
3. **Block Linking in JIT:** Direct jumps between compiled basic blocks bypass the JIT dispatcher loop, reducing branch misprediction stalls.
4. **DXGI Flip-Model Presentation:** Implements `DXGI_SWAP_EFFECT_FLIP_DISCARD` with zero tearing and precise hardware frame synchronization.
