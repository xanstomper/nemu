#pragma once

#include "ipc_service.hpp"
#include <string>
#include <memory>

#include <array>

namespace nemu::core::kernel::ipc {

#pragma pack(push, 1)
struct MiiCharInfo {
    std::array<u8, 16> create_id{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
    char16_t name[11]{u'N', u'e', u'm', u'u', 0}; // 22 bytes UTF-16
    u8 font_region{0};
    u8 favorite_color{0}; // Red
    u8 gender{0};         // Male
    u8 height{64};
    u8 build{64};
    u8 type{0};
    u8 region_move{0};
    u8 faceline_type{0};
    u8 faceline_color{0};
    u8 faceline_wrinkle{0};
    u8 faceline_make{0};
    u8 hair_type{0};
    u8 hair_color{0};
    u8 hair_flip{0};
    u8 eye_type{0};
    u8 eye_color{0};
    u8 eye_scale{4};
    u8 eye_aspect{3};
    u8 eye_rotate{4};
    u8 eye_x{2};
    u8 eye_y{12};
    u8 eyebrow_type{0};
    u8 eyebrow_color{0};
    u8 eyebrow_scale{4};
    u8 eyebrow_aspect{3};
    u8 eyebrow_rotate{4};
    u8 eyebrow_x{2};
    u8 eyebrow_y{10};
    u8 nose_type{0};
    u8 nose_scale{4};
    u8 nose_y{9};
    u8 mouth_type{0};
    u8 mouth_color{0};
    u8 mouth_scale{4};
    u8 mouth_aspect{3};
    u8 mouth_y{13};
    u8 mustache_type{0};
    u8 beard_type{0};
    u8 beard_color{0};
    u8 mustache_scale{4};
    u8 mustache_y{10};
    u8 glasses_type{0};
    u8 glasses_color{0};
    u8 glasses_scale{4};
    u8 glasses_y{10};
    u8 mole_type{0};
    u8 mole_scale{4};
    u8 mole_x{2};
    u8 mole_y{20};
    u8 reserved{0};
};
#pragma pack(pop)
static_assert(sizeof(MiiCharInfo) == 88, "MiiCharInfo must be exactly 88 bytes");

/// mii:u, mii:e - Nintendo Switch Mii Database Service.
/// Used by Mario Kart 8 Deluxe, Super Smash Bros Ultimate, Switch Sports, etc.
class MiiService final : public IIpcService {
public:
    explicit MiiService(std::string name = "mii:u");
    ~MiiService() override = default;

    enum Commands : u32 {
        IsFullDatabase = 0,
        GetCount = 1,
        Get = 2,
        Get1 = 3,
        Get2 = 4,
        GetDefault = 5,
        BuildRandom = 6,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

/// IDatabaseService subservice returned by MiiService
class MiiDatabaseSubService final : public IIpcService {
public:
    explicit MiiDatabaseSubService(std::string name = "mii:IDatabaseService");
    ~MiiDatabaseSubService() override = default;

    enum Commands : u32 {
        IsFullDatabase = 0,
        GetCount = 1,
        Get = 2,
        Get1 = 3,
        Get2 = 4,
        GetDefault = 5,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
