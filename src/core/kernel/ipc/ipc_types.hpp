#pragma once

#include "core/types.hpp"
#include <cstring>
#include <algorithm>
#include <string_view>
#include <type_traits>
#include <vector>

namespace nemu::core::kernel::ipc {

/// High-Level (HLE) IPC command-buffer contract.
///
/// Real Horizon places a 0x100-byte IPC command buffer in the thread-local
/// storage (TLS) block at offset +0x100. svcSendSyncRequest hands that buffer
/// to the session's service, which parses the HLE command and writes the reply
/// back into the same buffer. This engine models that ABI so guest code calling
/// through libnx's ipc layout interoperates with the HLE service handlers.

/// Offset, relative to the TLS base, at which the IPC command buffer lives.
inline constexpr size_t IpcBufferOffsetTls = 0x100;

/// Size of the IPC command buffer in the TLS area (matches Horizon).
inline constexpr size_t IpcBufferSize = 0x100;

/// Fixed header fields of an HLE IPC command buffer.
enum class IpcField : size_t {
    Type      = 0x00, // u32   command type
    Reserved0 = 0x04, // u32   reserved (send-static count / padding)
    DataSize  = 0x08, // u64   total payload size present in the buffer
    XId       = 0x10, // u32   service-specific command id
    Reserved1 = 0x14, // u32   reserved (buffer descriptor count)
    Payload   = 0x18, // ...   handler-specific command arguments / reply data
};

/// HLE IPC command types (subset of the Horizon CMIF type field).
enum class IpcCommandType : u32 {
    Invalid       = 0x0,
    Request       = 0x2, ///< Request an action on a service interface.
    Control       = 0x3, ///< Control request
    DomainRequest = 0x4, ///< Domain command request (SendMessage / CloseVirtualHandle).
    DomainControl = 0x5, ///< Domain control
    ControlCmd    = 0x6, ///< Control request (ConvertSessionToDomain, DuplicateSessionEx, QueryPointerBufferSize).
    Close         = 0xF, ///< Close the session.
};

enum class IpcBufferType : u32 {
    X_Pointer,  ///< Type X: In pointer buffer
    A_Send,     ///< Type A: In data buffer
    B_Receive,  ///< Type B: Out data buffer
    C_Receive,  ///< Type C: Out pointer buffer
    W_Exchange, ///< Type W: In/Out buffer
};

struct IpcBufferDescriptor {
    IpcBufferType type{IpcBufferType::A_Send};
    vaddr_t address{0};
    size_t size{0};
    u32 flags{0};
};

/// Reader wrapper over a guest-resident command buffer. Exposes type-safe
/// primitive access so service handlers do not reason about endianness or
/// alignment directly. Reading past `IpcBufferSize` is guarded.
class IpcRequestReader {
public:
    /// `base` must point at the IPC buffer (TLS + 0x100).
    explicit IpcRequestReader(const u8* base) : base_(base) {}

    [[nodiscard]] u32 GetType() const noexcept { return PeekU32(IpcField::Type); }
    [[nodiscard]] u32 GetXId() const noexcept { return PeekU32(IpcField::XId); }

    [[nodiscard]] u64 GetDataSize() const noexcept {
        u32 lo = PeekU32(IpcField::DataSize);
        u32 hi = PeekU32(static_cast<size_t>(IpcField::DataSize) + 4);
        return (static_cast<u64>(hi) << 32) | lo;
    }

    /// Read a primitive at a byte offset relative to the buffer base.
    template <typename T>
    T Read(size_t offset) const {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        if (offset + sizeof(T) > IpcBufferSize) {
            return T{0};
        }
        T value{0};
        for (size_t i = 0; i < sizeof(T); ++i) {
            value |= static_cast<T>(base_[offset + i]) << (8 * i);
        }
        return value;
    }

    /// Read a primitive from the Payload field.
    template <typename T>
    T Payload(size_t offset = 0) const {
        return Read<T>(static_cast<size_t>(IpcField::Payload) + offset);
    }

    /// Read an N-byte string stored inline in the payload (up to buffer bound).
    [[nodiscard]] std::string_view ReadString(size_t string_len, size_t offset = 0) const {
        const size_t start = static_cast<size_t>(IpcField::Payload) + offset;
        if (start >= IpcBufferSize) return {};
        const size_t avail = IpcBufferSize - start;
        const size_t n = std::min(string_len, avail);
        return std::string_view(reinterpret_cast<const char*>(base_) + start, n);
    }

    /// Return the raw CMIF command type (low 16 bits of word 0)
    [[nodiscard]] IpcCommandType GetCommandType() const noexcept {
        return static_cast<IpcCommandType>(PeekU32(0) & 0xFFFF);
    }

    /// Check if this is a domain command
    [[nodiscard]] bool IsDomainRequest() const noexcept {
        return GetCommandType() == IpcCommandType::DomainRequest;
    }

    /// For domain requests: 1 = SendMessage, 2 = CloseVirtualHandle
    [[nodiscard]] u8 GetDomainCommandType() const noexcept {
        return Payload<u8>(0);
    }

    /// For domain requests: target domain object ID
    [[nodiscard]] u32 GetDomainObjectId() const noexcept {
        return Payload<u32>(4);
    }

    /// Extract buffer descriptors passed in this IPC command
    [[nodiscard]] std::vector<IpcBufferDescriptor> GetBufferDescriptors() const {
        std::vector<IpcBufferDescriptor> desc_list;
        const u32 w0 = PeekU32(0);
        const u32 w1 = PeekU32(4);

        const u32 num_x = (w0 >> 16) & 0x0F;
        const u32 num_a = (w0 >> 20) & 0x0F;
        const u32 num_b = (w0 >> 24) & 0x0F;
        const u32 num_w = (w0 >> 28) & 0x0F;
        const u32 c_flags = (w1 >> 10) & 0x0F;
        const bool has_handle_desc = ((w1 >> 31) & 0x1) != 0;

        size_t cur_offset = 8;
        if (has_handle_desc) {
            u32 h_hdr = Read<u32>(cur_offset);
            cur_offset += 4;
            u32 copy_count = (h_hdr >> 1) & 0x0F;
            u32 move_count = (h_hdr >> 5) & 0x0F;
            cur_offset += (copy_count + move_count) * 4;
        }

        // Parse Type X (Pointer) descriptors: 2 words each
        for (u32 i = 0; i < num_x && cur_offset + 8 <= IpcBufferSize; ++i) {
            u32 word0 = Read<u32>(cur_offset);
            u32 word1 = Read<u32>(cur_offset + 4);
            cur_offset += 8;
            u64 addr = (static_cast<u64>((word0 >> 16) & 0x07) << 36) |
                       (static_cast<u64>((word0 >> 24) & 0x0F) << 32) |
                       static_cast<u64>(word1);
            size_t size = static_cast<size_t>(word0 & 0xFFFF);
            desc_list.push_back({IpcBufferType::X_Pointer, addr, size, 0});
        }

        // Parse Type A (Send) descriptors: 3 words each
        for (u32 i = 0; i < num_a && cur_offset + 12 <= IpcBufferSize; ++i) {
            u32 word0 = Read<u32>(cur_offset);
            u32 word1 = Read<u32>(cur_offset + 4);
            u32 word2 = Read<u32>(cur_offset + 8);
            cur_offset += 12;
            u64 addr = (static_cast<u64>((word2 >> 24) & 0x0F) << 32) | static_cast<u64>(word1);
            size_t size = (static_cast<size_t>((word2 >> 28) & 0x0F) << 32) | static_cast<size_t>(word0);
            desc_list.push_back({IpcBufferType::A_Send, addr, size, word2 & 0x03});
        }

        // Parse Type B (Receive) descriptors: 3 words each
        for (u32 i = 0; i < num_b && cur_offset + 12 <= IpcBufferSize; ++i) {
            u32 word0 = Read<u32>(cur_offset);
            u32 word1 = Read<u32>(cur_offset + 4);
            u32 word2 = Read<u32>(cur_offset + 8);
            cur_offset += 12;
            u64 addr = (static_cast<u64>((word2 >> 24) & 0x0F) << 32) | static_cast<u64>(word1);
            size_t size = (static_cast<size_t>((word2 >> 28) & 0x0F) << 32) | static_cast<size_t>(word0);
            desc_list.push_back({IpcBufferType::B_Receive, addr, size, word2 & 0x03});
        }

        // Parse Type W (Exchange) descriptors: 3 words each
        for (u32 i = 0; i < num_w && cur_offset + 12 <= IpcBufferSize; ++i) {
            u32 word0 = Read<u32>(cur_offset);
            u32 word1 = Read<u32>(cur_offset + 4);
            u32 word2 = Read<u32>(cur_offset + 8);
            cur_offset += 12;
            u64 addr = (static_cast<u64>((word2 >> 24) & 0x0F) << 32) | static_cast<u64>(word1);
            size_t size = (static_cast<size_t>((word2 >> 28) & 0x0F) << 32) | static_cast<size_t>(word0);
            desc_list.push_back({IpcBufferType::W_Exchange, addr, size, word2 & 0x03});
        }

        // Parse Type C (Receive pointer) descriptors: 2 words each
        if (c_flags > 1) {
            u32 num_c = c_flags - 1;
            for (u32 i = 0; i < num_c && cur_offset + 8 <= IpcBufferSize; ++i) {
                u32 word0 = Read<u32>(cur_offset);
                u32 word1 = Read<u32>(cur_offset + 4);
                cur_offset += 8;
                u64 addr = (static_cast<u64>(word1 & 0xFFFF) << 32) | static_cast<u64>(word0);
                size_t size = static_cast<size_t>(word1 >> 16);
                desc_list.push_back({IpcBufferType::C_Receive, addr, size, 0});
            }
        }

        return desc_list;
    }

private:
    [[nodiscard]] u32 PeekU32(IpcField field) const noexcept {
        return Read<u32>(static_cast<size_t>(field));
    }
    u32 PeekU32(size_t offset) const noexcept { return Read<u32>(offset); }

    const u8* base_;
};

/// Writer for an HLE IPC reply. Writes the result header (type + data size)
/// and inline payload back into the guest command buffer.
class IpcReplyWriter {
public:
    /// `base` must point at the IPC reply buffer (TLS + 0x100, same as the request).
    explicit IpcReplyWriter(u8* base) : base_(base) {}

    /// Set the reply command type and the payload size in the header.
    void Begin(u32 type, u64 payload_size) {
        Write<u32>(IpcField::Type, type);
        Write<u64>(IpcField::DataSize, payload_size);
    }

    /// Write a primitive at a byte offset relative to the buffer base.
    template <typename T>
    bool Write(size_t offset, T value) {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        if (offset + sizeof(T) > IpcBufferSize) {
            return false;
        }
        for (size_t i = 0; i < sizeof(T); ++i) {
            base_[offset + i] = static_cast<u8>((value >> (8 * i)) & 0xFF);
        }
        return true;
    }

    /// Write a primitive into the Payload field.
    template <typename T>
    bool Payload(size_t offset, T value) {
        return Write<T>(static_cast<size_t>(IpcField::Payload) + offset, value);
    }

    /// Write an N-byte string into the payload.
    [[nodiscard]] bool WriteString(size_t string_offset, std::string_view str) {
        const size_t start = static_cast<size_t>(IpcField::Payload) + string_offset;
        if (start >= IpcBufferSize) return false;
        const size_t cap = IpcBufferSize - start;
        const size_t n = std::min(str.size(), cap);
        std::memcpy(base_ + start, str.data(), n);
        return true;
    }

private:
    template <typename T>
    bool Write(IpcField field, T value) {
        return Write<T>(static_cast<size_t>(field), value);
    }

    u8* base_;
};

} // namespace nemu::core::kernel::ipc