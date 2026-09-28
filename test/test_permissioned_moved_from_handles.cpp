// A moved-from handle of every permissioned channel family fails closed.
//
// A handle holds its channel through a binding that the move clears.  The
// move hands the Permission or the pool share to the new handle, so the old
// one has nothing left to act on.  Each case below moves a handle, then uses
// the source, and the process must end.  A child that survives the use exits
// normally, and the case then fails, because the moved-from handle pushed,
// popped, stole, published or read through a token it no longer
// holds.
//
// The positive half checks that the moved-into handle still works, so that a
// binding which broke every handle would not pass as a fix.

#include <crucible/concurrent/_PermissionedCalendarGrid.h>
#include <crucible/concurrent/_PermissionedChaseLevDeque.h>
#include <crucible/concurrent/_PermissionedMpmcChannel.h>
#include <crucible/concurrent/_PermissionedShardedCalendarGrid.h>
#include <crucible/concurrent/_PermissionedShardedGrid.h>
#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/concurrent/_PermissionedSpscChannel.h>
#include <crucible/concurrent/_Queue.h>
#include <crucible/concurrent/_PermissionedMpscChannel.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/_PermissionGridGenerator.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cc = crucible::concurrent;
namespace cs = crucible::safety;

namespace {

struct SpscTag {};
struct MpscTag {};
struct MpmcTag {};
struct SnapshotTag {};
struct GridTag {};
struct CalendarTag {};
struct ShardedCalendarTag {};
struct DequeTag {};
struct QueueSpscTag {};
struct QueueMpscTag {};

struct IdentityKey {
    static std::uint64_t key(std::uint64_t v) noexcept { return v; }
};

struct Sample {
    std::uint64_t value = 0;
};

using Spsc = cc::PermissionedSpscChannel<int, 8, SpscTag>;
using Mpsc = cc::PermissionedMpscChannel<int, 8, MpscTag>;
using Mpmc = cc::PermissionedMpmcChannel<int, 8, MpmcTag>;
using Snapshot = cc::PermissionedSnapshot<Sample, SnapshotTag>;
using Grid = cc::PermissionedShardedGrid<int, 2, 2, 8, GridTag>;
using Calendar = cc::PermissionedCalendarGrid<std::uint64_t, 2, 8, 4, IdentityKey, 1, CalendarTag>;
using ShardedCalendar = cc::PermissionedShardedCalendarGrid<std::uint64_t, 2, 8, 4, IdentityKey, 1, ShardedCalendarTag>;
using Deque = cc::PermissionedChaseLevDeque<int, 16, DequeTag>;
using SpscQueue = cc::Queue<int, cc::kind::spsc<8>>;
using MpscQueue = cc::Queue<int, cc::kind::mpsc<8>>;

// The binding costs what the reference or the pointer it replaced cost.
static_assert(sizeof(Spsc::ProducerHandle) == sizeof(void*));
static_assert(sizeof(Spsc::ConsumerHandle) == sizeof(void*));
static_assert(sizeof(Grid::ProducerHandle<0>) == sizeof(void*));
static_assert(sizeof(Deque::OwnerHandle) == sizeof(void*));
static_assert(sizeof(Snapshot::WriterHandle) == sizeof(void*));

[[nodiscard]] auto spsc_handles(Spsc& channel) {
    auto whole = cs::mint_permission_root<Spsc::whole_tag>();
    auto [producer, consumer] =
        cs::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(whole));
    return std::pair{channel.producer(std::move(producer)), channel.consumer(std::move(consumer))};
}

[[nodiscard]] auto grid_handles(Grid& grid) {
    auto whole = cs::mint_permission_root<Grid::whole_tag>();
    auto perms = cs::mint_grid_permissions<Grid::whole_tag, 2, 2>(std::move(whole));
    return std::pair{grid.template producer<0>(std::move(std::get<0>(perms.producers))),
                     grid.template consumer<0>(std::move(std::get<0>(perms.consumers)))};
}

[[nodiscard]] auto calendar_handles(Calendar& grid) {
    auto whole = cs::mint_permission_root<Calendar::whole_tag>();
    auto perms = cs::mint_grid_permissions<Calendar::whole_tag, 2, 1>(std::move(whole));
    return std::pair{grid.template producer<0>(std::move(std::get<0>(perms.producers))),
                     grid.consumer(std::move(std::get<0>(perms.consumers)))};
}

[[nodiscard]] auto sharded_calendar_handles(ShardedCalendar& grid) {
    auto whole = cs::mint_permission_root<ShardedCalendar::whole_tag>();
    auto perms = cs::mint_grid_permissions<ShardedCalendar::whole_tag, 2, 2>(std::move(whole));
    return std::pair{grid.template producer<0>(std::move(std::get<0>(perms.producers))),
                     grid.template consumer<0>(std::move(std::get<0>(perms.consumers)))};
}

template <typename Queue, typename UserTag>
[[nodiscard]] auto queue_handles(Queue& queue) {
    auto whole = cs::mint_permission_root<cc::queue_tag::Whole<UserTag>>();
    auto [producer, consumer] =
        cs::mint_permission_split<cc::queue_tag::Producer<UserTag>, cc::queue_tag::Consumer<UserTag>>(
            std::move(whole));
    return std::pair{queue.producer_handle(std::move(producer)), queue.consumer_handle(std::move(consumer))};
}

// ── The attacks: each uses a handle after moving it ──────────────────
//
// Each attack reaches the moved-from handle through a call that the
// optimizer cannot see into.  Inlined, the empty binding is visible at
// compile time, and -Wstringop-overflow refuses the null access that the
// attack makes on purpose.

template <typename Handle>
[[gnu::noipa]] Handle& opaque_ref(Handle& handle) {
    return handle;
}

void spsc_producer() {
    Spsc channel{};
    auto [producer, consumer] = spsc_handles(channel);
    [[maybe_unused]] auto moved = std::move(producer);
    (void)opaque_ref(producer).try_push(1);
}

void spsc_consumer() {
    Spsc channel{};
    auto [producer, consumer] = spsc_handles(channel);
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_pop();
}

void mpsc_producer() {
    Mpsc channel{};
    auto producer = channel.producer();
    [[maybe_unused]] auto moved = std::move(*producer);
    (void)opaque_ref(*producer).try_push(1);
}

// The moved-from producer holds no pool share, so the drained window runs.
// A push from it inside that window is the one the window forbids.
void mpsc_producer_in_drained_window() {
    Mpsc channel{};
    auto producer = channel.producer();
    { auto moved = std::move(*producer); }
    (void)channel.with_drained_access([&producer] { (void)opaque_ref(*producer).try_push(1); });
}

void mpsc_consumer() {
    Mpsc channel{};
    auto consumer = channel.consumer(cs::mint_permission_root<Mpsc::consumer_tag>());
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_pop();
}

void mpmc_producer() {
    Mpmc channel{};
    auto producer = channel.producer();
    [[maybe_unused]] auto moved = std::move(*producer);
    (void)opaque_ref(*producer).try_push(1);
}

void mpmc_consumer() {
    Mpmc channel{};
    auto consumer = channel.consumer();
    [[maybe_unused]] auto moved = std::move(*consumer);
    (void)opaque_ref(*consumer).try_pop();
}

void mpmc_producer_after_close() {
    Mpmc channel{};
    auto producer = channel.producer();
    auto closed = std::move(*producer).close();
    (void)opaque_ref(*producer).try_push(1);
}

void mpmc_consumer_after_close() {
    Mpmc channel{};
    auto consumer = channel.consumer();
    auto closed = std::move(*consumer).close();
    (void)opaque_ref(*consumer).try_pop();
}

void snapshot_writer() {
    Snapshot snapshot{};
    auto writer = snapshot.writer(cs::mint_permission_root<cc::snapshot_tag::Writer<SnapshotTag>>());
    [[maybe_unused]] auto moved = std::move(writer);
    opaque_ref(writer).publish(Sample{1});
}

void snapshot_writer_after_release() {
    Snapshot snapshot{};
    auto writer = snapshot.writer(cs::mint_permission_root<cc::snapshot_tag::Writer<SnapshotTag>>());
    auto permission = std::move(writer).release_permission();
    (void)permission;
    opaque_ref(writer).publish(Sample{1});
}

void snapshot_reader() {
    Snapshot snapshot{};
    auto reader = snapshot.reader();
    [[maybe_unused]] auto moved = std::move(*reader);
    (void)opaque_ref(*reader).load();
}

void grid_producer() {
    Grid grid{};
    auto [producer, consumer] = grid_handles(grid);
    [[maybe_unused]] auto moved = std::move(producer);
    (void)opaque_ref(producer).try_push(1);
}

void grid_consumer() {
    Grid grid{};
    auto [producer, consumer] = grid_handles(grid);
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_pop();
}

void calendar_producer() {
    Calendar grid{};
    auto [producer, consumer] = calendar_handles(grid);
    [[maybe_unused]] auto moved = std::move(producer);
    (void)opaque_ref(producer).try_push(1);
}

void calendar_consumer() {
    Calendar grid{};
    auto [producer, consumer] = calendar_handles(grid);
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_pop();
}

void sharded_calendar_producer() {
    ShardedCalendar grid{};
    auto [producer, consumer] = sharded_calendar_handles(grid);
    [[maybe_unused]] auto moved = std::move(producer);
    (void)opaque_ref(producer).try_push(1);
}

void sharded_calendar_consumer() {
    ShardedCalendar grid{};
    auto [producer, consumer] = sharded_calendar_handles(grid);
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_pop();
}

void deque_owner() {
    Deque deque{};
    auto owner = deque.owner(cs::mint_permission_root<Deque::owner_tag>());
    [[maybe_unused]] auto moved = std::move(owner);
    (void)opaque_ref(owner).try_push(1);
}

void deque_thief() {
    Deque deque{};
    auto thief = deque.thief();
    [[maybe_unused]] auto moved = std::move(*thief);
    (void)opaque_ref(*thief).try_steal();
}

void queue_spsc_producer() {
    SpscQueue queue{};
    auto [producer, consumer] = queue_handles<SpscQueue, QueueSpscTag>(queue);
    [[maybe_unused]] auto moved = std::move(producer);
    (void)opaque_ref(producer).try_push(1);
}

void queue_mpsc_consumer() {
    MpscQueue queue{};
    auto [producer, consumer] = queue_handles<MpscQueue, QueueMpscTag>(queue);
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_pop();
}

struct Attack {
    const char* name;
    void (*run)();
};

constexpr Attack kAttacks[] = {
    {"spsc producer", &spsc_producer},
    {"spsc consumer", &spsc_consumer},
    {"mpsc producer", &mpsc_producer},
    {"mpsc producer in the drained window", &mpsc_producer_in_drained_window},
    {"mpsc consumer", &mpsc_consumer},
    {"mpmc producer", &mpmc_producer},
    {"mpmc consumer", &mpmc_consumer},
    {"mpmc producer after close", &mpmc_producer_after_close},
    {"mpmc consumer after close", &mpmc_consumer_after_close},
    {"snapshot writer", &snapshot_writer},
    {"snapshot writer after release", &snapshot_writer_after_release},
    {"snapshot reader", &snapshot_reader},
    {"sharded grid producer", &grid_producer},
    {"sharded grid consumer", &grid_consumer},
    {"calendar grid producer", &calendar_producer},
    {"calendar grid consumer", &calendar_consumer},
    {"sharded calendar grid producer", &sharded_calendar_producer},
    {"sharded calendar grid consumer", &sharded_calendar_consumer},
    {"chase-lev owner", &deque_owner},
    {"chase-lev thief", &deque_thief},
    {"spsc queue producer", &queue_spsc_producer},
    {"mpsc queue consumer", &queue_mpsc_consumer},
};

[[nodiscard]] bool ends_the_process(void (*attack)()) {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: a use the binding catches ends the process, so it
    // runs in a child that the parent observes.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, see above
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        attack();
        std::_Exit(0);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: death test, see above
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    return WIFSIGNALED(status) != 0;
}

// ── The positive half: the moved-into handle still works ─────────────

[[nodiscard]] int moved_into_handles_work() {
    Spsc channel{};
    auto [producer, consumer] = spsc_handles(channel);
    auto producer_moved = std::move(producer);
    auto consumer_moved = std::move(consumer);
    if (!producer_moved.try_push(7)) return 1;
    const auto item = consumer_moved.try_pop();
    if (!item || *item != 7) return 1;

    Mpmc mpmc{};
    auto mpmc_producer_handle = mpmc.producer();
    auto mpmc_consumer_handle = mpmc.consumer();
    auto mpmc_producer_moved = std::move(*mpmc_producer_handle);
    if (!mpmc_producer_moved.try_push(9)) return 1;
    const auto mpmc_item = mpmc_consumer_handle->try_pop();
    if (!mpmc_item || *mpmc_item != 9) return 1;
    auto closed = std::move(mpmc_producer_moved).close();
    (void)closed;
    if (mpmc.outstanding_producers() != 1) return 1;
    return 0;
}

}  // namespace

int main() {
    std::fprintf(stderr, "[expected] each case below prints the contract report of a child process\n");
    int failures = 0;
    for (const Attack& attack : kAttacks) {
        if (!ends_the_process(attack.run)) {
            std::fprintf(stderr, "FAIL: a moved-from %s still acted on its channel\n", attack.name);
            ++failures;
        }
    }
    if (moved_into_handles_work() != 0) {
        std::fprintf(stderr, "FAIL: a moved-into handle lost its channel\n");
        ++failures;
    }
    if (failures != 0) return 1;
    std::printf("test_permissioned_moved_from_handles: %zu attacks refused\n", std::size(kAttacks));
    return 0;
}
