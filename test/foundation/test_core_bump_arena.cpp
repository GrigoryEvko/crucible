// Tests of foundation/core/BumpArena.h: the allocations, the brands and the
// epochs.

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
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::Address;
using ::foundation::core::address_of;
using ::foundation::core::ArenaRun;
using ::foundation::core::BumpArena;
using ::foundation::core::Epoch;
using ::foundation::core::EpochArena;
using ::foundation::core::ElementCount;
using ::foundation::core::EpochOf;
using ::foundation::core::Ref;
using ::foundation::core::RunStart;
using ::foundation::core::View;
using Bytes = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;

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

// The family of the arenas of the tests that keep an arena past one scope.
struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

// The owner of one epoch arena.  Each test holds its own owner, and two
// owners share the brand of the arena.
struct Scratch {
    struct[[= ::foundation::brand::arena_member_brand{}]] Arena final {};

    explicit Scratch(::foundation::core::ArenaBlockSize block) noexcept
        : arena{::foundation::core::mint_epoch_arena<Arena>(block)} {}

    EpochArena<Arena> arena;
};

using ScratchEpoch = Epoch<EpochOf<Scratch::Arena>>;

// The address of the first element of a View that holds one.
template <class T>
[[nodiscard]] Address first_address(View<T> view) noexcept {
    return address_of(view.get(0).expect("the view holds an element"));
}

// count bytes of the arena or the epoch, as the address of the first one.
template <class Arena>
[[nodiscard]] Address take(Arena& arena, std::size_t count) noexcept {
    auto const run = arena.array(::foundation::effects::testing::test().alloc, count, std::byte{0});
    return first_address(run.view());
}

void test_make_builds_the_object() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::with_bump_arena(alloc, Bytes{4096}, [alloc](auto& arena) noexcept {
        auto const pair = arena.template make<Pair>(alloc, Pair{.key = 7, .count = 3, .flags = 1});
        CRUCIBLE_FATAL_INVARIANT(pair->key == 7 && pair->count == 3 && pair->flags == 1);
        auto const zeroed = arena.template make<Pair>(alloc);
        CRUCIBLE_FATAL_INVARIANT(zeroed->key == 0 && zeroed->count == 0 && zeroed->flags == 0);
        zeroed->count = 9;
        CRUCIBLE_FATAL_INVARIANT((*zeroed).count == 9);
        CRUCIBLE_FATAL_INVARIANT(!(address_of(pair) == address_of(zeroed)));
        auto const wide = arena.template make<Wide>(alloc);
        CRUCIBLE_FATAL_INVARIANT(address_of(wide).template is_aligned<64>());
        CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == 4096);
    });
}

void test_array_fills_each_element() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::with_bump_arena(alloc, Bytes{4096}, [alloc](auto& arena) noexcept {
        auto numbers_run = arena.template array<std::uint32_t>(alloc, 1000, 7);
        View<std::uint32_t> const numbers = numbers_run.view();
        CRUCIBLE_FATAL_INVARIANT(numbers.size() == 1000 && numbers_run.size() == 1000);
        std::uint64_t total = 0;
        for (std::uint32_t const element : numbers) {
            total += element;
        }
        CRUCIBLE_FATAL_INVARIANT(total == 7000);
        *numbers.get(999).expect("the position is below the count") = 1;
        CRUCIBLE_FATAL_INVARIANT(*numbers.get(999).expect("the position is below the count") == 1);

        auto const wide = arena.array(alloc, 3, Wide{});
        CRUCIBLE_FATAL_INVARIANT(first_address(wide.view()).template is_aligned<64>());
        auto const pairs = arena.array(alloc, 5, Pair{.key = 2});
        for (Pair const& element : pairs.view()) {
            CRUCIBLE_FATAL_INVARIANT(element.key == 2 && element.count == 0);
        }
    });
}

// A window of a run names the elements of its part, with the brand of the
// run, and a window past the end gives no run.  A run of const reads the same
// elements.
void test_window_keeps_the_run() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<TestFamily> arena = ::foundation::core::mint_bump_arena<TestFamily>(Bytes{4096});
    ArenaRun<std::uint32_t, TestFamily> numbers = arena.array<std::uint32_t>(alloc, 8, 0);
    std::uint32_t next = 0;
    for (std::uint32_t& element : numbers.view()) {
        element = next++;
    }
    ArenaRun<std::uint32_t, TestFamily> const part =
        numbers.window(RunStart{ElementCount{2}}, ElementCount{3}).expect("the window fits in the run");
    CRUCIBLE_FATAL_INVARIANT(part.size() == 3);
    CRUCIBLE_FATAL_INVARIANT(*part.view().get(0).expect("a window of three holds element 0") == 2);
    CRUCIBLE_FATAL_INVARIANT(*part.view().get(2).expect("a window of three holds element 2") == 4);
    CRUCIBLE_FATAL_INVARIANT(numbers.window(RunStart{ElementCount{6}}, ElementCount{3}).is_none());
    CRUCIBLE_FATAL_INVARIANT(numbers.window(RunStart{ElementCount{8}}, ElementCount{0}).is_some());
    ArenaRun<std::uint32_t const, TestFamily> const read = part;
    CRUCIBLE_FATAL_INVARIANT(address_of(::foundation::core::to(*read.view().get(1).expect("element 1")))
                             == address_of(::foundation::core::to(*part.view().get(1).expect("element 1"))));
}

// A count of zero gives the empty run and takes no block.
void test_empty_array_takes_no_bytes() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::with_bump_arena(alloc, Bytes{4096}, [alloc](auto& arena) noexcept {
        auto const empty = arena.template array<std::uint64_t>(alloc, 0, 5);
        CRUCIBLE_FATAL_INVARIANT(empty.size() == 0 && empty.view().size() == 0);
        CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == 0);
    });
}

// An element that copies only from an element that is not const, as a class
// that holds a Link does.  array cannot fill a run with it.
struct Numbered {
    std::uint64_t position = 0;
    std::uint64_t order = 0;

    constexpr Numbered(std::uint64_t at, std::uint64_t made) noexcept : position{at}, order{made} {}
    Numbered(Numbered&) noexcept = default;
    Numbered(Numbered&&) noexcept = default;
    Numbered& operator=(Numbered&) noexcept = default;
    Numbered& operator=(Numbered&&) noexcept = default;
    ~Numbered() = default;
};

// build_array builds each element in its place from the value that make
// gives for its position, from the first position to the last.  A count of
// zero calls make no time and takes no bytes.  An epoch builds its runs in
// the same way.
void test_build_array_builds_each_element() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<TestFamily> arena = ::foundation::core::mint_bump_arena<TestFamily>(Bytes{4096});
    std::uint64_t calls = 0;
    ArenaRun<Numbered, TestFamily> run =
        arena.build_array<Numbered>(alloc, ElementCount{5}, [&calls](RunStart at) noexcept {
            std::uint64_t const made = calls++;
            return Numbered{at.value().raw(), made};
        });
    CRUCIBLE_FATAL_INVARIANT(run.size() == 5 && calls == 5);
    std::uint64_t expected = 0;
    for (Numbered const& element : run.view()) {
        CRUCIBLE_FATAL_INVARIANT(element.position == expected && element.order == expected);
        ++expected;
    }

    ::foundation::core::with_bump_arena(alloc, Bytes{4096}, [alloc, &calls](auto& scoped) noexcept {
        auto const empty = scoped.template build_array<Numbered>(alloc, ElementCount{0}, [&calls](RunStart) noexcept {
            ++calls;
            return Numbered{0, 0};
        });
        CRUCIBLE_FATAL_INVARIANT(empty.size() == 0 && scoped.retained_bytes() == 0);
    });
    CRUCIBLE_FATAL_INVARIANT(calls == 5);

    Scratch scratch{Bytes{4096}};
    std::uint64_t const total = scratch.arena.with_epoch([alloc](ScratchEpoch& epoch) noexcept {
        auto const lines = epoch.build_array<Wide>(alloc, ElementCount{3}, [](RunStart at) noexcept {
            return Wide{.first = at.value().raw() + 1, .middle = 0, .last = 0};
        });
        std::uint64_t sum = 0;
        for (Wide const& line : lines.view()) {
            sum += line.first;
        }
        return sum;
    });
    CRUCIBLE_FATAL_INVARIANT(total == 6);
}

// A request larger than a block takes a block of its own size, and the arena
// then serves from that block.
void test_large_request_takes_its_own_block() {
    ::foundation::core::with_bump_arena(::foundation::effects::testing::test().alloc, Bytes{4096},
                                        [](auto& arena) noexcept {
                                            static_cast<void>(take(arena, 1000));
                                            static_cast<void>(take(arena, 20000));
                                            CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() >= 4096 + 20000);
                                        });
}

// An arena of a family tag lives past one scope, alone and in a Box.  Each
// Ref and each run carries the tag, and the Box builds the arena in its own
// storage, so a Ref of the boxed arena stays valid after a move of the Box.
void test_family_arena_lives_in_a_box() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<TestFamily> arena = ::foundation::core::mint_bump_arena<TestFamily>(Bytes{4096});
    Ref<int, TestFamily> const seven = arena.make<int>(alloc, 7);
    ArenaRun<std::uint32_t, TestFamily> const run = arena.array(alloc, std::size_t{4}, std::uint32_t{3});
    CRUCIBLE_FATAL_INVARIANT(*seven == 7 && run.size() == 4);

    ::foundation::core::Box<BumpArena<TestFamily>> boxed =
        ::foundation::core::mint_box<BumpArena<TestFamily>>(alloc, Bytes{4096});
    Ref<int, TestFamily> const eight = boxed->make<int>(alloc, 8);
    ::foundation::core::Box<BumpArena<TestFamily>> moved = std::move(boxed);
    CRUCIBLE_FATAL_INVARIANT(*eight == 8 && moved->retained_bytes() == 4096);
}

// A position of an object names the same object in its arena, in the first
// block and in a later block.  A position gives no object in another arena
// of the same family, and an object of another arena has no position in
// this arena.
void test_position_names_its_object_in_its_arena() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::Box<BumpArena<TestFamily>> held =
        ::foundation::core::mint_box<BumpArena<TestFamily>>(alloc, Bytes{4096});
    ::foundation::core::Box<BumpArena<TestFamily>> other =
        ::foundation::core::mint_box<BumpArena<TestFamily>>(alloc, Bytes{4096});
    Ref<Pair, TestFamily> const first = held->make<Pair>(alloc, Pair{.key = 7, .count = 3, .flags = 1});
    static_cast<void>(take(*held, 20000));
    Ref<Pair, TestFamily> const later = held->make<Pair>(alloc, Pair{.key = 9, .count = 4, .flags = 2});
    Ref<Pair, TestFamily> const stranger = other->make<Pair>(alloc, Pair{.key = 5, .count = 1, .flags = 0});

    auto const first_position = held->position_of(first).expect("the arena made the first object");
    auto const later_position = held->position_of(later).expect("the arena made the later object");
    CRUCIBLE_FATAL_INVARIANT(!(first_position == later_position));
    CRUCIBLE_FATAL_INVARIANT(held->position_of(stranger).is_none());

    // The Box moves, and the position names the same object, because the
    // arena keeps its blocks and its identity.
    ::foundation::core::Box<BumpArena<TestFamily>> moved = std::move(held);
    Ref<Pair, TestFamily> const first_again = moved->at(first_position).expect("the position is of this arena");
    Ref<Pair, TestFamily> const later_again = moved->at(later_position).expect("the position is of this arena");
    CRUCIBLE_FATAL_INVARIANT(address_of(first_again) == address_of(first) && first_again->key == 7);
    CRUCIBLE_FATAL_INVARIANT(address_of(later_again) == address_of(later) && later_again->key == 9);
    CRUCIBLE_FATAL_INVARIANT(other->at(first_position).is_none());
}

// The second epoch gets the addresses of the first epoch, and the arena
// takes no block for it.
void test_end_keeps_the_blocks() {
    Scratch scratch{Bytes{4096}};
    struct Addresses {
        Address small;
        Address large;
        Address after;
    };
    auto const fill = [](ScratchEpoch& epoch) noexcept {
        return Addresses{take(epoch, 1000), take(epoch, 20000), take(epoch, 1000)};
    };
    Addresses const first = scratch.arena.with_epoch(fill);
    std::size_t const retained = scratch.arena.retained_bytes();
    Addresses const second = scratch.arena.with_epoch(fill);
    CRUCIBLE_FATAL_INVARIANT(first.small == second.small);
    CRUCIBLE_FATAL_INVARIANT(first.large == second.large);
    CRUCIBLE_FATAL_INVARIANT(first.after == second.after);
    CRUCIBLE_FATAL_INVARIANT(scratch.arena.retained_bytes() == retained);
}

// An end above the limit gives back each block but the first, and the arena
// then serves the next epoch.  The body reads the arena through a const
// reference, which opens no epoch.
void test_end_above_the_limit_keeps_one_block() {
    constexpr std::size_t block = std::size_t{1} << 20;
    constexpr std::size_t limit = EpochArena<Scratch::Arena>::default_retained_bytes_limit;
    Scratch scratch{Bytes{block}};
    scratch.arena.with_epoch([&arena = std::as_const(scratch.arena)](ScratchEpoch& epoch) noexcept {
        static_cast<void>(take(epoch, 64));
        static_cast<void>(take(epoch, limit));
        CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() > limit);
    });
    CRUCIBLE_FATAL_INVARIANT(scratch.arena.retained_bytes() == block);
    scratch.arena.with_epoch([](ScratchEpoch& epoch) noexcept { static_cast<void>(take(epoch, 64)); });
    CRUCIBLE_FATAL_INVARIANT(scratch.arena.retained_bytes() == block);
}

// A retained limit that set_retained_limit sets refuses each block but the
// first at the end of an epoch above it.  The same epoch under the default
// limit keeps each block.
void test_set_limit_refuses_the_blocks_above_it() {
    Scratch kept{Bytes{4096}};
    CRUCIBLE_FATAL_INVARIANT(kept.arena.retained_limit()
                             == Bytes{EpochArena<Scratch::Arena>::default_retained_bytes_limit});
    kept.arena.with_epoch([](ScratchEpoch& epoch) noexcept { static_cast<void>(take(epoch, 20000)); });
    CRUCIBLE_FATAL_INVARIANT(kept.arena.retained_bytes() > 4096);

    Scratch limited{Bytes{4096}};
    limited.arena.set_retained_limit(Bytes{4096});
    CRUCIBLE_FATAL_INVARIANT(limited.arena.retained_limit() == Bytes{4096});
    limited.arena.with_epoch([&arena = std::as_const(limited.arena)](ScratchEpoch& epoch) noexcept {
        static_cast<void>(take(epoch, 1000));
        static_cast<void>(take(epoch, 20000));
        CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() > 4096);
    });
    CRUCIBLE_FATAL_INVARIANT(limited.arena.retained_bytes() == 4096);

    Scratch zero{Bytes{4096}};
    zero.arena.set_retained_limit(Bytes{0});
    zero.arena.with_epoch([](ScratchEpoch& epoch) noexcept { static_cast<void>(take(epoch, 64)); });
    CRUCIBLE_FATAL_INVARIANT(zero.arena.retained_bytes() == 4096);
}

// A long run of epochs with a mix of sizes takes its blocks in its first
// period, and no later epoch adds a block.  The sizes repeat with a period
// of 3000 turns, because 7919 and 3000 have no common factor.
void test_long_run_keeps_its_blocks() {
    constexpr std::uint32_t period = 3000;
    Scratch scratch{Bytes{4096}};
    std::size_t retained_after_one_period = 0;
    for (std::uint32_t turn = 0; turn < 2 * period; ++turn) {
        std::size_t const count = 1 + (std::size_t{turn} * 7919) % period;
        scratch.arena.with_epoch([count](ScratchEpoch& epoch) noexcept {
            for (std::uint32_t index = 0; index < 8; ++index) {
                static_cast<void>(take(epoch, count + std::size_t{index} * 512));
            }
        });
        if (turn == period - 1) {
            retained_after_one_period = scratch.arena.retained_bytes();
        }
    }
    CRUCIBLE_FATAL_INVARIANT(retained_after_one_period > 0);
    CRUCIBLE_FATAL_INVARIANT(scratch.arena.retained_bytes() == retained_after_one_period);
    CRUCIBLE_FATAL_INVARIANT(scratch.arena.retained_bytes()
                             <= EpochArena<Scratch::Arena>::default_retained_bytes_limit);
}

// A block size above zero stays above zero after each plus, and a sum past
// the largest size_t saturates there.  A block size of zero does not compile
// (neg_core_arena_block_size_zero).
void test_block_size_stays_above_zero() {
    constexpr ::foundation::core::ArenaBlockSize floor{Bytes{16}};
    static_assert(floor.bytes() == Bytes{16});
    static_assert(floor.plus(Bytes{0}).bytes() == Bytes{16});
    static_assert(floor.plus(Bytes{48}).bytes() == Bytes{64});
    static_assert(floor.plus(Bytes{~std::size_t{0}}).bytes() == Bytes{~std::size_t{0}});
    // A count that the compiler cannot fold: below(1) is zero at run time.
    ::crucible::test::PhiloxStream stream{0x5EEDA7E4A0000002u};
    Bytes const counted{std::size_t{240} + stream.below(1)};
    ::foundation::core::ArenaBlockSize const block = floor.plus(counted);
    CRUCIBLE_FATAL_INVARIANT(block.bytes() == Bytes{256});
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::with_bump_arena(alloc, block, [alloc](auto& arena) noexcept {
        auto const pair = arena.template make<Pair>(alloc, Pair{.key = 7});
        CRUCIBLE_FATAL_INVARIANT(pair->key == 7);
        CRUCIBLE_FATAL_INVARIANT(arena.retained_bytes() == 256);
    });
}

// Each wrong use that a type cannot refuse ends the process: an array past
// the largest View.  The wrong use runs in a child through expect_dies of the
// Check family.
void test_wrong_uses_end_the_process(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
            ::foundation::core::with_bump_arena(alloc, Bytes{4096}, [alloc](auto& arena) noexcept {
                static_cast<void>(arena.template array<std::uint64_t>(alloc, ~std::size_t{0} / 4, 0));
            });
        },
        ::foundation::core::TextView{
            "foundation: fatal: an array of 4611686018427387903 elements of 8 bytes in an arena is longer than "
            "the largest View"});
}

// The property: arrays of random counts and alignments in random epochs sit
// at their alignment and never overlap.  Each array holds its own fill after
// each later array of the epoch, so a later array wrote none of its bytes.
// The fills of the epoch are the model.
void test_agrees_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDA7E4A0000001u};
    Scratch scratch{Bytes{512}};
    for (int trial = 0; trial < 64; ++trial) {
        scratch.arena.with_epoch([&stream](ScratchEpoch& epoch) noexcept {
            ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
            ::foundation::core::FixedArray<View<Wide const>, 32> arrays{};
            for (auto const position : arrays.indices()) {
                std::size_t const count = 1 + stream.below(40);
                std::uint64_t const fill = position.raw() + 1;
                if (stream.below(2) == 0) {
                    auto const odd = epoch.array<std::uint32_t>(alloc, 1, 0);
                    CRUCIBLE_FATAL_INVARIANT(first_address(odd.view()).is_aligned<alignof(std::uint32_t)>());
                }
                auto const lines_run = epoch.array(alloc, count, Wide{.first = fill});
                View<Wide const> const lines = lines_run.view();
                arrays.with_element(position, [lines](View<Wide const>& slot) noexcept { slot = lines; });
                CRUCIBLE_FATAL_INVARIANT(first_address(arrays.at(position)).is_aligned<64>());
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
    test_window_keeps_the_run();
    test_empty_array_takes_no_bytes();
    test_build_array_builds_each_element();
    test_large_request_takes_its_own_block();
    test_family_arena_lives_in_a_box();
    test_position_names_its_object_in_its_arena();
    test_end_keeps_the_blocks();
    test_end_above_the_limit_keeps_one_block();
    test_set_limit_refuses_the_blocks_above_it();
    test_long_run_keeps_its_blocks();
    test_block_size_stays_above_zero();
    test_wrong_uses_end_the_process(test);
    test_agrees_with_plain_model();
    return 0;
}
