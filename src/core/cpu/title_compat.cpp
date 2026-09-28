// SPDX-FileCopyrightText: NEMU Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "title_compat.hpp"
#include <algorithm>
#include <iterator>

namespace nemu::core::cpu {

const TitleCompat* FindTitleCompat(u64 title_id) noexcept {
    static constexpr size_t kCount = sizeof(kKnownTitles) / sizeof(kKnownTitles[0]);
    if (kCount == 0 || title_id == 0) return nullptr;

    const auto* first = kKnownTitles;
    const auto* last = kKnownTitles + kCount;
    const auto* it = std::lower_bound(first, last, title_id,
        [](const TitleCompat& t, u64 id) { return t.title_id < id; });
    if (it != last && it->title_id == title_id) return it;
    return nullptr;
}

} // namespace nemu::core::cpu
