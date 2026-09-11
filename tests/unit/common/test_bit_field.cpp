#include "core/common/bit_field.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core::common;

#define BF_ASSERT_FIRST_(a, ...) a
#define BF_ASSERT(...) \
    do { \
        if (!(BF_ASSERT_FIRST_(__VA_ARGS__))) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

// Model a guest status register with named compile-time bit fields.
using FieldA = BitField<0, 5, u32>;  // bits 0..4
using FieldB = BitField<5, 3, u32>;  // bits 5..7
using FlagC  = BitField<8, 1, u32>;  // bit 8

int main() {
    std::cout << "[Test: Compile-Time Bit Field]" << std::endl;

    // 1. Static Get/Set on a plain word, neighbors preserved.
    {
        u32 word = 0;
        word = FieldA::Set(word, 0b10101u);
        BF_ASSERT(FieldA::Get(word) == 0b10101u);
        word = FieldB::Set(word, 0b110u);
        BF_ASSERT(FieldB::Get(word) == 0b110u);
        BF_ASSERT(FieldA::Get(word) == 0b10101u, "neighbor field preserved");
        BF_ASSERT(word == ((0b110u << 5u) | 0b10101u));
    }

    // 2. Ref wrapper read/write on a live register.
    {
        u32 raw = 0;
        FieldA::Ref field_a(raw);
        FieldB::Ref field_b(raw);
        FlagC::Ref flag_c(raw);

        field_a = 0b10101u;
        field_b = 0b011u;
        flag_c = 1u;
        BF_ASSERT(raw == ((1u << 8u) | (0b011u << 5u) | 0b10101u));
        BF_ASSERT(field_a == 0b10101u, "reads via conversion operator");
        BF_ASSERT(field_b == 0b011u);
        BF_ASSERT(flag_c == 1u);

        // Mutating one field preserves the others.
        field_a = 0u;
        BF_ASSERT(field_b == 0b011u && flag_c == 1u, "writes preserve neighbors");
        BF_ASSERT(raw == ((1u << 8u) | (0b011u << 5u)));
    }

    // 3. Field masking at the edges of the storage word.
    {
        // 1-bit fields at the top of a u16.
        using Top = BitField<15, 1, u16>;
        u16 w = 0;
        Top::Ref t(w);
        t = 1;
        BF_ASSERT(Top::Get(w) == 1u && (w & 0x8000u) != 0);
        t = 0;
        BF_ASSERT(Top::Get(w) == 0u && w == 0u);
    }

    std::cout << "  - Compile-time bit-field tests: PASSED" << std::endl;
    std::cout << "[Test: Compile-Time Bit Field PASSED]" << std::endl;
    return 0;
}