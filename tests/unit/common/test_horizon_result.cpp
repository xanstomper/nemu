#include "core/common/horizon_result.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core::common;

#define HR_ASSERT_FIRST_(a, ...) a
#define HR_ASSERT(...) \
    do { \
        if (!(HR_ASSERT_FIRST_(__VA_ARGS__))) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main() {
    std::cout << "[Test: Horizon Result Code]" << std::endl;

    // 1. Success is raw 0.
    {
        const ResultCode ok = ResultCode::Success();
        HR_ASSERT(ok.IsSuccess() && !ok.IsError());
        HR_ASSERT(ok.raw() == 0);
        HR_ASSERT(ok == ResultCode(0u));
    }

    // 2. Packing known real-world values round-trips.
    {
        // Kern:Module (module 6), ResultInvalidHandle description 1.
        const ResultCode invalid_handle(6u, 1u, /*level=*/1u);
        HR_ASSERT(invalid_handle.IsError() && !invalid_handle.IsSuccess());
        HR_ASSERT(invalid_handle.GetModule() == 6u);
        HR_ASSERT(invalid_handle.GetDescription() == 1u);
        HR_ASSERT(invalid_handle.GetLevel() == 1u);
        // raw = (6<<10) | (1<<9) | 1
        HR_ASSERT(invalid_handle.raw() == ((6u << 10u) | (1u << 9u) | 1u));
    }

    // 3. Field round-trip through the raw word (module+desc+level survive).
    {
        const ResultCode r(11u, 99u, 1u);
        HR_ASSERT(r.GetModule() == 11u && r.GetDescription() == 99u && r.GetLevel() == 1u);
        // Reconstruct from raw behaves identically.
        const ResultCode from_raw(r.raw());
        HR_ASSERT(from_raw == r);
        HR_ASSERT(from_raw.GetModule() == 11u && from_raw.GetDescription() == 99u);
    }

    // 4. Masking: module 2047 stays within 11 bits, description 511 within 9 bits.
    {
        const ResultCode big(2047u, 511u, 1u);
        HR_ASSERT(big.GetModule() == 2047u);
        HR_ASSERT(big.GetDescription() == 511u);
        // Out-of-range module (>2047) is masked, not stored.
        const ResultCode over(4096u, 5u, 1u);
        HR_ASSERT(over.GetModule() == 0u, "module masked to 11 bits");
        HR_ASSERT(over.GetDescription() == 5u);
    }

    // 5. Inequality.
    {
        const ResultCode a(1u, 0u, 1u), b(1u, 1u, 1u);
        HR_ASSERT(a != b);
        HR_ASSERT(a != ResultCode::Success());
    }

    std::cout << "  - Horizon result-code tests: PASSED" << std::endl;
    std::cout << "[Test: Horizon Result Code PASSED]" << std::endl;
    return 0;
}