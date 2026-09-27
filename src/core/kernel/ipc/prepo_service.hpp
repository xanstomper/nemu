#pragma once

#include "ipc_service.hpp"
#include <string>

namespace nemu::core::kernel::ipc {

/// prepo:u, prepo:a, prepo:m - Play Report (Telemetry) service.
class PrepoService final : public IIpcService {
public:
    explicit PrepoService(std::string name = "prepo:u");
    ~PrepoService() override = default;

    enum Commands : u32 {
        SaveReportOld = 10100,
        SaveReportWithUserOld = 10101,
        SaveReportOld2 = 10102,
        SaveReportWithUserOld2 = 10103,
        SaveReport = 10106,
        SaveReportWithUser = 10107,
        RequestImmediateTransmission = 10200,
        GetTransmissionStatus = 10300,
        GetSystemSessionId = 10400,
        SaveSystemReport = 20102,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] u64 GetReportCount() const noexcept { return report_count_; }

private:
    u64 report_count_{0};
};

} // namespace nemu::core::kernel::ipc
