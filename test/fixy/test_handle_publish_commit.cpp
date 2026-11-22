// What fixy/handle/PublishCommit.h claims, checked.
//
// The cell is a counter that only the publishing stage bumps.  Each write
// takes the token of that stage, so code with no token cannot bump it
// (neg_handle_publish_commit_bump_outside_auth).  The load that a reader
// spins on is acquire, and the bump is acq_rel, so a reader that sees a
// count sees every write the publisher made before that bump.  That half
// needs two threads to show.

#include <fixy/handle/PublishCommit.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>

namespace h = fixy::handle;

namespace {

struct CommitTag {};

class PublishingStage;

// The token of the one writer.  Its constructor is private, and the stage
// mints it, so no other thread holds one (THE BUILD OF A TOKEN of
// foundation/Transfer.h).
class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] Publisher final {
    Publisher() noexcept {}
    friend class PublishingStage;

public:
    Publisher(Publisher const&) = delete("a publisher token names the one writer of the cell");
    Publisher& operator=(Publisher const&) = delete("a publisher token names the one writer of the cell");
    ~Publisher() = default;
};

using Cell = h::PublishCommitCell<CommitTag, Publisher>;

// The stage that publishes.  One atomic exchange gives its token to one
// thread for the life of the stage, and a second claim ends the process.
class PublishingStage {
public:
    [[nodiscard]] Publisher claim_publisher() noexcept {
        if (is_claimed_.exchange_acq_rel(true)) ::foundation::core::fatal("a second claim of the publisher token");
        return Publisher{};
    }

private:
    ::foundation::core::Atomic<bool> is_claimed_{false};
};

// The writes of the one writer: it publishes a side effect, then bumps the
// cell.
void publish(Cell& cell, Publisher const& auth, std::uint64_t* slot, std::uint64_t value) noexcept {
    *slot = value;
    (void)cell.bump(auth);
}

static_assert(std::is_same_v<Cell::tag_type, CommitTag>);
static_assert(std::is_same_v<Cell::write_auth_type, Publisher>);
static_assert(alignof(Cell) == 64, "the counter must sit on a cache line of its own");
static_assert(!std::is_copy_constructible_v<Cell> && !std::is_move_constructible_v<Cell>);
static_assert(std::is_trivially_destructible_v<Cell>);
// The token is a proof of the cell: the door check of synchronized{} reads
// each write as proven.
static_assert(::foundation::transfer::Synchronized<Cell>);

// The two reads are named for their order.  No read takes the order as an
// argument, because a release order is undefined for a load.
template <typename C>
concept ReadsWithAnOrderArgument = requires(C const& cell) { cell.load(std::memory_order_release); };
static_assert(!ReadsWithAnOrderArgument<Cell>);

[[nodiscard]] bool a_fresh_cell_reads_zero() noexcept {
    Cell cell;
    PublishingStage stage{};
    Publisher const auth = stage.claim_publisher();
    return cell.load_acquire() == 0 && cell.peek_relaxed(auth) == 0;
}

[[nodiscard]] bool the_writer_counts() noexcept {
    Cell cell;
    PublishingStage stage{};
    Publisher const auth = stage.claim_publisher();
    std::uint64_t slot = 0;
    publish(cell, auth, &slot, 7);
    const std::uint64_t before = cell.bump_by(auth, 3);
    return before == 1 && cell.load_acquire() == 4 && slot == 7;
}

// The reader waits for a count and then reads the side effect with no
// fence of its own.  The acquire load of the count is what makes the
// plain read of the slot see the value.
[[nodiscard]] bool a_reader_that_sees_the_count_sees_the_write() noexcept {
    Cell cell;
    std::uint64_t slot = 0;
    std::uint64_t seen = 0;
    std::thread reader([&] {
        while (cell.load_acquire() == 0) {}
        seen = slot;
    });
    PublishingStage stage{};
    Publisher const auth = stage.claim_publisher();
    publish(cell, auth, &slot, 42);
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
    check(a_fresh_cell_reads_zero(), "a fresh cell reads zero through both reads");
    check(the_writer_counts(), "the writer's bumps add up, and bump_by returns the count before it");
    check(a_reader_that_sees_the_count_sees_the_write(),
          "a reader that sees the published count also sees the write made before the bump");
    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_handle_publish_commit: only the writer bumps, and a count publishes its writes\n");
    return EXIT_SUCCESS;
}
