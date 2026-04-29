// Tests for SwissTable.h — SIMD control-byte operations.
//
// Validates h2_tag range, BitMask iteration, CtrlGroup match / match_empty
// for every position across kGroupWidth.  Exercises all three SIMD paths
// (AVX-512, AVX2, SSE2, NEON, portable) through whichever the build uses.

#include <crucible/SwissTable.h>

#include "test_assert.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using crucible::detail::kEmpty;
using crucible::detail::kGroupWidth;
using crucible::detail::h2_tag;
using crucible::detail::BitMask;
using crucible::detail::CtrlGroup;

static void test_h2_tag_range() {
    // Top 7 bits = 0..127, always non-negative as int8_t.
    // Bit 7 cleared to leave 0x80 distinct as the empty marker.
    for (uint64_t h : {0ULL, 1ULL, 0xFFULL, 0xFE00'0000'0000'0000ULL,
                      ~0ULL, 0x7F80'0000'0000'0000ULL}) {
        int8_t tag = h2_tag(h);
        assert(tag >= 0);
        assert(tag != kEmpty);
    }
    // Specific value checks.
    assert(h2_tag(0) == 0);
    assert(h2_tag(~0ULL) == 0x7F);  // high 7 bits all set
    assert(h2_tag(1ULL << 57) == 0x01);
    std::printf("  test_h2_tag_range:              PASSED\n");
}

static void test_bitmask_iteration() {
    BitMask m{.mask = 0};
    assert(!static_cast<bool>(m));

    BitMask m2{.mask = 0b10110u};
    assert(static_cast<bool>(m2));
    assert(m2.lowest() == 1);
    m2.clear_lowest();
    assert(m2.lowest() == 2);
    m2.clear_lowest();
    assert(m2.lowest() == 4);
    m2.clear_lowest();
    assert(!static_cast<bool>(m2));

    // Iteration pattern from the Swiss table hot path.
    BitMask m3{.mask = 0xF0F0u};  // 8 bits set at positions 4,5,6,7,12,13,14,15
    uint32_t count = 0;
    uint32_t last = 0;
    while (m3) {
        last = m3.lowest();
        m3.clear_lowest();
        count++;
    }
    assert(count == 8);
    assert(last == 15);
    std::printf("  test_bitmask_iteration:         PASSED\n");
}

static void test_group_match_empty() {
    // All-empty group — match_empty returns a full mask.
    alignas(64) int8_t ctrl[kGroupWidth];
    for (size_t i = 0; i < kGroupWidth; ++i) ctrl[i] = kEmpty;
    auto g = CtrlGroup::load(ctrl);
    auto empty = g.match_empty();
    assert(static_cast<bool>(empty));
    uint32_t n = 0;
    while (empty) { empty.clear_lowest(); n++; }
    assert(n == kGroupWidth);

    // No empty slots — all set to non-empty H2 tags.
    for (size_t i = 0; i < kGroupWidth; ++i) ctrl[i] = static_cast<int8_t>(i & 0x7F);
    g = CtrlGroup::load(ctrl);
    assert(!static_cast<bool>(g.match_empty()));
    std::printf("  test_match_empty:               PASSED\n");
}

static void test_group_match_tag() {
    alignas(64) int8_t ctrl[kGroupWidth];
    // Sparse pattern: set tag 0x42 at every 3rd position, empty elsewhere.
    for (size_t i = 0; i < kGroupWidth; ++i) {
        ctrl[i] = (i % 3 == 0) ? int8_t{0x42} : kEmpty;
    }
    auto g = CtrlGroup::load(ctrl);
    auto hits = g.match(0x42);
    uint32_t count = 0;
    while (hits) {
        uint32_t pos = hits.lowest();
        assert(pos % 3 == 0);  // every match must be on a multiple of 3
        hits.clear_lowest();
        count++;
    }
    const uint32_t expected = (kGroupWidth + 2) / 3;
    assert(count == expected);

    // Tag that appears nowhere.
    auto none = g.match(0x55);
    assert(!static_cast<bool>(none));
    std::printf("  test_match_tag:                 PASSED\n");
}

static void test_match_single_position() {
    // Exercise every individual position across the group width to
    // confirm the movemask / vpaddl reduction is positionally correct.
    for (size_t pos = 0; pos < kGroupWidth; ++pos) {
        alignas(64) int8_t ctrl[kGroupWidth];
        for (size_t i = 0; i < kGroupWidth; ++i)
            ctrl[i] = (i == pos) ? int8_t{0x3A} : kEmpty;
        auto g = CtrlGroup::load(ctrl);
        auto hits = g.match(0x3A);
        assert(static_cast<bool>(hits));
        assert(hits.lowest() == pos);
        hits.clear_lowest();
        assert(!static_cast<bool>(hits));
    }
    std::printf("  test_position_coverage:         PASSED\n");
}

int main() {
    std::printf("test_swiss_table (kGroupWidth=%zu):\n", kGroupWidth);
    test_h2_tag_range();
    test_bitmask_iteration();
    test_group_match_empty();
    test_group_match_tag();
    test_match_single_position();
    std::printf("test_swiss_table: 5 groups, all passed\n");
    return 0;
}
