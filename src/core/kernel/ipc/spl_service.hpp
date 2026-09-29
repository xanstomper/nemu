#pragma once

#include "ipc_service.hpp"
#include <string>

namespace nemu::core::kernel::ipc {

/// spl, spl:ssl, spl:mig, spl:fs - Security / Cryptography Service.
/// Used for hardware cryptographic random numbers and key derivation.
class SplService final : public IIpcService {
public:
    explicit SplService(std::string name = "spl");
    ~SplService() override = default;

    enum Commands : u32 {
        GetRandomBytes = 0,
        GenerateAesKek = 1,
        GenerateAesKey = 2,
        GenerateKey = 3,
        ComputeCmac = 4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
