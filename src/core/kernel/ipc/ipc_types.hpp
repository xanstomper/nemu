#pragma once

#include "core/types.hpp"
#include <cstring>
#include <algorithm>
#include <string_view>
#include <type_traits>

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
    Request      = 0x2, ///< Request an action on a service interface (cmd + in/out buffers).
    Control      = 0x6, ///< Control request against the service (domain / metadata).
    Close        = 0xF, ///< Close the session.
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