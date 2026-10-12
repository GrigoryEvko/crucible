// The epoch of an arena, the one right to end the bytes of the arena.
//
// An epoch covers each byte that the arena gives out until its end.  The end
// keeps the blocks, so a loop that ends an epoch in each turn allocates no
// block in its steady state.  An arena that holds more bytes than its limit
// at the end frees each block but the first.  A wrong use of an epoch ends
// the process: a second epoch, an epoch of an arena that holds bytes, and a
// token that ends with no end.

#include <crucible/Arena.h>
#include <fixy/Borrowed.h>
#include <fixy/Refined.h>
#include <foundation/effects/Effect.h>
#include "test_abort_probe.h"
#include "test_assert.h"

#include <cstddef>
#include <cstdint>

namespace {

using crucible::Arena;
using crucible::ArenaEpoch;

// One allocation of count bytes, as a reference that a test can compare.
[[nodiscard]] fixy::WeakRef<std::byte> take(Arena& arena, foundation::effects::Alloc alloc, size_t count) {
    return fixy::WeakRef<std::byte>::from_raw(static_cast<std::byte*>(arena.alloc(
        alloc, fixy::mint_refined<fixy::positive>(count), fixy::mint_refined<fixy::power_of_two>(size_t{8}))));
}

// The second epoch gets the addresses of the first epoch, and the arena
// allocates no block for it.
void test_end_keeps_the_blocks() {
    auto test = foundation::effects::testing::test();
    Arena arena{4096};

    ArenaEpoch first = arena.open_epoch();
    const fixy::WeakRef<std::byte> first_small = take(arena, test.alloc, 1000);
    const fixy::WeakRef<std::byte> first_large = take(arena, test.alloc, 20000);
    const fixy::WeakRef<std::byte> first_after = take(arena, test.alloc, 1000);
    const size_t blocks = arena.block_count();
    const size_t retained = arena.retained_bytes();
    assert(blocks == 3);
    std::move(first).end();
    assert(!arena.has_open_epoch());
    assert(arena.total_allocated() == 0);
    assert(arena.block_count() == blocks);
    assert(arena.retained_bytes() == retained);

    ArenaEpoch second = arena.open_epoch();
    assert(take(arena, test.alloc, 1000) == first_small);
    assert(take(arena, test.alloc, 20000) == first_large);
    assert(take(arena, test.alloc, 1000) == first_after);
    assert(arena.block_count() == blocks);
    assert(arena.retained_bytes() == retained);
    std::move(second).end();

    crucible::test::pass("  end_keeps_the_blocks:                 PASSED (blocks={}, bytes={})\n", blocks, retained);
}

// An end above the limit frees each block but the first, and the arena then
// serves the next epoch.
void test_end_above_the_limit_keeps_one_block() {
    auto test = foundation::effects::testing::test();
    constexpr size_t BLOCK = size_t{1} << 20;
    Arena arena{BLOCK};

    ArenaEpoch epoch = arena.open_epoch();
    (void)take(arena, test.alloc, Arena::RETAINED_BYTES_LIMIT);
    assert(arena.block_count() == 2);
    assert(arena.retained_bytes() > Arena::RETAINED_BYTES_LIMIT);
    std::move(epoch).end();
    assert(arena.block_count() == 1);
    assert(arena.retained_bytes() == BLOCK);
    assert(arena.total_allocated() == 0);

    ArenaEpoch next = arena.open_epoch();
    (void)take(arena, test.alloc, 64);
    assert(arena.total_allocated() == 64);
    std::move(next).end();

    crucible::test::pass("  end_above_the_limit_keeps_one_block:  PASSED\n");
}

// A long run of epochs with a mix of sizes reaches its blocks in its first
// period, and no later epoch adds a block.  The sizes repeat with a period
// of 3000 turns, because 7919 and 3000 have no common factor.
void test_long_run_keeps_its_blocks() {
    auto test = foundation::effects::testing::test();
    constexpr uint32_t PERIOD = 3000;
    Arena arena{4096};

    size_t retained_after_one_period = 0;
    for (uint32_t turn = 0; turn < 2 * PERIOD; ++turn) {
        ArenaEpoch epoch = arena.open_epoch();
        const size_t count = 1 + (size_t{turn} * 7919) % PERIOD;
        for (uint32_t index = 0; index < 8; ++index) {
            (void)take(arena, test.alloc, count + size_t{index} * 512);
        }
        std::move(epoch).end();
        if (turn == PERIOD - 1) retained_after_one_period = arena.retained_bytes();
    }
    assert(retained_after_one_period > 0);
    assert(arena.retained_bytes() == retained_after_one_period);
    assert(arena.retained_bytes() <= Arena::RETAINED_BYTES_LIMIT);

    crucible::test::pass("  long_run_keeps_its_blocks:            PASSED (blocks={}, bytes={})\n", arena.block_count(),
                         arena.retained_bytes());
}

// Each wrong use of an epoch ends the process.
void test_wrong_uses_end_the_process() {
    auto test = foundation::effects::testing::test();

    Arena twice{4096};
    ArenaEpoch first = twice.open_epoch();
    const bool second_epoch_aborts = crucible::test::aborts([&] {
        ArenaEpoch second = twice.open_epoch();
        std::move(second).end();
    });
    assert(second_epoch_aborts);
    std::move(first).end();

    Arena holds_bytes{4096};
    (void)take(holds_bytes, test.alloc, 64);
    const bool epoch_of_used_arena_aborts = crucible::test::aborts([&] {
        ArenaEpoch epoch = holds_bytes.open_epoch();
        std::move(epoch).end();
    });
    assert(epoch_of_used_arena_aborts);

    // The token ends the epoch before the report, so the arena is whole.
    Arena dropped{4096};
    const bool dropped_token_aborts = crucible::test::aborts([&] {
        ArenaEpoch epoch = dropped.open_epoch();
        (void)take(dropped, test.alloc, 64);
    });
    assert(dropped_token_aborts);
    assert(!dropped.has_open_epoch());
    assert(dropped.total_allocated() == 0);

    crucible::test::pass("  wrong_uses_end_the_process:           PASSED\n");
}

}  // namespace

int main() {
    test_end_keeps_the_blocks();
    test_end_above_the_limit_keeps_one_block();
    test_long_run_keeps_its_blocks();
    test_wrong_uses_end_the_process();
    crucible::test::pass("test_arena_epoch: all tests passed\n");
    return 0;
}
