#pragma once

#include "core/types.hpp"
#include "instruction.hpp"

namespace nemu::core::cpu {

class Decoder {
public:
    static DecodedInstruction Decode(u32 raw) noexcept;

private:
    static DecodedInstruction DecodeBranches(u32 raw) noexcept;
    static DecodedInstruction DecodeDataProcImm(u32 raw) noexcept;
    static DecodedInstruction DecodeDataProcReg(u32 raw) noexcept;
    static DecodedInstruction DecodeLoadStore(u32 raw) noexcept;
    static DecodedInstruction DecodeSystem(u32 raw) noexcept;
};

} // namespace nemu::core::cpu
