#include "core/memory/address_space.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>

#define NEMU_T_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core::memory;

int main() {
    std::cout << "[Test: Host Address Space (fastmem backing)]" << std::endl;

    constexpr size_t kReserve = 4ull * 1024 * 1024 * 1024; // 4 GiB reserve

    {
        AddressSpace as;
        NEMU_T_ASSERT(!as.IsReserved());

        NEMU_T_ASSERT(as.Reserve(kReserve));
        NEMU_T_ASSERT(as.IsReserved());
        NEMU_T_ASSERT(as.GetReservationSize() == kReserve);
        NEMU_T_ASSERT(as.GetBase() != nullptr);
        NEMU_T_ASSERT(as.IsInRange(0x1000000, 0x4000));          // in range
        NEMU_T_ASSERT(!as.IsInRange(kReserve - 1, 2));            // crosses boundary
        NEMU_T_ASSERT(as.GetPointer(0x10000) == nullptr);         // not committed yet

        // Commit a few pages and get a direct host pointer.
        const vaddr_t base = 0x1000000;
        const size_t size = 0x4000;
        NEMU_T_ASSERT(as.Commit(base, size));
        u8* host = as.GetPointer(base);
        NEMU_T_ASSERT(host != nullptr);
        NEMU_T_ASSERT(host == as.GetBase() + base);               // 1:1 host mapping

        // Write through the host pointer (the fast-path access).
        std::memset(host, 0, size);
        host[0] = 0xAB;
        host[0x20] = 0xCD;
        NEMU_T_ASSERT(host[0] == 0xAB && host[0x20] == 0xCD);

        // An uncommitted gap still returns nullptr (would fault in real use).
        NEMU_T_ASSERT(as.GetPointer(0) == nullptr);
        NEMU_T_ASSERT(as.GetRangePointer(base, size) != nullptr);
        NEMU_T_ASSERT(as.GetRangePointer(base, size + 1) == nullptr); // crosses committed end

        // Commit a second region and verify both are independently backed.
        const vaddr_t base2 = 0x2000000;
        NEMU_T_ASSERT(as.Commit(base2, 0x1000));
        u8* host2 = as.GetPointer(base2);
        NEMU_T_ASSERT(host2 != nullptr && host2 != host);
        host2[0] = 0x77;
        NEMU_T_ASSERT(host[0] == 0xAB); // first region unaffected

        // Decommit the second region -> pointer becomes unavailable again.
        NEMU_T_ASSERT(as.Decommit(base2, 0x1000));
        NEMU_T_ASSERT(as.GetPointer(base2) == nullptr);
    }

    // Out-of-bound reserve request must fail.
    {
        AddressSpace as;
        NEMU_T_ASSERT(!as.Reserve(0));        // zero size
        NEMU_T_ASSERT(!as.Reserve(0x1234));   // not page aligned
    }

    std::cout << "  - Host address-space (fastmem backing) tests: PASSED" << std::endl;
    std::cout << "[Test: Host Address Space PASSED]" << std::endl;
    return 0;
}