// SPDX-FileCopyrightText: NEMU Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Differential test for the per-title compat registry: sorted-order invariant,
// lookup hit/miss contract, and seed-data spot checks against the Ryujinx
// compat database source rows.

#include "core/cpu/title_compat.hpp"
#include <cassert>
#include <cstdio>
#include <algorithm>

using namespace nemu::core::cpu;

#define NEMU_TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "[FAIL] Assertion failed: " #cond " at %s:%d\n", __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

static void TestSortedInvariant() {
    const size_t n = sizeof(kKnownTitles) / sizeof(kKnownTitles[0]);
    for (size_t i = 1; i < n; ++i) {
        NEMU_TEST_ASSERT(kKnownTitles[i - 1].title_id < kKnownTitles[i].title_id &&
                         "table must be strictly sorted for binary search");
        NEMU_TEST_ASSERT(!kKnownTitles[i].name.empty());
    }
    std::printf("  PASS TestSortedInvariant (%zu titles)\n", n);
}

static void TestLookup() {
    // Hit: a known nvdec title from the seed.
    const TitleCompat* t = FindTitleCompat(0x0100ff500e34a000ULL); // Xenoblade DE
    NEMU_TEST_ASSERT(t && t->tweaks.nvdec_required);

    // Miss: unknown title and null id.
    NEMU_TEST_ASSERT(FindTitleCompat(0x0BAD0BAD0BAD0BADULL) == nullptr);
    NEMU_TEST_ASSERT(FindTitleCompat(0) == nullptr);

    // 32-bit flagged titles exist in the seed.
    bool saw32 = false;
    const size_t n = sizeof(kKnownTitles) / sizeof(kKnownTitles[0]);
    for (size_t i = 0; i < n && !saw32; ++i) saw32 = kKnownTitles[i].tweaks.is_32bit;
    NEMU_TEST_ASSERT(saw32);

    // Deadlock-flagged titles exist.
    bool sawDl = false;
    for (size_t i = 0; i < n && !sawDl; ++i) sawDl = kKnownTitles[i].tweaks.sync_relaxed;
    NEMU_TEST_ASSERT(sawDl);
    std::puts("  PASS TestLookup");
}

int main() {
    std::puts("========================================");
    std::puts("    NEMU TITLE COMPAT REGISTRY TESTS    ");
    std::puts("========================================");
    TestSortedInvariant();
    TestLookup();
    std::puts("ALL TITLE COMPAT TESTS PASSED SUCCESSFULLY!");
    return 0;
}
