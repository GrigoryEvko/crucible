// What fixy/handle/PublishCommit.h claims, checked.
//
// The cell is a counter that only the publishing stage bumps.  The
// friend list is the gate, so a type that is not WriteAuth cannot bump
// it (neg_handle_publish_commit_bump_outside_auth).  The load that a
// reader spins on is acquire, and the bump is acq_rel, so a reader that
// sees a count sees every write the publisher made before that bump.
// That half needs two threads to show.

#include <fixy/handle/PublishCommit.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>

namespace h = fixy::handle;

namespace {

struct CommitTag {};

// The one writer.  It publishes a side effect, then bumps the cell.
class Publisher {
public:
    using Cell = h::PublishCommitCell<CommitTag, Publisher>;

    static void publish(Cell& cell, std::uint64_t* slot, std::uint64_t value) noexcept {
        *slot = value;
        (void)cell.bump();
    }

    static std::uint64_t publish_many(Cell& cell, std::uint64_t count) noexcept { return cell.bump_by(count); }
};

using Cell = Publisher::Cell;

static_assert(std::is_same_v<Cell::tag_type, CommitTag>);
static_assert(std::is_same_v<Cell::write_auth_type, Publisher>);
static_assert(alignof(Cell) == 64, "the counter must sit on a cache line of its own");
static_assert(!std::is_copy_constructible_v<Cell> && !std::is_move_constructible_v<Cell>);
static_assert(std::is_trivially_destructible_v<Cell>);

[[nodiscard]] bool a_fresh_cell_reads_zero() noexcept {
    Cell cell;
    return cell.load_acquire() == 0 && cell.peek_relaxed() == 0 && cell.get() == 0
           && cell.load(std::memory_order_relaxed) == 0;
}

[[nodiscard]] bool the_writer_counts() noexcept {
    Cell cell;
    std::uint64_t slot = 0;
    Publisher::publish(cell, &slot, 7);
    const std::uint64_t before = Publisher::publish_many(cell, 3);
    return before == 1 && cell.get() == 4 && slot == 7;
}

// The reader waits for a count and then reads the side effect with no
// fence of its own.  The acquire load of the count is what makes the
// plain read of the slot see the value.
[[nodiscard]] bool a_reader_that_sees_the_count_sees_the_write() noexcept {
    Cell cell;
    std::uint64_t slot = 0;
    std::uint64_t seen = 0;
    std::thread reader([&] {
        while (cell.load_acquire() == 0) {
        }
        seen = slot;
    });
    Publisher::publish(cell, &slot, 42);
    reader.join();
    return seen == 42;
}

}  // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool holds, const char* claim) {
        if (!holds) {
            std::fprintf(stderr, "FAIL: %s\n", claim);
            ++failures;
        }
    };
    check(a_fresh_cell_reads_zero(), "a fresh cell reads zero through every load");
    check(the_writer_counts(), "the writer's bumps add up, and bump_by returns the count before it");
    check(a_reader_that_sees_the_count_sees_the_write(),
          "a reader that sees the published count also sees the write made before the bump");
    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_handle_publish_commit: only the writer bumps, and a count publishes its writes\n");
    return EXIT_SUCCESS;
}
