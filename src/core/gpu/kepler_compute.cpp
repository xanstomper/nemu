#include "kepler_compute.hpp"
#include "compute_qmd.hpp"
#include "gpu_interface.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <array>

namespace nemu::core::gpu {

KeplerCompute::KeplerCompute(memory::VirtualMemory* memory, IGpuBackend* backend)
    : memory_(memory), backend_(backend) {}

void KeplerCompute::Reset() {
    qmd_addr_ = 0;
}

void KeplerCompute::CallMethod(u32 method, u32 argument) {
    switch (method) {
    case REG_LAUNCH_DESC_LOC:
        qmd_addr_ = (qmd_addr_ & 0xFFFFFFFF00000000ULL) | argument;
        return;
    case REG_LAUNCH_DESC_LOC + 1:
        qmd_addr_ = (qmd_addr_ & 0x00000000FFFFFFFFULL) | (static_cast<u64>(argument) << 32);
        return;
    case REG_LAUNCH:
        ProcessLaunch(); // yuzu: launch register write -> ProcessLaunch()
        return;
    default:
        return; // const-buffer / code setup latched upstream; QMD carries the rest
    }
}

void KeplerCompute::ProcessLaunch() {
    if (qmd_addr_ == 0) {
        NEMU_LOG_WARN("KeplerCompute", "launch without QMD address");
        return;
    }

    // Read the 256-byte Queue Meta Data descriptor from guest memory (yuzu:
    // ReadBlockUnsafe(launch_desc_loc, &launch_description, NUM_LAUNCH_PARAMS*4)).
    std::array<u8, ComputeQmd::kByteSize> qmd_bytes{};
    if (!memory_->ReadBlock(qmd_addr_, qmd_bytes.data(), qmd_bytes.size())) {
        NEMU_LOG_WARN("KeplerCompute", "QMD unreadable at 0x{:X}", qmd_addr_);
        return;
    }
    const auto qmd = ComputeQmd::FromBytes(qmd_bytes);

    NEMU_LOG_INFO("KeplerCompute",
                  "dispatch grid=({},{},{}) block=({},{},{}) shared={}B @0x{:X}",
                  qmd.GridDimX(), qmd.GridDimY(), qmd.GridDimZ(),
                  qmd.BlockDimX(), qmd.BlockDimY(), qmd.BlockDimZ(),
                  qmd.SharedMemorySize(), qmd_addr_);

    if (backend_) {
        backend_->DispatchCompute(qmd.GridDimX(), qmd.GridDimY(), qmd.GridDimZ());
    }
    ++dispatch_count_;
}

} // namespace nemu::core::gpu
