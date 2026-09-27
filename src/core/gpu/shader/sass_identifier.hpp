#pragma once

#include "core/types.hpp"
#include <array>
#include <cstdint>

namespace nemu::core::gpu::shader {

// SassOpcode enum + SassEncoding struct defined by the generated table.
// The full encoding table stays TU-local in sass_identifier.cpp; the header
// only needs the enum type for SassInfo. Include the table header-only would
// duplicate 279 rows per TU, so we re-declare the enum here and keep the
// definition and header decl in sync via static_assert in the .cpp.
enum class MaxwellOpcode : u32;
enum class SassOpcode : u32;

struct SassInfo {
    SassOpcode opcode;
    size_t encoding_index; // index into kSassEncodings, SIZE_MAX if unknown
};

/// Identify the major opcode of a 64-bit Maxwell SASS instruction word.
SassInfo IdentifySass(u64 raw_inst);

/// Human-readable instruction family name (e.g. "FFMA (reg)").
const char* SassCuteName(size_t encoding_index);

/// Map a SASS instruction word directly to its corresponding MaxwellOpcode.
MaxwellOpcode IdentifyMaxwell(u64 raw_inst);

} // namespace nemu::core::gpu::shader
