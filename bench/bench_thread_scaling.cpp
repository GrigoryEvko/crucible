// The scaling rows of the primitives of the base, from one thread to many.
//
// THE OPS
//   shared add         fetch_add_acq_rel of foundation/core/Atomic.h on one
//                      cell that each thread of the row writes
//   own add            the same add on a cell of each thread, each cell on a
//                      cache line of its own: the control with no shared line
//   mpsc push          try_push of fixy/concurrent/MpscRing.h from each thread
//                      of the row but the last, and try_pop on the last
//                      thread, the one consumer
//   mpsc packed push   the same calls on the ring with packed cells
//                      (CellSpacing::packed), whose cache line holds the cells
//                      of some adjacent tickets
//   mpsc batch push    try_push_batch of kBatch items from each producer, and
//                      try_pop_batch of up to kPopBatch items on the consumer
//   mpsc reference     try_push and try_pop of the pinned MPMCQueue of Erik
//                      Rigtorp (utils/pins.txt), a queue with a turn word in
//                      each slot in the form of Dmitry Vyukov
//   spsc push          try_push of fixy/concurrent/SpscRing.h on the main
//                      thread, and try_pop on one consumer
//   spsc proven push   try_push_proven and try_pop_proven of a ring that the
//                      bench owns, the calls of the handles of a wire channel
//   spsc batch push    try_push_batch of kBatch items on the main thread, and
//                      try_pop_batch of up to kPopBatch items on the consumer
//   spsc reference     try_push and try_pop of the pinned SPSCQueue of Erik
//                      Rigtorp (utils/pins.txt)
//   A push into a full ring waits for the consumer.  A pop of an empty ring
//   waits for a producer.  Each wait spins with the spin hint.  A batch op
//   counts each item of its batch.
//
// THE PLACES OF THE CONSUMER
//   The consumer of an mpsc row is the thread of the last slot of the row.
//   Each spsc op has two rows of two threads: one with the consumer on the
//   CPU of slot 1, in the L3 group of the producer when the claim holds a
//   second core of that group, and one with the consumer on the first CPU of
//   the spread outside the L3 group of the producer.  A spread with no CPU
//   outside that group gives no row of the second place.
//
// THE THREADS OF A ROW
//   A row of N threads runs the op on N threads at the same time, one thread
//   on the CPU of each slot 0 to N - 1 of the spread of the run (THE SPREAD
//   OF A RUN of bench/bench_harness.h).  Slot 0 is the main thread, a
//   producer.  Each other slot is a worker on a std::jthread, which pins
//   itself to the CPU of its slot (THE RAW WORKERS).
//
// THE RAW WORKERS
//   The rows measure the bare rings of fixy/concurrent/ and their pinned
//   references.  The threads of a row share the rings of RowState, and a
//   ring keeps plain positions that one side writes and the other reads.  No
//   type of the base states that, so no door of the base can check the
//   workers.  The rows thus keep the raw rings and raw threads as a
//   measurement, in a MEASURE region.
//
// THE PHASES OF A ROW
//   1. The window: each thread runs the op and counts it for kWindowNs.  The
//      row gives the items of the producers of all threads in one second, and
//      the efficiency: that throughput over the throughput of the first row
//      of the op, times the ratio of the producers.  For an mpsc op, the base
//      is the row of one producer and the consumer.  The pause check of the
//      harness reads the threads over the window.
//      The window is bench::kWindows consecutive windows of equal time, one
//      phase each (THE WINDOWS of bench/bench_harness.h).  Each thread counts
//      its items in each window, and the row gives the median and the spread
//      of the throughput of the windows.  Each window has the pause check of
//      the time that the main thread lost in it.
//   2. The latency: the workers go on, and the main thread runs the op under
//      Run::measure, one op in each sample.  Its report gives p50, p99 and
//      the maximum of one op, with the wait for the consumer in it.  The
//      harness takes the cost of the clock off each sample, so an op of a
//      few nanoseconds reads at the resolution of the clock.  The JSON tail of
//      the bench gives the latency report of each row, with the count of its
//      threads and the place of its consumer in its name.
//   In each row with a consumer, the consumer keeps up with the producers, so
//   the ring is nearly empty: each pop of the consumer finds the newest items.
//   The consumer adds each item that it pops into a sum, and it stores the sum
//   at its end, so it reads each cell, as a consumer that uses its items does.
//
// THE COUNTS OF THREADS
//   With no argument, the rows have 1, 2, 4, 8 and 16 threads, up to the
//   count of the slots.  The bench states nine cores, so loop bench gives it
//   the eight cores of one L3 group and one core of a second group.  The rows
//   of 1 to 8 threads then keep one group, and the ninth core holds the
//   consumer of the spsc rows in another group.  Each argument is one more
//   count of threads after the rows of 1 and 2 threads, which are the bases of
//   the efficiency.  A count above the slots or above kMaxThreads ends the run.
//
// THE GROUPS OF OPS
//   An argument add, mpsc or spsc selects the ops of that group: the two
//   adds, the mpsc ops, or the spsc ops.  With no such argument,
//   the run gives each group.  So two builds of one ring can run in turns:
//       python3 utils/bin/loop bench --cores 9 --command -- BINARY spsc
//
// THE COMMANDS
//   1 to 8 threads, one L3 group:   python3 utils/bin/loop bench bench_thread_scaling
//   16 threads, two L3 groups:      python3 utils/bin/loop bench --cores 16 bench_thread_scaling
//   32 threads, the shared CPUs:    python3 utils/bin/loop heavy -j 32 -- build-bench/bench/bench_thread_scaling 32
//   64 to 224 threads, on a host with no other work:
//                                   build-bench/bench/bench_thread_scaling 64 128 224
//   A run with no claim puts its threads on CPUs that other work can use,
//   so each row of that run is noisy.  The pause check reads at most
//   kPauseThreads threads of the harness.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/os/Args.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

#include "bench_harness.h"

namespace {

namespace eff = ::foundation::effects;

using Item = std::uint64_t;

// The threads of one row, at most.  The command of the owner runs 224.
constexpr std::size_t kMaxThreads = 256;
// The rows of one op, at most, and the latency reports of the ten ops.
constexpr std::size_t kMaxRows = 16;
constexpr std::size_t kMaxReports = 10 * kMaxRows;
constexpr std::uint64_t kWindowNs = 500000000;
constexpr std::size_t kLatencySamples = 1000000;
constexpr std::size_t kLatencyWarmup = 10000;
constexpr std::size_t kLatencyMs = 1000;
// The ops of a worker between two reads of the phase, and of the main
// thread between two reads of the clock.
constexpr std::uint64_t kOpsPerCheck = 64;
constexpr std::uint64_t kOpsPerClock = 1024;
constexpr std::size_t kRingCapacity = 4096;
// The items of one batch push, and the items of one batch pop at most.
constexpr std::size_t kBatch = 16;
constexpr std::size_t kPopBatch = 64;

}  // namespace

// ── The pinned references ───────────────────────────────────────────────
//
// Each reference gives try_push and try_pop in the shape of the ring that it
// measures.  The wrappers are in line in this unit, so each op of a reference
// row inlines into its loop, as the op of the ring does.  A build with no
// pinned source gives a wrapper that the rows never call, and the bench omits
// the rows of that reference.

#if defined(CRUCIBLE_BENCH_RIGTORP_SPSC)
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the pinned SPSCQueue of Erik Rigtorp is the reference of the SPSC ring")

#include <rigtorp/SPSCQueue.h>

namespace {

inline constexpr bool kHasSpscReference = true;

struct SpscReference {
    rigtorp::SPSCQueue<Item> queue{kRingCapacity};

    [[nodiscard, gnu::always_inline]] bool try_push(Item const& item) noexcept { return queue.try_push(item); }

    [[nodiscard, gnu::always_inline]] ::fixy::Option<Item> try_pop() noexcept {
        Item* const front = queue.front();
        if (front == nullptr) return ::fixy::none;
        Item const item = *front;
        queue.pop();
        return ::fixy::some(item);
    }
};

}  // namespace

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
#else
namespace {

inline constexpr bool kHasSpscReference = false;

struct SpscReference {
    [[nodiscard]] bool try_push(Item const& /*item*/) noexcept { return true; }
    [[nodiscard]] ::fixy::Option<Item> try_pop() noexcept { return ::fixy::none; }
};

}  // namespace
#endif

#if defined(CRUCIBLE_BENCH_RIGTORP_MPMC)
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the pinned MPMCQueue of Erik Rigtorp is the reference of the MPSC ring")

#include <rigtorp/MPMCQueue.h>

namespace {

inline constexpr bool kHasMpscReference = true;

struct MpscReference {
    rigtorp::MPMCQueue<Item> queue{kRingCapacity};

    [[nodiscard, gnu::always_inline]] bool try_push(Item const& item) noexcept { return queue.try_push(item); }

    [[nodiscard, gnu::always_inline]] ::fixy::Option<Item> try_pop() noexcept {
        Item item = 0;
        if (!queue.try_pop(item)) return ::fixy::none;
        return ::fixy::some(item);
    }
};

}  // namespace

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
#else
namespace {

inline constexpr bool kHasMpscReference = false;

struct MpscReference {
    [[nodiscard]] bool try_push(Item const& /*item*/) noexcept { return true; }
    [[nodiscard]] ::fixy::Option<Item> try_pop() noexcept { return ::fixy::none; }
};

}  // namespace
#endif

namespace {

enum class OpKind : std::uint8_t {
    SharedAdd,
    OwnAdd,
    MpscPush,
    MpscPackedPush,
    MpscBatchPush,
    MpscReferencePush,
    SpscPush,
    SpscProvenPush,
    SpscBatchPush,
    SpscReferencePush,
};

// The owner of the ring of the proven rows (THE PROVEN FORM of
// fixy/concurrent/SpscRing.h).  A row runs one producer and one consumer of
// that ring, so the bench makes the proof of each side, as the channel of a
// wire makes it for its handles.
struct ProvenOwner final {
    using Ring = ::fixy::concurrent::SpscRing<Item, kRingCapacity, ProvenOwner>;

    [[nodiscard]] static constexpr auto producer() noexcept { return Ring::producer_proof{}; }
    [[nodiscard]] static constexpr auto consumer() noexcept { return Ring::consumer_proof{}; }
};

// True for an op with one consumer on the thread of a slot.
template <OpKind Kind>
inline constexpr bool kHasConsumer = Kind != OpKind::SharedAdd && Kind != OpKind::OwnAdd;

// True for an op of the SPSC ring or of its reference.
template <OpKind Kind>
inline constexpr bool kIsSpsc = Kind == OpKind::SpscPush || Kind == OpKind::SpscProvenPush
                             || Kind == OpKind::SpscBatchPush || Kind == OpKind::SpscReferencePush;

// The items of one op.
template <OpKind Kind>
inline constexpr std::uint64_t kItemsPerOp =
    Kind == OpKind::MpscBatchPush || Kind == OpKind::SpscBatchPush ? kBatch : 1;

// Where the consumer of a row runs (THE PLACES OF THE CONSUMER).
enum class ConsumerPlace : std::uint8_t {
    LastSlot,
    OtherL3,
};

// The phase of a row (THE PHASES OF A ROW).  The main thread writes it.  The
// window has one phase for each of its windows, so a worker reads the window
// in the one cell that it reads for the phase.
enum class Phase : std::uint32_t {
    Wait,
    Window0,
    Window1,
    Window2,
    Window3,
    Window4,
    Latency,
    Stop,
};
static_assert(static_cast<std::uint32_t>(Phase::Latency) - static_cast<std::uint32_t>(Phase::Window0)
                  == bench::kWindows,
              "the window of a row has one phase for each window of the harness");

using WindowIndex = ::fixy::Index<bench::kWindows>;

// The window of a phase, or none for a phase outside the window.
[[nodiscard]] ::fixy::Option<WindowIndex> window_of(Phase phase) noexcept {
    std::uint32_t const raw = static_cast<std::uint32_t>(phase);
    std::uint32_t const first = static_cast<std::uint32_t>(Phase::Window0);
    if (raw < first) return ::fixy::none;
    return WindowIndex::check(raw - first);
}

// The phase of a window.
[[nodiscard]] Phase phase_of(WindowIndex window) noexcept {
    return static_cast<Phase>(static_cast<std::uint32_t>(Phase::Window0) + window.raw());
}

// The role of the thread of a slot in a row.
enum class Role : std::uint8_t {
    Producer,
    Consumer,
};

struct ThreadCountTag final {};
using ThreadCount = ::fixy::Id<ThreadCountTag, std::uint32_t>;

// The line that one thread writes: its own cell, its items in each window,
// and whether it published them.
struct alignas(64) ThreadLine {
    ::fixy::Atomic<std::uint64_t> cell{};
    ::fixy::FixedArray<::fixy::Atomic<std::uint64_t>, bench::kWindows> window_ops{};
    ::fixy::Atomic<bool> is_published{false};
};

struct alignas(64) PhaseLine {
    ::fixy::Atomic<Phase> phase{Phase::Wait};
};

struct alignas(64) CountLine {
    ::fixy::Atomic<std::uint64_t> value{};
};

// The state that the threads of one row share.  The main thread owns it, and
// it outlives each worker of the row.  Each row builds each ring and each
// reference, and it uses the one of its op.
struct RowState {
    PhaseLine phase{};
    // The workers that wait for the window.
    CountLine ready{};
    CountLine shared{};
    ::fixy::FixedArray<ThreadLine, kMaxThreads> lines{};
    ::fixy::concurrent::MpscRing<Item, kRingCapacity> mpsc{};
    ::fixy::concurrent::MpscRing<Item, kRingCapacity, ::fixy::concurrent::CellSpacing::packed> mpsc_packed{};
    ::fixy::concurrent::SpscRing<Item, kRingCapacity> spsc{};
    ProvenOwner::Ring spsc_proven{};
    MpscReference mpsc_reference{};
    SpscReference spsc_reference{};
    // The items of each batch push.  No thread writes them after the start.
    ::fixy::FixedArray<Item, kBatch> batch{};
};

[[nodiscard]] Phase read_phase(RowState const& state) noexcept { return state.phase.phase.load_acquire(); }

[[nodiscard]] ThreadLine& line_of(RowState& state, bench::ThreadSlot slot) noexcept {
    return *state.lines.get(slot.raw()).expect("a slot of a row is below kMaxThreads");
}

// One push that waits while its ring is full, and that stops when the row
// stops, so no producer waits for a consumer that ended.
template <class Push>
[[gnu::always_inline]] inline void push_waiting(RowState const& state, Push const& push) noexcept {
    while (!push()) {
        if (read_phase(state) == Phase::Stop) return;
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The batch push of the SPSC ring, which can take a part of the batch: it
// pushes the rest until the whole batch went in, or the row stops.
[[gnu::always_inline]] inline void push_spsc_batch(RowState& state) noexcept {
    std::size_t pushed = 0;
    while (pushed < kBatch) {
        std::size_t const taken = state.spsc.try_push_batch(
            state.batch.view().window(pushed, kBatch - pushed).expect("the rest of a batch lies inside it"));
        pushed += taken;
        if (taken != 0) continue;
        if (read_phase(state) == Phase::Stop) return;
        CRUCIBLE_SPIN_PAUSE;
    }
}

// One op of a producer (THE OPS).
template <OpKind Kind>
[[gnu::always_inline]] inline void run_op(RowState& state, ThreadLine& line) noexcept {
    if constexpr (Kind == OpKind::SharedAdd) {
        static_cast<void>(state.shared.value.fetch_add_acq_rel(1));
    } else if constexpr (Kind == OpKind::OwnAdd) {
        static_cast<void>(line.cell.fetch_add_acq_rel(1));
    } else if constexpr (Kind == OpKind::MpscPush) {
        push_waiting(state, [&state]() noexcept { return state.mpsc.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::MpscPackedPush) {
        push_waiting(state, [&state]() noexcept { return state.mpsc_packed.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::MpscBatchPush) {
        // The batch of the MPSC ring is all or nothing.
        push_waiting(state, [&state]() noexcept { return state.mpsc.try_push_batch(state.batch.view()) == kBatch; });
    } else if constexpr (Kind == OpKind::MpscReferencePush) {
        push_waiting(state, [&state]() noexcept { return state.mpsc_reference.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::SpscPush) {
        push_waiting(state, [&state]() noexcept { return state.spsc.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::SpscProvenPush) {
        push_waiting(
            state, [&state]() noexcept { return state.spsc_proven.try_push_proven(ProvenOwner::producer(), Item{1}); });
    } else if constexpr (Kind == OpKind::SpscBatchPush) {
        push_spsc_batch(state);
    } else {
        push_waiting(state, [&state]() noexcept { return state.spsc_reference.try_push(Item{1}); });
    }
}

// The loop of a producer worker: the items of each window, which it
// publishes, then the ops of the latency phase until the stop.  A run of
// kOpsPerCheck ops counts in the window of the phase that the worker read
// before it.
template <OpKind Kind>
void run_producer(RowState& state, ThreadLine& line) noexcept {
    static_cast<void>(state.ready.value.fetch_add_acq_rel(1));
    while (read_phase(state) == Phase::Wait)
        CRUCIBLE_SPIN_PAUSE;
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops{};
    for (::fixy::Option<WindowIndex> window = window_of(read_phase(state)); window.is_some();
         window = window_of(read_phase(state))) {
        for (std::uint64_t op = 0; op < kOpsPerCheck; ++op)
            run_op<Kind>(state, line);
        for (WindowIndex const index : window)
            ops.at(index) += kOpsPerCheck * kItemsPerOp<Kind>;
    }
    for (WindowIndex const index : ops.indices())
        line.window_ops.at(index).store_release(ops.at(index));
    line.is_published.store_release(true);
    while (read_phase(state) != Phase::Stop) {
        for (std::uint64_t op = 0; op < kOpsPerCheck; ++op)
            run_op<Kind>(state, line);
    }
}

// The sum of the items that a pop took: one item, or each item of a batch.
// The consumer adds each popped item into its sum, so the read of each cell
// stays in the code, as in a consumer that uses its items.
[[gnu::always_inline]] inline std::uint64_t sum_of(::fixy::Option<Item> item) noexcept {
    return std::move(item).value_or(Item{0});
}

[[gnu::always_inline]] inline std::uint64_t sum_of(::fixy::View<Item> out, std::size_t count) noexcept {
    std::uint64_t sum = 0;
    for (Item const item : out.window(0, count).expect("a batch pop gives at most the count of its View"))
        sum += item;
    return sum;
}

// One pop of the consumer of an op: true when it took an item.  It adds
// the items that it took into sum.
template <OpKind Kind>
[[gnu::always_inline]] inline bool pop_once(RowState& state, [[maybe_unused]] ::fixy::View<Item> out,
                                            std::uint64_t& sum) noexcept {
    // A pop of an MPSC ring is a merge of the producers, so it takes Nondet.
    [[maybe_unused]] eff::cap::Nondet const merge = eff::testing::test().nondet;
    auto const take_one = [&sum](::fixy::Option<Item> item) noexcept {
        bool const has_item = item.is_some();
        sum += sum_of(std::move(item));
        return has_item;
    };
    auto const take_batch = [&sum, out](std::size_t count) noexcept {
        sum += sum_of(out, count);
        return count != 0;
    };
    if constexpr (Kind == OpKind::MpscPush) {
        return take_one(state.mpsc.try_pop(merge));
    } else if constexpr (Kind == OpKind::MpscPackedPush) {
        return take_one(state.mpsc_packed.try_pop(merge));
    } else if constexpr (Kind == OpKind::MpscBatchPush) {
        return take_batch(state.mpsc.try_pop_batch(merge, out));
    } else if constexpr (Kind == OpKind::MpscReferencePush) {
        return take_one(state.mpsc_reference.try_pop());
    } else if constexpr (Kind == OpKind::SpscPush) {
        return take_one(state.spsc.try_pop());
    } else if constexpr (Kind == OpKind::SpscProvenPush) {
        return take_one(state.spsc_proven.try_pop_proven(ProvenOwner::consumer()));
    } else if constexpr (Kind == OpKind::SpscBatchPush) {
        return take_batch(state.spsc.try_pop_batch(out));
    } else {
        return take_one(state.spsc_reference.try_pop());
    }
}

// The loop of the consumer of an op: it pops until the stop, and it counts
// no item of the window.  It stores the sum of its items into its line at
// the end, so no read of a cell is dead code.
template <OpKind Kind>
void run_consumer(RowState& state, ThreadLine& line) noexcept {
    ::fixy::FixedArray<Item, kPopBatch> out{};
    std::uint64_t sum = 0;
    line.is_published.store_release(true);
    static_cast<void>(state.ready.value.fetch_add_acq_rel(1));
    while (read_phase(state) != Phase::Stop) {
        for (std::uint64_t pop = 0; pop < kOpsPerCheck; ++pop) {
            if (!pop_once<Kind>(state, out.view(), sum)) CRUCIBLE_SPIN_PAUSE;
        }
    }
    line.cell.store_release(sum);
}

// What the window of the main thread measured: its items, its wall time and
// the time that it lost in each window, and the pause of the whole window.
struct Window {
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops{};
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> wall_ns{};
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> lost_ns{};
    bench::Pause pause{};
};

// The window of the main thread on the CPU of slot 0 (THE PHASES OF A ROW).
// It starts each window, runs the op for its part of kWindowNs, and then
// starts the latency phase.  The check of each window reads two clocks and no
// file of procfs, so it does not change the basis of the pause of the whole
// window (THE WINDOWS of bench/bench_harness.h).
template <OpKind Kind>
[[nodiscard]] Window run_window(RowState& state, ThreadLine& line, bench::CpuId cpu) noexcept {
    bench::CoreHold const hold{cpu};
    ::fixy::InitLoadCtx const startup{eff::testing::init()};
    eff::cap::Nondet const nondet = eff::testing::test().nondet;
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(startup);
    auto const now = [&clock, &nondet]() noexcept {
        return clock.read(nondet).expect("a read of the monotonic clock").peek();
    };
    bench::PauseReading const before = bench::detail::read_pause();
    Window window{};
    bench::WindowClock window_clock = bench::detail::read_window_clock();
    std::uint64_t start = now();
    for (WindowIndex const index : ::fixy::IndexRange<bench::kWindows>{}) {
        state.phase.phase.store_release(phase_of(index));
        std::uint64_t end = start;
        std::uint64_t ops = 0;
        while (end - start < kWindowNs / bench::kWindows) {
            for (std::uint64_t op = 0; op < kOpsPerClock; ++op)
                run_op<Kind>(state, line);
            ops += kOpsPerClock * kItemsPerOp<Kind>;
            end = now();
        }
        bench::WindowClock const window_end = bench::detail::read_window_clock();
        window.ops.at(index) = ops;
        window.wall_ns.at(index) = end - start;
        window.lost_ns.at(index) =
            bench::detail::lost_between(::fixy::side_as<bench::side::ReadingBefore>(window_clock),
                                        ::fixy::side_as<bench::side::ReadingAfter>(window_end));
        window_clock = window_end;
        start = end;
    }
    state.phase.phase.store_release(Phase::Latency);
    bench::PauseReading const after = bench::detail::read_pause();
    window.pause = bench::detail::pause_between(::fixy::side_as<bench::side::ReadingBefore>(before),
                                                ::fixy::side_as<bench::side::ReadingAfter>(after));
    return window;
}

// The result of one row.
struct RowResult {
    ThreadCount threads{1};
    ConsumerPlace place = ConsumerPlace::LastSlot;
    // The items of the producers in one second, in hundredths of a million.
    std::uint64_t centi_mops = 0;
    // The throughput of the producers in each window, in millions of items in
    // one second, summed up by its median and its spread, and the count of
    // the windows in which the main thread lost more than the bound of the
    // pause check (THE WINDOWS of bench/bench_harness.h).
    bench::WindowSpread windows{};
    std::size_t paused_windows = 0;
    bench::Pause window_pause{};
    bench::Report latency{};
};

// The name of an op.
template <OpKind Kind>
[[nodiscard]] ::fixy::TextView name_of() noexcept {
    if constexpr (Kind == OpKind::SharedAdd) return ::fixy::TextView{"shared add"};
    if constexpr (Kind == OpKind::OwnAdd) return ::fixy::TextView{"own add"};
    if constexpr (Kind == OpKind::MpscPush) return ::fixy::TextView{"mpsc push"};
    if constexpr (Kind == OpKind::MpscPackedPush) return ::fixy::TextView{"mpsc packed push"};
    if constexpr (Kind == OpKind::MpscBatchPush) return ::fixy::TextView{"mpsc batch push"};
    if constexpr (Kind == OpKind::MpscReferencePush) return ::fixy::TextView{"mpsc reference push"};
    if constexpr (Kind == OpKind::SpscPush) return ::fixy::TextView{"spsc push"};
    if constexpr (Kind == OpKind::SpscProvenPush) return ::fixy::TextView{"spsc proven push"};
    if constexpr (Kind == OpKind::SpscBatchPush) return ::fixy::TextView{"spsc batch push"};
    return ::fixy::TextView{"spsc reference push"};
}

// The name of the latency report of an op in a row, with the count of the
// threads of the row and the place of its consumer, such as
// "spsc push [latency of one op, 2 threads, consumer in another L3]".  Each
// row gives a report, so each report of the JSON tail has a name of its own.
template <OpKind Kind>
[[nodiscard]] bench::Run run_of(ThreadCount threads, ConsumerPlace place) {
    ::fixy::TextView const where =
        place == ConsumerPlace::OtherL3 ? ::fixy::TextView{", consumer in another L3"} : ::fixy::TextView{};
    ::fixy::FixedText<96> name{};
    static_cast<void>(
        ::fixy::format(name, "{} [latency of one op, {} threads{}]", name_of<Kind>(), threads.raw(), where)
            .expect("the name of a latency report fits in 96 characters"));
    return bench::Run{name};
}

// The first CPU of the spread outside the L3 group of the CPU of slot 0, or
// none when sysfs gives no group or the spread holds no such CPU.
// Complexity: linear in the slots of the spread.
[[nodiscard]] ::fixy::Option<bench::CpuId> other_l3_cpu_of(bench::Spread const& spread) noexcept {
    bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none());
    crucible::warden::CpuSet const group = bench::read_l3_group(main_cpu);
    if (group.is_empty()) return ::fixy::none;
    for (std::uint32_t slot = 1; slot < spread.count(); ++slot) {
        bench::CpuId const cpu = spread.of_slot(bench::ThreadSlot{slot}).value_or(bench::CpuId::none());
        if (cpu.is_valid() && !group.contains(cpu.raw())) return ::fixy::some(cpu);
    }
    return ::fixy::none;
}

// The CPU of a slot of a row.  The consumer of a row of the place OtherL3
// runs on the CPU that other_l3_cpu_of gives.
[[nodiscard]] bench::CpuId cpu_of_slot(bench::Spread const& spread, bench::ThreadSlot slot, Role role,
                                       ConsumerPlace place) noexcept {
    if (role == Role::Consumer && place == ConsumerPlace::OtherL3) {
        return other_l3_cpu_of(spread).expect("a row of a consumer in another L3 group has such a CPU");
    }
    return spread.of_slot(slot).expect("a row has at most the slots of the spread");
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the rows of the bare rings run their workers on std::jthread")
namespace {

// The workers of one row, one std::jthread for each slot from slot 1 on (THE
// RAW WORKERS).  A worker pins itself to the CPU of its slot before it runs.
// The join waits for each worker, and the end of the row calls it after the
// stop.
class RawWorkers {
public:
    template <OpKind Kind>
    void start(RowState& state, bench::Spread const& spread, ThreadCount threads, ConsumerPlace place) {
        std::uint32_t const consumer_slot = kHasConsumer<Kind> ? threads.raw() - 1 : threads.raw();
        for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
            Role const role = slot == consumer_slot ? Role::Consumer : Role::Producer;
            ThreadLine& line = line_of(state, bench::ThreadSlot{slot});
            bench::CpuId const cpu = cpu_of_slot(spread, bench::ThreadSlot{slot}, role, place);
            threads_[slot] = std::jthread{[&state, &line, role, cpu]() noexcept {
                bench::CoreHold const hold{cpu};
                if (role == Role::Consumer) {
                    run_consumer<Kind>(state, line);
                } else {
                    run_producer<Kind>(state, line);
                }
            }};
        }
    }

    void join() noexcept {
        for (std::jthread& thread : threads_) {
            if (thread.joinable()) thread.join();
        }
    }

private:
    std::array<std::jthread, kMaxThreads> threads_{};
};

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// Runs one row of an op on the first threads.count slots of the spread.
template <OpKind Kind>
[[nodiscard]] RowResult run_row(bench::Spread const& spread, ThreadCount threads, ConsumerPlace place) {
    RowState state{};
    for (::fixy::Index<kBatch> const index : state.batch.indices())
        state.batch.at(index) = Item{index.raw() + 1};
    RawWorkers workers{};
    workers.start<Kind>(state, spread, threads, place);
    while (state.ready.value.load_acquire() + 1 < threads.raw())
        CRUCIBLE_SPIN_PAUSE;

    ThreadLine& own = line_of(state, bench::kMeasuringSlot);
    bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).expect("a row has the slot of the main thread");
    Window const window = run_window<Kind>(state, own, main_cpu);
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops = window.ops;
    for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
        ThreadLine const& line = line_of(state, bench::ThreadSlot{slot});
        while (!line.is_published.load_acquire())
            CRUCIBLE_SPIN_PAUSE;
        for (WindowIndex const index : ops.indices())
            ops.at(index) += line.window_ops.at(index).load_acquire();
    }

    std::uint64_t all_ops = 0;
    std::uint64_t all_wall_ns = 0;
    ::fixy::FixedArray<double, bench::kWindows> window_mops{};
    for (WindowIndex const index : ops.indices()) {
        std::uint64_t const wall_ns = window.wall_ns.at(index);
        all_ops += ops.at(index);
        all_wall_ns += wall_ns;
        window_mops.at(index) =
            wall_ns == 0 ? 0.0 : static_cast<double>(ops.at(index)) * 1000.0 / static_cast<double>(wall_ns);
    }

    RowResult result{.threads = threads,
                     .place = place,
                     .centi_mops = all_wall_ns == 0 ? 0 : all_ops * 100000 / all_wall_ns,
                     .windows = bench::spread_of_windows(window_mops.view()),
                     .paused_windows = bench::detail::paused_windows_of(window.lost_ns.view(), window.pause.basis),
                     .window_pause = window.pause};
    result.latency = run_of<Kind>(threads, place)
                         .core(main_cpu.raw())
                         .batch(1)
                         .samples(kLatencySamples)
                         .warmup(kLatencyWarmup)
                         .max_wall_ms(kLatencyMs)
                         .measure([&state, &own] { run_op<Kind>(state, own); });
    if (threads.raw() <= 16) bench::print_thread_cpus("the row");
    state.phase.phase.store_release(Phase::Stop);
    workers.join();
    return result;
}

// The producers of a row: each thread, but the consumer of an op with one.
template <OpKind Kind>
[[nodiscard]] std::uint64_t producers_of(ThreadCount threads) noexcept {
    return kHasConsumer<Kind> ? threads.raw() - 1 : threads.raw();
}

// The row that the efficiency of each row of an op compares with: its
// producers and its throughput in hundredths of a million items in a second.
struct EfficiencyBase {
    std::uint64_t producers = 1;
    std::uint64_t centi_mops = 0;
};

// Prints one row: its threads, the place of its consumer, its throughput, its
// efficiency over the base times the ratio of the producers, the median and
// the spread of the throughput of its windows, its pause, and the report of
// its latency.  The ratios print with two decimals, and the spread with one.
// A pause of the whole window or of one window over the bound makes the row
// not valid.
template <OpKind Kind>
void print_row(RowResult const& row, EfficiencyBase const& base) noexcept {
    std::uint64_t const ideal = base.centi_mops * producers_of<Kind>(row.threads) / base.producers;
    std::uint64_t const efficiency = ideal == 0 ? 0 : row.centi_mops * 100 / ideal;
    auto const median = static_cast<std::uint64_t>(row.windows.median * 100.0 + 0.5);
    auto const spread = static_cast<std::uint64_t>(row.windows.spread * 1000.0 + 0.5);
    bool const is_paused = row.window_pause.is_over_bound() || row.paused_windows > 0;
    ::fixy::TextView const where =
        row.place == ConsumerPlace::OtherL3 ? ::fixy::TextView{", consumer in another L3"} : ::fixy::TextView{};
    // The rows of the harness go through stdio, and report() does not.
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "  {} threads{}: {}.{}{} Mops/s, efficiency {}.{}{}", row.threads.raw(), where,
                   row.centi_mops / 100, row.centi_mops % 100 / 10, row.centi_mops % 10, efficiency / 100,
                   efficiency % 100 / 10, efficiency % 10);
    ::fixy::report(::fixy::Sink::Out, ", {} windows: median {}.{}{} Mops/s, spread {}.{}%", row.windows.windows,
                   median / 100, median % 100 / 10, median % 10, spread / 10, spread % 10);
    ::fixy::report(::fixy::Sink::Out, ", window pause {} us, other thread delay {} us, {} paused windows{}\n",
                   row.window_pause.own_ns() / 1000, row.window_pause.other.run_delay_ns / 1000, row.paused_windows,
                   is_paused ? ::fixy::TextView{"  NOT VALID"} : ::fixy::TextView{});
    row.latency.print_text();
}

// The rows of one op: one row for each count of threads, or the two rows of
// the two places of an spsc op.  Then the line of each row.  The first row is
// the base of the efficiency.  The latency report of each row goes to
// reports, for the JSON tail of the bench.
template <OpKind Kind>
void run_op_rows(bench::Spread const& spread, ::fixy::View<ThreadCount const> counts, ::fixy::TextView title,
                 ::fixy::FixedList<bench::Report, kMaxReports>& reports) {
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "\n=== {} ===\n", title);
    ::fixy::FixedList<RowResult, kMaxRows> rows{};
    if constexpr (kIsSpsc<Kind>) {
        if (spread.count() < 2) return;
        static_cast<void>(rows.append(run_row<Kind>(spread, ThreadCount{2}, ConsumerPlace::LastSlot))
                              .expect("an op has at most kMaxRows rows"));
        if (other_l3_cpu_of(spread).is_some()) {
            static_cast<void>(rows.append(run_row<Kind>(spread, ThreadCount{2}, ConsumerPlace::OtherL3))
                                  .expect("an op has at most kMaxRows rows"));
        }
    } else {
        for (ThreadCount const threads : counts) {
            // A row of an op with a consumer needs a producer beside its consumer.
            if (producers_of<Kind>(threads) == 0) continue;
            static_cast<void>(rows.append(run_row<Kind>(spread, threads, ConsumerPlace::LastSlot))
                                  .expect("an op has at most kMaxRows rows"));
        }
    }
    if (rows.size() == 0) return;
    RowResult const& first = *rows.get(0).expect("the test above found a row");
    EfficiencyBase const base{.producers = producers_of<Kind>(first.threads), .centi_mops = first.centi_mops};
    for (RowResult const& row : rows) {
        print_row<Kind>(row, base);
    }
    for (RowResult& row : rows) {
        bench::add_report(reports, std::move(row.latency));
    }
}

// The groups of ops of a run (THE GROUPS OF OPS).
enum class OpGroup : std::uint8_t {
    Add,
    Mpsc,
    Spsc,
};

// The group that an argument names, or none for an argument that names no
// group, such as a count of threads.
[[nodiscard]] ::fixy::Option<OpGroup> group_of(::fixy::TextView argument) noexcept {
    if (argument == ::fixy::TextView{"add"}) return ::fixy::some(OpGroup::Add);
    if (argument == ::fixy::TextView{"mpsc"}) return ::fixy::some(OpGroup::Mpsc);
    if (argument == ::fixy::TextView{"spsc"}) return ::fixy::some(OpGroup::Spsc);
    return ::fixy::none;
}

// The groups that the arguments name, or each group when no argument names
// one.  Complexity: linear in the arguments.
[[nodiscard]] ::fixy::FixedArray<bool, 3> groups_of(::fixy::os::Args const& arguments) noexcept {
    ::fixy::FixedArray<bool, 3> groups{};
    bool is_named = false;
    for (::fixy::CText const argument : arguments) {
        for (OpGroup const group : group_of(argument.view())) {
            *groups.get(static_cast<std::size_t>(group)).expect("a group is below three") = true;
            is_named = true;
        }
    }
    if (!is_named) {
        for (bool& group : groups)
            group = true;
    }
    return groups;
}

// Reads the counts of the rows (THE COUNTS OF THREADS).  An argument that
// names a group of ops is no count.
[[nodiscard]] ::fixy::FixedList<ThreadCount, kMaxRows> counts_of(::fixy::os::Args const& arguments,
                                                                 bench::Spread const& spread) noexcept {
    ::fixy::FixedList<ThreadCount, kMaxRows> counts{};
    auto const add = [&counts, &spread](ThreadCount threads) noexcept {
        if (threads.raw() > spread.count() || threads.raw() > kMaxThreads) {
            ::fixy::fatal("a row of {} threads is larger than the {} slots of the run, or than {}", threads.raw(),
                          spread.count(), kMaxThreads);
        }
        static_cast<void>(counts.append(threads).expect("a run has at most kMaxRows counts"));
    };
    add(ThreadCount{1});
    if (spread.count() >= 2) add(ThreadCount{2});
    std::size_t groups = 0;
    for (::fixy::CText const argument : arguments) {
        if (group_of(argument.view()).is_some()) ++groups;
    }
    if (arguments.size() <= 1 + groups) {
        for (std::uint32_t threads = 4; threads <= 16 && threads <= spread.count(); threads *= 2)
            add(ThreadCount{threads});
        return counts;
    }
    bool is_program = true;
    for (::fixy::CText const argument : arguments) {
        if (is_program || group_of(argument.view()).is_some()) {
            is_program = false;
            continue;
        }
        std::uint32_t const threads =
            ::fixy::parse<std::uint32_t>(argument.view())
                .match([](std::uint32_t parsed) noexcept { return parsed; },
                       [&argument](::fixy::ParseError) noexcept -> std::uint32_t {
                           ::fixy::fatal("the argument '{}' is not a count of threads", argument.view());
                       });
        if (threads > 2) add(ThreadCount{threads});
    }
    return counts;
}

// The bench with the arguments of main (THE COUNTS OF THREADS).
int run_bench(::fixy::os::Args const& arguments) {
    bench::print_system_info();
    bench::elevate_priority();
    bench::Spread const spread = bench::Spread::of_calling_thread();
    ::fixy::FixedList<ThreadCount, kMaxRows> const counts = counts_of(arguments, spread);
    bench::detail::flush_standard_output();
    if (spread.source() == bench::SpreadSource::Claim) {
        ::fixy::report(::fixy::Sink::Out, "  slots: the {} claimed cores, slot k on core k, from cpu{}\n",
                       spread.count(), spread.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none()).raw());
    } else {
        ::fixy::report(::fixy::Sink::Out,
                       "  NOTE: no claimed core.  The {} slots take one CPU of each core of the affinity, then the "
                       "SMT siblings, and other work can use them, so each row is noisy\n",
                       spread.count());
    }
    ::fixy::report(::fixy::Sink::Out, "  the consumer in another L3 group: cpu{}\n",
                   other_l3_cpu_of(spread).value_or(bench::CpuId::none()).raw());
    for (ThreadCount const threads : counts) {
        if (threads.raw() > bench::kPauseThreads) {
            ::fixy::report(::fixy::Sink::Out,
                           "  NOTE: a row of {} threads has more threads than the {} that the pause check reads\n",
                           threads.raw(), bench::kPauseThreads);
        }
    }
    ::fixy::FixedArray<bool, 3> const groups = groups_of(arguments);
    auto const has_group = [&groups](OpGroup group) noexcept {
        return *groups.get(static_cast<std::size_t>(group)).expect("a group is below three");
    };
    ::fixy::FixedList<bench::Report, kMaxReports> reports{};
    if (has_group(OpGroup::Add)) {
        run_op_rows<OpKind::SharedAdd>(spread, counts.view(), "shared add: one cell for all threads", reports);
        run_op_rows<OpKind::OwnAdd>(spread, counts.view(), "own add: one cell on its own line for each thread",
                                    reports);
    }
    if (has_group(OpGroup::Mpsc)) {
        run_op_rows<OpKind::MpscPush>(spread, counts.view(), "mpsc push: the last thread is the one consumer", reports);
        run_op_rows<OpKind::MpscPackedPush>(spread, counts.view(),
                                            "mpsc packed push: the ring with packed cells, one consumer", reports);
        run_op_rows<OpKind::MpscBatchPush>(spread, counts.view(),
                                           "mpsc batch push: batches of 16, and batch pops of up to 64", reports);
        if (kHasMpscReference) {
            run_op_rows<OpKind::MpscReferencePush>(
                spread, counts.view(), "mpsc reference push: the pinned MPMCQueue of Erik Rigtorp", reports);
        }
    }
    if (has_group(OpGroup::Spsc)) {
        run_op_rows<OpKind::SpscPush>(spread, counts.view(), "spsc push: the main thread produces", reports);
        run_op_rows<OpKind::SpscProvenPush>(spread, counts.view(),
                                            "spsc proven push: the calls that take the proof of each side", reports);
        run_op_rows<OpKind::SpscBatchPush>(spread, counts.view(),
                                           "spsc batch push: batches of 16, and batch pops of up to 64", reports);
        if (kHasSpscReference) {
            run_op_rows<OpKind::SpscReferencePush>(
                spread, counts.view(), "spsc reference push: the pinned SPSCQueue of Erik Rigtorp", reports);
        }
    }
    bench::emit_reports_json(reports.view(), bench::env_json());
    return 0;
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return run_bench(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
