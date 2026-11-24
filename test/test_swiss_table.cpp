// The control-byte operations have one hand-written path per instruction
// set. Whichever the build selects is the one these cases run, so the file
// is only as wide as the machine it was built for.

#include <crucible/SwissTable.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::detail::kEmpty;
using crucible::detail::h2_tag;
using crucible::detail::BitMask;
using crucible::detail::CtrlGroup;

static constexpr size_t kGroupWidth = crucible::detail::group_width();

static void test_h2_tag_range() {
    // Top 7 bits = 0..127, always non-negative as int8_t.
    // Bit 7 cleared to leave 0x80 distinct as the empty marker.
    for (uint64_t h : {0ULL, 1ULL, 0xFFULL, 0xFE00000000000000ULL, ~0ULL, 0x7F80000000000000ULL}) {
        int8_t tag = h2_tag(h);
        CRUCIBLE_TEST_ASSERT(kTest, tag >= 0);
        CRUCIBLE_TEST_ASSERT(kTest, tag != kEmpty);
    }
    CRUCIBLE_TEST_ASSERT(kTest, h2_tag(0) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, h2_tag(~0ULL) == 0x7F);  // high 7 bits all set
    CRUCIBLE_TEST_ASSERT(kTest, h2_tag(1ULL << 57) == 0x01);
    ::fixy::check::pass(kTest, "  test_h2_tag_range:              PASSED\n");
}

static void test_bitmask_iteration() {
    static_assert(sizeof(BitMask) == sizeof(uint64_t));
    static_assert(sizeof(BitMask::Mask) == sizeof(uint64_t));

    BitMask m{0};
    CRUCIBLE_TEST_ASSERT(kTest, !static_cast<bool>(m));
    CRUCIBLE_TEST_ASSERT(kTest, m.raw() == 0);

    BitMask m2{0b10110u};
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(m2));
    CRUCIBLE_TEST_ASSERT(kTest, m2.lowest() == 1);
    m2.clear_lowest();
    CRUCIBLE_TEST_ASSERT(kTest, m2.lowest() == 2);
    m2.clear_lowest();
    CRUCIBLE_TEST_ASSERT(kTest, m2.lowest() == 4);
    m2.clear_lowest();
    CRUCIBLE_TEST_ASSERT(kTest, !static_cast<bool>(m2));

    BitMask m3{0xF0F0u};  // 8 bits set at positions 4,5,6,7,12,13,14,15
    uint32_t count = 0;
    uint32_t last = 0;
    while (m3) {
        last = m3.lowest();
        m3.clear_lowest();
        count++;
    }
    CRUCIBLE_TEST_ASSERT(kTest, count == 8);
    CRUCIBLE_TEST_ASSERT(kTest, last == 15);
    ::fixy::check::pass(kTest, "  test_bitmask_iteration:         PASSED\n");
}

static void test_group_match_empty() {
    alignas(64)::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
    ctrl.with_mut([](auto run) noexcept { ::fixy::fill(std::move(run), kEmpty); });
    auto g = CtrlGroup::load(ctrl.view());
    auto empty = g.match_empty();
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(empty));
    uint32_t n = 0;
    while (empty) {
        empty.clear_lowest();
        n++;
    }
    CRUCIBLE_TEST_ASSERT(kTest, n == kGroupWidth);

    // No empty slot: every byte is set to a non-empty tag.
    for (const auto index : ctrl.indices()) {
        int8_t const tag = static_cast<int8_t>(index.raw() & 0x7F);
        ctrl.set(index, tag);
    }
    g = CtrlGroup::load(ctrl.view());
    CRUCIBLE_TEST_ASSERT(kTest, !static_cast<bool>(g.match_empty()));
    ::fixy::check::pass(kTest, "  test_match_empty:               PASSED\n");
}

static void test_group_match_tag() {
    alignas(64)::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
    // Sparse pattern: set tag 0x42 at every 3rd position, empty elsewhere.
    for (const auto index : ctrl.indices()) {
        int8_t const tag = (index.raw() % 3 == 0) ? int8_t{0x42} : kEmpty;
        ctrl.set(index, tag);
    }
    auto g = CtrlGroup::load(ctrl.view());
    auto hits = g.match(0x42);
    uint32_t count = 0;
    while (hits) {
        uint32_t pos = hits.lowest();
        CRUCIBLE_TEST_ASSERT(kTest, pos % 3 == 0);  // every match must be on a multiple of 3
        hits.clear_lowest();
        count++;
    }
    const uint32_t expected = (kGroupWidth + 2) / 3;
    CRUCIBLE_TEST_ASSERT(kTest, count == expected);

    // Tag that appears nowhere.
    auto none = g.match(0x55);
    CRUCIBLE_TEST_ASSERT(kTest, !static_cast<bool>(none));
    ::fixy::check::pass(kTest, "  test_match_tag:                 PASSED\n");
}

static void test_match_single_position() {
    // Every position across the group width, one at a time, so that the
    // reduction from vector lanes to a bitmask is checked at each index.
    for (size_t pos = 0; pos < kGroupWidth; ++pos) {
        alignas(64)::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        for (const auto index : ctrl.indices()) {
            int8_t const tag = (index.raw() == pos) ? int8_t{0x3A} : kEmpty;
            ctrl.set(index, tag);
        }
        auto g = CtrlGroup::load(ctrl.view());
        auto hits = g.match(0x3A);
        CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(hits));
        CRUCIBLE_TEST_ASSERT(kTest, hits.lowest() == pos);
        hits.clear_lowest();
        CRUCIBLE_TEST_ASSERT(kTest, !static_cast<bool>(hits));
    }
    ::fixy::check::pass(kTest, "  test_position_coverage:         PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_swiss_table (kGroupWidth={}):\n", kGroupWidth);
    test_h2_tag_range();
    test_bitmask_iteration();
    test_group_match_empty();
    test_group_match_tag();
    test_match_single_position();
    ::fixy::check::pass(kTest, "test_swiss_table: 5 groups, all passed\n");
    return 0;
}
