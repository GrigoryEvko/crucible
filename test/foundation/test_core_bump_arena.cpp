// Tests of foundation/core/BumpArena.h: the allocations and the epoch.

#include <foundation/core/BumpArena.h>

#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::Address;
using ::foundation::core::address_of;
using ::foundation::core::BumpArena;
using ::foundation::core::View;

struct Pair {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

// One cache line: a value in its first word and zero in its last word.
struct alignas(64) Wide {
    std::uint64_t first = 0;
    std::uint64_t middle = 0;
    std::uint64_t last = 0;
};

// The address of the first element of a View that holds one.
template <class T>
[[nodiscard]] Address first_address(View<T> view) noexcept {
    return address_of(view.get(0).expect("the view holds an element"));
}

// count bytes of the arena, as the address of the first one.
[[nodiscard]] Address take(BumpArena& arena, std::size_t count) noexcept {
    return first_address(arena.array(::foundation::effects::testing::test().alloc, count, std::byte{0}));
}

void test_make_builds_the_object() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena arena{4096};
    auto const pair = arena.make<Pair>(alloc, Pair{.key = 7, .count = 3, .flags = 1});
    CRUCIBLE_FATAL_INVARIANT(pair->key == 7 && pair->count == 3 && pair->flags == 1);
    auto const zeroed = arena.make<Pair>(alloc);
    CRUCIBLE_FATAL_INVARIANT(zeroed->key == 0 && zeroed->count == 0 && zeroed->flags == 0);
    zeroed->count = 9;
    CRUCIBLE_FATAL_INVARIANT((*zeroed).count == 9);
    CRUCIBLE_FATAL_INVARIANT(!(address_of(pair) == address_of(zeroed)));
    auto const wide = arena.make<Wide>(alloc);
    CRUCIBLE_FATAL_INVARIANT(address_of(wide).is_aligned<64>());
    CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == 4096);
}

void test_array_fills_each_element() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena arena{4096};
    View<std::uint32_t> const numbers = arena.array<std::uint32_t>(alloc, 1000, 7);
    CRUCIBLE_FATAL_INVARIANT(numbers.size() == 1000);
    std::uint64_t total = 0;
    for (std::uint32_t const element : numbers) {
        total += element;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 7000);
    *numbers.get(999).expect("the position is below the count") = 1;
    CRUCIBLE_FATAL_INVARIANT(*numbers.get(999).expect("the position is below the count") == 1);

    View<Wide> const wide = arena.array(alloc, 3, Wide{});
    CRUCIBLE_FATAL_INVARIANT(first_address(wide).is_aligned<64>());
    View<Pair> const pairs = arena.array(alloc, 5, Pair{.key = 2});
    for (Pair const& element : pairs) {
        CRUCIBLE_FATAL_INVARIANT(element.key == 2 && element.count == 0);
    }
}

// A count of zero gives the empty View and takes no block.
void test_empty_array_takes_no_bytes() {
    BumpArena arena{4096};
    View<std::uint64_t> const empty = arena.array<std::uint64_t>(::foundation::effects::testing::test().alloc, 0, 5);
    CRUCIBLE_FATAL_INVARIANT(empty.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == 0);
}

// A request larger than a block takes a block of its own size, and the arena
// then serves from that block.
void test_large_request_takes_its_own_block() {
    BumpArena arena{4096};
    static_cast<void>(take(arena, 1000));
    static_cast<void>(take(arena, 20000));
    CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() >= 4096 + 20000);
}

// The second epoch gets the addresses of the first epoch, and the arena
// takes no block for it.
void test_end_keeps_the_blocks() {
    BumpArena arena{4096};
    struct Addresses {
        Address small;
        Address large;
        Address after;
    };
    Addresses const first = arena.with_epoch(
        [&]() noexcept { return Addresses{take(arena, 1000), take(arena, 20000), take(arena, 1000)}; });
    std::size_t const retained = arena.retained_bytes();
    Addresses const second = arena.with_epoch(
        [&]() noexcept { return Addresses{take(arena, 1000), take(arena, 20000), take(arena, 1000)}; });
    CRUCIBLE_FATAL_INVARIANT(first.small == second.small);
    CRUCIBLE_FATAL_INVARIANT(first.large == second.large);
    CRUCIBLE_FATAL_INVARIANT(first.after == second.after);
    CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == retained);
}

// An end above the limit gives back each block but the first, and the arena
// then serves the next epoch.
void test_end_above_the_limit_keeps_one_block() {
    constexpr std::size_t block = std::size_t{1} << 20;
    BumpArena arena{block};
    arena.with_epoch([&]() noexcept {
        static_cast<void>(take(arena, 64));
        static_cast<void>(take(arena, BumpArena::retained_bytes_limit));
        CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() > BumpArena::retained_bytes_limit);
    });
    CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == block);
    arena.with_epoch([&]() noexcept { static_cast<void>(take(arena, 64)); });
    CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == block);
}

// A long run of epochs with a mix of sizes takes its blocks in its first
// period, and no later epoch adds a block.  The sizes repeat with a period
// of 3000 turns, because 7919 and 3000 have no common factor.
void test_long_run_keeps_its_blocks() {
    constexpr std::uint32_t period = 3000;
    BumpArena arena{4096};
    std::size_t retained_after_one_period = 0;
    for (std::uint32_t turn = 0; turn < 2 * period; ++turn) {
        std::size_t const count = 1 + (std::size_t{turn} * 7919) % period;
        arena.with_epoch([&]() noexcept {
            for (std::uint32_t index = 0; index < 8; ++index) {
                static_cast<void>(take(arena, count + std::size_t{index} * 512));
            }
        });
        if (turn == period - 1) {
            retained_after_one_period = arena.retained_bytes();
        }
    }
    CRUCIBLE_FATAL_INVARIANT(retained_after_one_period > 0);
    CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == retained_after_one_period);
    CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() <= BumpArena::retained_bytes_limit);
}

// Each wrong use ends the process: an epoch in an epoch of one arena, an
// epoch of an arena that holds bytes, an array past the largest View, and a
// block size of zero.  Each wrong use runs in a child through expect_dies of
// the Check family.
void test_wrong_uses_end_the_process(::fixy::TestRunnerCtx const& test) {
    BumpArena nested{4096};
    ::fixy::check::expect_dies(
        test, [&nested] noexcept { nested.with_epoch([&nested]() noexcept { nested.with_epoch([]() noexcept {}); }); },
        ::foundation::core::TextView{
            "foundation: fatal: an epoch of a BumpArena in an epoch of the same arena: end the first epoch "
            "before the next one opens"});

    BumpArena holds_bytes{4096};
    static_cast<void>(take(holds_bytes, 64));
    ::fixy::check::expect_dies(
        test, [&holds_bytes] noexcept { holds_bytes.with_epoch([]() noexcept {}); },
        ::foundation::core::TextView{
            "foundation: fatal: an epoch of a BumpArena that holds bytes: open the epoch before the first "
            "allocation"});

    BumpArena too_long{4096};
    ::fixy::check::expect_dies(
        test,
        [&too_long] noexcept {
            static_cast<void>(
                too_long.array<std::uint64_t>(::foundation::effects::testing::test().alloc, ~std::size_t{0} / 4, 0));
        },
        ::foundation::core::TextView{
            "foundation: fatal: an array of 4611686018427387903 elements of 8 bytes in a BumpArena is longer "
            "than the largest View"});

    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            BumpArena const zero{0};
            static_cast<void>(zero.retained_bytes());
        },
        ::foundation::core::TextView{
            "foundation: fatal: a BumpArena of blocks of zero bytes: give a block size above zero"});
}

// The property: arrays of random counts and alignments in random epochs sit
// at their alignment and never overlap.  Each array holds its own fill after
// each later array of the epoch, so a later array wrote none of its bytes.
// The fills of the epoch are the model.
void test_agrees_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDA7E4A0000001u};
    BumpArena arena{512};
    for (int trial = 0; trial < 64; ++trial) {
        arena.with_epoch([&]() noexcept {
            ::foundation::core::FixedArray<View<Wide>, 32> arrays{};
            for (auto [position, slot] : arrays.enumerate()) {
                std::size_t const count = 1 + stream.below(40);
                std::uint64_t const fill = position.raw() + 1;
                if (stream.below(2) == 0) {
                    View<std::uint32_t> const odd =
                        arena.array<std::uint32_t>(::foundation::effects::testing::test().alloc, 1, 0);
                    CRUCIBLE_FATAL_INVARIANT(first_address(odd).is_aligned<alignof(std::uint32_t)>());
                }
                *slot = arena.array(::foundation::effects::testing::test().alloc, count, Wide{.first = fill});
                CRUCIBLE_FATAL_INVARIANT(first_address(*slot).is_aligned<64>());
            }
            for (auto [position, slot] : arrays.enumerate()) {
                for (Wide const& line : *slot) {
                    CRUCIBLE_FATAL_INVARIANT(line.first == position.raw() + 1);
                    CRUCIBLE_FATAL_INVARIANT(line.last == 0);
                }
            }
        });
    }
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_make_builds_the_object();
    test_array_fills_each_element();
    test_empty_array_takes_no_bytes();
    test_large_request_takes_its_own_block();
    test_end_keeps_the_blocks();
    test_end_above_the_limit_keeps_one_block();
    test_long_run_keeps_its_blocks();
    test_wrong_uses_end_the_process(test);
    test_agrees_with_plain_model();
    return 0;
}
