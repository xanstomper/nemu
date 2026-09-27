#include "sass_identifier.hpp"
#include <array>
#include <cstddef>

namespace nemu::core::gpu::shader {

// SassOpcode must be at namespace scope (same entity as the header decl);
// only the encoding TABLE is TU-local.
#include "sass_opcode_table.inc" // defines SassOpcode + SassEncoding + kSassEncodings

namespace {
#include "sass_cute_names.inc" // parallel kSassCuteNames (TU-local)
} // namespace

SassInfo IdentifySass(u64 raw_inst) {
    for (size_t i = 0; i < kSassEncodings.size(); ++i) {
        const auto& e = kSassEncodings[i];
        if ((raw_inst & e.mask) == e.value) {
            return {e.opcode, i};
        }
    }
    // Unknown opcode: NOP family, no encoding index. Callers log + skip
    // (matches how a faulting card treats garbage words).
    return {SassOpcode::NOP, SIZE_MAX};
}

const char* SassCuteName(size_t encoding_index) {
    if (encoding_index < kSassCuteNames.size()) {
        return kSassCuteNames[encoding_index];
    }
    return "UNKNOWN";
}

} // namespace nemu::core::gpu::shader
