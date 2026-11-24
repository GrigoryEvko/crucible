// The scaling rows of the primitives of the base, from one thread to many.
//
// Ops
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
//   mpsc permissioned  try_push of a producer handle of the merge channel of
//   push               fixy/concurrent/PermissionedChannel.h on each producer,
//                      and try_pop of its consumer handle on the last thread
//                      (the permissioned rows)
//   mpsc batch push    try_push_batch of kBatch items from each producer, and
//                      try_pop_batch of up to kPopBatch items on the consumer
//   mpsc reference     try_push and try_pop of the pinned MPMCQueue of Erik
//                      Rigtorp (utils/pins.txt), a queue with a turn word in
//                      each slot in the form of Dmitry Vyukov
//   lanes push         try_push of fixy/concurrent/LaneRing.h into the lane
//                      of each producer, and try_pop on the last thread, which
//                      takes one item of each lane in turn (the lanes)
//   lanes permissioned try_push of a producer handle of a lane of the lanes
//   push               channel of fixy/concurrent/PermissionedChannel.h, and
//                      try_pop of its consumer handle (the permissioned rows)
//   mpsc slow push     the mpsc push, with one slow producer (the slow producer)
//   lanes slow push    the lanes push, with one slow producer
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
// Lanes
//   The ring of lanes has kBenchLanes lanes of kLaneCapacity cells.  The
//   producer of slot k pushes into lane k, and a row ends each lane past its
//   producers before its threads start.  The consumer waits for the lane of
//   its turn, so each producer of a row pushes at the pace of the slowest one.
//   A row of more producers than lanes gives no row of a lanes op.
//
// Slow producer
//   In a row of a slow op, the producer of the last slot before the consumer
//   runs kSlowPauses spin hints before each push.  The other producers push at
//   full speed.  A row of fewer than three threads has no slow producer, and
//   gives no row.  The shared ring of the mpsc slow push takes the items of the
//   fast producers while the slow one waits.  The ring of lanes waits for the
//   slow lane, which sets the pace of the whole row (the stall of
//   fixy/concurrent/LaneRing.h).
//
// Places of the consumer
//   The consumer of an mpsc row is the thread of the last slot of the row.
//   Each spsc op has two rows of two threads: one with the consumer on the
//   CPU of slot 1, in the L3 group of the producer when the claim holds a
//   second core of that group, and one with the consumer on the first CPU of
//   the spread outside the L3 group of the producer.  A spread with no CPU
//   outside that group gives no row of the second place.
//
// Threads of a row
//   A row of N threads runs the op on N threads at the same time, one thread
//   on the CPU of each slot 0 to N - 1 of the spread of the run (the spread
//   of a run of bench/bench_harness.h).  Slot 0 is the main thread, a
//   producer.  Each other slot is a worker on a std::jthread, which pins
//   itself to the CPU of its slot (the raw workers).
//
// Raw workers
//   The rows measure the bare rings of fixy/concurrent/ and their pinned
//   references.  The threads of a row share the rings of RowHome, and a
//   ring keeps plain positions that one side writes and the other reads.  No
//   type of the base states that, so no door of the base can check the
//   workers.  The rows thus keep the raw rings and raw threads as a
//   measurement, in a MEASURE region.
//
// Permissioned rows
//   The rows of the permissioned channel run each worker beside the main
//   thread through run_beside of fixy/os/ThreadTasks.h, and not on a raw
//   thread.  The main thread opens the frames of its own producer end and of
//   the consumer end (the frame doors of fixy/concurrent/
//   PermissionedChannel.h), and the workers start and join in the inner body.
//   The consumer end moves into the body of its worker, and the body moves it
//   onto its own stack.  Each producer worker opens the frame of its own end
//   in its body, on its own stack.  So no end shares a line with an object of
//   the frame of the main thread.  A body borrows only the atomics of the row
//   (RowControl) and the channel, which is synchronized, as the door of the
//   thread asks.  The count of the bodies is a count of template arguments, so
//   these rows take the counts 2, 4, 8 and 16.
//
// Phases of a row
//   1. The window: each thread runs the op and counts it for kWindowNs.  The
//      row gives the items of the producers of all threads in one second, and
//      the efficiency: that throughput over the throughput of the first row
//      of the op, times the ratio of the producers.  For an mpsc op, the base
//      is the row of one producer and the consumer.  The pause check of the
//      harness reads the threads over the window.
//      The window is bench::kWindows consecutive windows of equal time, one
//      phase each (the windows of bench/bench_harness.h).  Each thread counts
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
// Counts of threads
//   With no argument, the rows have 1, 2, 4, 8 and 16 threads, up to the
//   count of the slots.  The bench states nine cores, so loop bench gives it
//   the eight cores of one L3 group and one core of a second group.  The rows
//   of 1 to 8 threads then keep one group, and the ninth core holds the
//   consumer of the spsc rows in another group.  Each argument is one more
//   count of threads after the rows of 1 and 2 threads, which are the bases of
//   the efficiency.  A count above the slots or above kMaxThreads ends the run.
//
// Groups of ops
//   An argument add, mpsc or spsc selects the ops of that group: the two
//   adds, the mpsc ops, or the spsc ops.  With no such argument,
//   the run gives each group.  So two builds of one ring can run in turns:
//       python3 utils/bin/loop bench --cores 9 --command -- BINARY spsc
//
// Commands
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
#include <fixy/concurrent/LaneRing.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/os/Args.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

#include "bench_harness.h"

namespace {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

// The context that starts the workers of a permissioned row: Bg for the
// threads, Alloc for their frames.
using Starter = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using Item = std::uint64_t;

// The threads of one row, at most.  The command of the owner runs 224.
constexpr std::size_t kMaxThreads = 256;
// The rows of one op, at most, and the latency reports of the fourteen ops.
constexpr std::size_t kMaxRows = 16;
constexpr std::size_t kMaxReports = 14 * kMaxRows;
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
// The lanes of the ring of lanes and the cells of each lane (the lanes).
constexpr ::fixy::concurrent::LaneCount kBenchLanes{16};
constexpr std::size_t kLaneCapacity = 1024;
using BenchLaneIndex = ::fixy::concurrent::LaneIndex<kBenchLanes>;
// The spin hints of the slow producer before each push (the slow producer).
constexpr std::uint32_t kSlowPauses = 64;

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
    MpscPermissionedPush,
    LanesPush,
    LanesPermissionedPush,
    MpscSlowPush,
    LanesSlowPush,
    SpscPush,
    SpscProvenPush,
    SpscBatchPush,
    SpscReferencePush,
};

// The owner of the ring of the proven rows (the proven form of
// fixy/concurrent/SpscRing.h).  A row runs one producer and one consumer of
// that ring, so the bench makes the proof of each side, as the channel of a
// wire makes it for its handles.
struct ProvenOwner final {
    using Ring = ::fixy::concurrent::SpscRing<Item, kRingCapacity, ProvenOwner>;

    [[nodiscard]] static constexpr auto producer() noexcept { return Ring::producer_proof{}; }
    [[nodiscard]] static constexpr auto consumer() noexcept { return Ring::consumer_proof{}; }
};

// The owner of the MPSC rings of the rows (the writers of
// fixy/concurrent/MpscRing.h).  A row runs one consumer of a ring, so the
// bench makes the proof of the consumer, as the channel of a merge makes it
// for its handle.
struct MpscOwner final {
    using Ring = ::fixy::concurrent::MpscRing<Item, kRingCapacity, MpscOwner>;
    using PackedRing =
        ::fixy::concurrent::MpscRing<Item, kRingCapacity, MpscOwner, ::fixy::concurrent::CellSpacing::packed>;

    [[nodiscard]] static constexpr auto consumer() noexcept { return Ring::consumer_proof{}; }
};

// The owner of the ring of lanes of the rows (the writers of
// fixy/concurrent/LaneRing.h).  Each producer of a row holds one lane, and one
// thread pops, so the bench mints the proof of a lane for each push of its
// producer, and the proof of the consumer for each pop, as the lanes channel
// mints them for its handles.
struct LanesOwner final {
    using Ring = ::fixy::concurrent::LaneRing<Item, kLaneCapacity, kBenchLanes, LanesOwner>;

    [[nodiscard]] static Ring::LaneProof lane(BenchLaneIndex lane) noexcept { return Ring::LaneProof{lane}; }
    [[nodiscard]] static Ring::ConsumerProof consumer() noexcept { return Ring::ConsumerProof{}; }
};

// True for an op with one consumer on the thread of a slot.
template <OpKind Kind>
inline constexpr bool kHasConsumer = Kind != OpKind::SharedAdd && Kind != OpKind::OwnAdd;

// True for an op of the SPSC ring or of its reference.
template <OpKind Kind>
inline constexpr bool kIsSpsc = Kind == OpKind::SpscPush || Kind == OpKind::SpscProvenPush
                             || Kind == OpKind::SpscBatchPush || Kind == OpKind::SpscReferencePush;

// True for an op of a pinned reference.
template <OpKind Kind>
inline constexpr bool kIsReference = Kind == OpKind::MpscReferencePush || Kind == OpKind::SpscReferencePush;

// True for an op of the ring of lanes or of the lanes channel (the lanes).
template <OpKind Kind>
inline constexpr bool kIsLanes =
    Kind == OpKind::LanesPush || Kind == OpKind::LanesPermissionedPush || Kind == OpKind::LanesSlowPush;

// True for an op with one slow producer (the slow producer).
template <OpKind Kind>
inline constexpr bool kHasSlowProducer = Kind == OpKind::MpscSlowPush || Kind == OpKind::LanesSlowPush;

// The items of one op.
template <OpKind Kind>
inline constexpr std::uint64_t kItemsPerOp =
    Kind == OpKind::MpscBatchPush || Kind == OpKind::SpscBatchPush ? kBatch : 1;

// Where the consumer of a row runs (the places of the consumer).
enum class ConsumerPlace : std::uint8_t {
    LastSlot,
    OtherL3,
};

// The phase of a row (the phases of a row).  The main thread writes it.  The
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
    return ::fixy::enum_at<Phase>(static_cast<std::size_t>(Phase::Window0) + window.raw())
        .expect("the phases count up from zero, and each window index names a phase");
}

// The role of the thread of a slot in a row.
enum class Role : std::uint8_t {
    Producer,
    Consumer,
};

struct ThreadCountTag final {};
using ThreadCount = ::fixy::Id<ThreadCountTag, std::uint32_t>;

// The pace of a producer (the slow producer).
enum class Pace : std::uint8_t {
    full,
    slow,
};

// The lane and the pace of one producer of a row.  Each producer holds its
// seat as a value, which the main thread gives it before its thread starts.
struct Seat {
    BenchLaneIndex lane{::fixy::index<0>()};
    Pace pace = Pace::full;
};

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

// The part of the state of a row that holds only atomics: the phase, the
// count of the workers that wait, the shared cell and the line of each
// thread.  A body beside the caller borrows only this part (the permissioned
// rows).
struct RowControl {
    PhaseLine phase{};
    // The workers that wait for the window.
    CountLine ready{};
    CountLine shared{};
    ::fixy::FixedArray<ThreadLine, kMaxThreads> lines{};
};

// The index of the line of a slot of a row.
using LineIndex = ::fixy::Index<kMaxThreads>;

// The state that the threads of one row share.  The main thread owns it, and
// it outlives each worker of the row.  Each row builds each ring, and it uses
// the one of its op.  The state holds only rings of the base, so a frame body
// can write it.
struct RowState : RowControl {
    MpscOwner::Ring mpsc{};
    MpscOwner::PackedRing mpsc_packed{};
    LanesOwner::Ring lanes{};
    ::fixy::concurrent::SpscRing<Item, kRingCapacity> spsc{};
    ProvenOwner::Ring spsc_proven{};
    // The items of each batch push.  No thread writes them after the start.
    ::fixy::FixedArray<Item, kBatch> batch{};
};

// The pinned references of a row, which each row builds beside its state.
// Their queues hold raw pointers, and the store check refuses a frame body
// that writes such an object.  So each op of a reference runs outside each
// frame.
struct RowReferences {
    MpscReference mpsc{};
    SpscReference spsc{};
};

// The state and the references of a row in one block of the heap, so an op
// of a reference reads its queue and the phase of the row from one address.
// A frame body captures only the state.
struct RowHome {
    RowState state{};
    RowReferences references{};
};

[[nodiscard]] Phase read_phase(RowControl const& control) noexcept { return control.phase.phase.load_acquire(); }

[[nodiscard]] ThreadLine const& line_of(RowControl const& control CRUCIBLE_LIFETIMEBOUND,
                                        bench::ThreadSlot slot) noexcept {
    return *control.lines.get(slot.raw()).expect("a slot of a row is below kMaxThreads");
}
void line_of(RowControl const&&, bench::ThreadSlot) =
    delete("the reference names a line of the control, and a temporary ends with the full expression");

// One push that waits while its ring is full, and that stops when the row
// stops, so no producer waits for a consumer that ended.
template <class Push>
[[gnu::always_inline]] inline void push_waiting(RowControl const& control, Push const& push) noexcept {
    while (!push()) {
        if (read_phase(control) == Phase::Stop) return;
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

// The delay of the slow producer before each push (the slow producer).
[[gnu::always_inline]] inline void wait_for_pace(Seat const& seat) noexcept {
    if (seat.pace == Pace::full) return;
    for (std::uint32_t pause = 0; pause < kSlowPauses; ++pause) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The seat of the producer of a slot of a row: lane k for the producer of slot
// k of a lanes op (the lanes), and the slow pace for the last producer of a
// slow op (the slow producer).  A row of a lanes op has at most kBenchLanes
// producers.
template <OpKind Kind>
[[nodiscard]] Seat seat_of(bench::ThreadSlot slot, ThreadCount threads) noexcept {
    Seat seat{};
    if constexpr (kIsLanes<Kind>) {
        seat.lane = BenchLaneIndex::check(slot.raw()).expect("a row of a lanes op has a lane for each producer");
    }
    if constexpr (kHasSlowProducer<Kind>) {
        std::uint32_t const producers = threads.raw() - 1;
        seat.pace = slot.raw() + 1 == producers ? Pace::slow : Pace::full;
    } else {
        static_cast<void>(threads);
    }
    return seat;
}

// One op of a producer (the ops).  Only the own add writes the line of the
// thread, so only its arm opens the frame of the line.  The line is a
// LineIndex, or a constant index for the main thread.  A reference op runs
// outside each frame (RowReferences).
template <OpKind Kind, class Line>
[[gnu::always_inline]] inline void run_op(RowHome& home, Line line, Seat const& seat) noexcept {
    RowState& state = home.state;
    if constexpr (Kind == OpKind::SharedAdd) {
        static_cast<void>(state.shared.value.fetch_add_acq_rel(1));
    } else if constexpr (Kind == OpKind::OwnAdd) {
        // Each thread writes only the line of its own slot, and each field of a line is an Atomic.
        state.lines.with_element(line, [] [[gnu::always_inline]] (ThreadLine & own) noexcept {
            static_cast<void>(own.cell.fetch_add_acq_rel(1));
        });
    } else if constexpr (Kind == OpKind::MpscPush) {
        push_waiting(state, [&state]() noexcept { return state.mpsc.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::LanesPush) {
        push_waiting(state,
                     [&state, &seat]() noexcept { return state.lanes.try_push(LanesOwner::lane(seat.lane), Item{1}); });
    } else if constexpr (Kind == OpKind::MpscSlowPush) {
        wait_for_pace(seat);
        push_waiting(state, [&state]() noexcept { return state.mpsc.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::LanesSlowPush) {
        wait_for_pace(seat);
        push_waiting(state,
                     [&state, &seat]() noexcept { return state.lanes.try_push(LanesOwner::lane(seat.lane), Item{1}); });
    } else if constexpr (Kind == OpKind::MpscPackedPush) {
        push_waiting(state, [&state]() noexcept { return state.mpsc_packed.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::MpscBatchPush) {
        // The batch of the MPSC ring is all or nothing.
        push_waiting(state, [&state]() noexcept { return state.mpsc.try_push_batch(state.batch.view()) == kBatch; });
    } else if constexpr (Kind == OpKind::MpscReferencePush) {
        push_waiting(state, [&home]() noexcept { return home.references.mpsc.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::SpscPush) {
        push_waiting(state, [&state]() noexcept { return state.spsc.try_push(Item{1}); });
    } else if constexpr (Kind == OpKind::SpscProvenPush) {
        push_waiting(
            state, [&state]() noexcept { return state.spsc_proven.try_push_proven(ProvenOwner::producer(), Item{1}); });
    } else if constexpr (Kind == OpKind::SpscBatchPush) {
        push_spsc_batch(state);
    } else {
        push_waiting(state, [&home]() noexcept { return home.references.spsc.try_push(Item{1}); });
    }
}

// The loop of a producer worker: the items of each window, which it
// publishes, then the ops of the latency phase until the stop.  A run of
// kOpsPerCheck ops counts in the window of the phase that the worker read
// before it.
// The op is a call of the thread, so a worker that owns the end of a channel
// runs the same loop.  The loop runs outside the frame of the line, and a
// frame of the line publishes the counts.
template <OpKind Kind, class Op>
void run_producer_loop(RowControl& control, Op const& op, LineIndex line) noexcept {
    static_cast<void>(control.ready.value.fetch_add_acq_rel(1));
    while (read_phase(control) == Phase::Wait)
        CRUCIBLE_SPIN_PAUSE;
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops{};
    for (::fixy::Option<WindowIndex> window = window_of(read_phase(control)); window.is_some();
         window = window_of(read_phase(control))) {
        for (std::uint64_t count = 0; count < kOpsPerCheck; ++count)
            op();
        for (WindowIndex const index : window)
            ops.with_element(index, [](std::uint64_t& count) noexcept { count += kOpsPerCheck * kItemsPerOp<Kind>; });
    }
    // Each thread writes only the line of its own slot, and each field of a line is an Atomic.
    control.lines.with_element(line, [&ops](ThreadLine& own) noexcept {
        for (WindowIndex const index : ops.indices()) {
            std::uint64_t const value = ops.at(index);
            // The main thread reads this Atomic only after is_published, and no other thread writes it.
            own.window_ops.with_element(
                index, [value](::fixy::Atomic<std::uint64_t>& cell) noexcept { cell.store_release(value); });
        }
        own.is_published.store_release(true);
    });
    while (read_phase(control) != Phase::Stop) {
        for (std::uint64_t count = 0; count < kOpsPerCheck; ++count)
            op();
    }
}

template <OpKind Kind>
void run_producer(RowHome& home, LineIndex line, Seat const& seat) noexcept {
    run_producer_loop<Kind>(
        home.state, [&home, line, &seat] [[gnu::always_inline]] () noexcept { run_op<Kind>(home, line, seat); }, line);
}

// The sum of the items that a pop took: one item, or each item of a batch.
// The consumer adds each popped item into its sum, so the read of each cell
// stays in the code, as in a consumer that uses its items.
[[gnu::always_inline]] inline std::uint64_t sum_of(::fixy::Option<Item> item) noexcept {
    return std::move(item).value_or(Item{0});
}

[[gnu::always_inline]] inline std::uint64_t sum_of(::fixy::View<Item const> out, std::size_t count) noexcept {
    std::uint64_t sum = 0;
    for (Item const item : out.window(0, count).expect("a batch pop gives at most the count of its View"))
        sum += item;
    return sum;
}

// One pop of the consumer of an op: true when it took an item.  It adds
// the items that it took into sum.
template <OpKind Kind, class Items>
[[gnu::always_inline]] inline bool pop_once(RowState& state, [[maybe_unused]] Items& out, std::uint64_t& sum) noexcept {
    // A pop of an MPSC ring is a merge of the producers, so it takes Nondet.
    [[maybe_unused]] eff::cap::Nondet const merge = eff::testing::test().nondet;
    auto const take_one = [&sum](::fixy::Option<Item> item) noexcept {
        bool const has_item = item.is_some();
        sum += sum_of(std::move(item));
        return has_item;
    };
    auto const take_batch = [&sum, &out](std::size_t count) noexcept {
        sum += sum_of(out.view(), count);
        return count != 0;
    };
    if constexpr (Kind == OpKind::MpscPush || Kind == OpKind::MpscSlowPush) {
        return take_one(state.mpsc.try_pop(MpscOwner::consumer(), merge));
    } else if constexpr (Kind == OpKind::LanesPush || Kind == OpKind::LanesSlowPush) {
        // The pop of the ring of lanes is in the order of its lanes, so it
        // takes no tag of Nondet.
        return take_one(state.lanes.try_pop(LanesOwner::consumer()));
    } else if constexpr (Kind == OpKind::MpscPackedPush) {
        return take_one(state.mpsc_packed.try_pop(MpscOwner::consumer(), merge));
    } else if constexpr (Kind == OpKind::MpscBatchPush) {
        auto [count, back] = ::fixy::reborrow(std::move(out), [&state, merge](auto lent) noexcept {
            return state.mpsc.try_pop_batch(MpscOwner::consumer(), merge, std::move(lent));
        });
        out = std::move(back);
        return take_batch(count);
    } else if constexpr (Kind == OpKind::SpscPush) {
        return take_one(state.spsc.try_pop());
    } else if constexpr (Kind == OpKind::SpscProvenPush) {
        return take_one(state.spsc_proven.try_pop_proven(ProvenOwner::consumer()));
    } else {
        auto [count, back] = ::fixy::reborrow(
            std::move(out), [&state](auto lent) noexcept { return state.spsc.try_pop_batch(std::move(lent)); });
        out = std::move(back);
        return take_batch(count);
    }
}

// One pop of the consumer of a reference op, with the rule of pop_once.
template <OpKind Kind>
[[gnu::always_inline]] inline bool pop_reference(RowReferences& references, std::uint64_t& sum) noexcept {
    auto const take_one = [&sum](::fixy::Option<Item> item) noexcept {
        bool const has_item = item.is_some();
        sum += sum_of(std::move(item));
        return has_item;
    };
    if constexpr (Kind == OpKind::MpscReferencePush) {
        return take_one(references.mpsc.try_pop());
    } else {
        return take_one(references.spsc.try_pop());
    }
}

// The loop of the consumer of an op: it pops until the stop, and it counts
// no item of the window.  It gives the sum of its items, and the caller
// stores the sum into its line, so no read of a cell is dead code.  The
// caller publishes its line before the loop, so the publish comes before the
// count of the ready threads.
// The pop is a call of the thread, as the op of run_producer_loop is.  The
// loop gives the pop its source on each call, so a pop of the end of a channel
// holds no borrow of the end.
template <class Pop, class Source, class Items>
[[nodiscard]] std::uint64_t run_consumer_loop(RowControl& control, Pop const& pop, Source& source,
                                              Items&& items) noexcept {
    std::uint64_t sum = 0;
    static_cast<void>(control.ready.value.fetch_add_acq_rel(1));
    while (read_phase(control) != Phase::Stop) {
        for (std::uint64_t count = 0; count < kOpsPerCheck; ++count) {
            if (!pop(source, items, sum)) CRUCIBLE_SPIN_PAUSE;
        }
    }
    return sum;
}

// The consumer of an op.  The pop of a ring of the base runs in the frame of
// the buffer of a batch pop.  The pop of a reference takes one item, and it
// runs outside each frame (RowReferences).
template <OpKind Kind>
void run_consumer(RowHome& home, LineIndex line) noexcept {
    RowState& state = home.state;
    // Each thread writes only the line of its own slot, and each field of a line is an Atomic.
    state.lines.with_element(line, [](ThreadLine& own) noexcept { own.is_published.store_release(true); });
    std::uint64_t item_sum = 0;
    if constexpr (kIsReference<Kind>) {
        item_sum = run_consumer_loop(
            state,
            [] [[gnu::always_inline]] (RowReferences & from, ::fixy::View<Item> /*items*/,
                                       std::uint64_t& sum) noexcept { return pop_reference<Kind>(from, sum); },
            home.references, ::fixy::View<Item>{});
    } else {
        ::fixy::FixedArray<Item, kPopBatch> out{};
        item_sum = out.with_mut([&state](auto run) noexcept {
            return run_consumer_loop(
                state,
                [] [[gnu::always_inline]] (RowState & from, auto& items, std::uint64_t& sum) noexcept {
                    return pop_once<Kind>(from, items, sum);
                },
                state, run);
        });
    }
    state.lines.with_element(line, [item_sum](ThreadLine& own) noexcept { own.cell.store_release(item_sum); });
}

// What the window of the main thread measured: its items, its wall time and
// the time that it lost in each window, and the pause of the whole window.
struct Window {
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops{};
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> wall_ns{};
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> lost_ns{};
    bench::Pause pause{};
};

// The window of the main thread on the CPU of slot 0 (the phases of a row).
// It starts each window, runs the op for its part of kWindowNs, and then
// starts the latency phase.  The check of each window reads two clocks and no
// file of procfs, so it does not change the basis of the pause of the whole
// window (the windows of bench/bench_harness.h).
template <OpKind Kind, class Op>
[[nodiscard]] Window run_window_loop(RowControl& control, Op const& op, bench::CpuId cpu) noexcept {
    bench::CoreHold const hold{cpu};
    ::fixy::InitLoadCtx const startup{eff::testing::init()};
    eff::cap::Nondet const nondet = eff::testing::test().nondet;
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(startup);
    auto const now = [&clock, &nondet]() noexcept {
        return clock.read(nondet).expect("a read of the monotonic clock").peek();
    };
    bench::detail::PauseFiles const pause_files = bench::detail::open_pause_files();
    auto const before = bench::detail::read_pause_before(pause_files);
    Window window{};
    bench::WindowClock window_clock = bench::detail::read_window_clock();
    std::uint64_t start = now();
    for (WindowIndex const index : ::fixy::IndexRange<bench::kWindows>{}) {
        control.phase.phase.store_release(phase_of(index));
        std::uint64_t end = start;
        std::uint64_t ops = 0;
        while (end - start < kWindowNs / bench::kWindows) {
            for (std::uint64_t count = 0; count < kOpsPerClock; ++count)
                op();
            ops += kOpsPerClock * kItemsPerOp<Kind>;
            end = now();
        }
        bench::WindowClock const window_end = bench::detail::read_window_clock();
        std::uint64_t const wall_ns = end - start;
        std::uint64_t const lost_ns =
            bench::detail::lost_between(::fixy::side_as<bench::side::ReadingBefore>(window_clock),
                                        ::fixy::side_as<bench::side::ReadingAfter>(window_end));
        window.ops.with_element(index, [ops](std::uint64_t& value) noexcept { value = ops; });
        window.wall_ns.with_element(index, [wall_ns](std::uint64_t& value) noexcept { value = wall_ns; });
        window.lost_ns.with_element(index, [lost_ns](std::uint64_t& value) noexcept { value = lost_ns; });
        window_clock = window_end;
        start = end;
    }
    control.phase.phase.store_release(Phase::Latency);
    auto const after = bench::detail::read_pause_after(pause_files);
    window.pause = bench::detail::pause_between(before, after);
    return window;
}

template <OpKind Kind>
[[nodiscard]] Window run_window(RowHome& home, LineIndex line, Seat const& seat, bench::CpuId cpu) noexcept {
    return run_window_loop<Kind>(
        home.state, [&home, line, &seat] [[gnu::always_inline]] () noexcept { run_op<Kind>(home, line, seat); }, cpu);
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
    // pause check (the windows of bench/bench_harness.h).
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
    if constexpr (Kind == OpKind::MpscPermissionedPush) return ::fixy::TextView{"mpsc permissioned push"};
    if constexpr (Kind == OpKind::LanesPush) return ::fixy::TextView{"lanes push"};
    if constexpr (Kind == OpKind::LanesPermissionedPush) return ::fixy::TextView{"lanes permissioned push"};
    if constexpr (Kind == OpKind::MpscSlowPush) return ::fixy::TextView{"mpsc slow push"};
    if constexpr (Kind == OpKind::LanesSlowPush) return ::fixy::TextView{"lanes slow push"};
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

// The workers of one row, one std::jthread for each slot from slot 1 on (the
// raw workers).  A worker pins itself to the CPU of its slot before it runs,
// and keeps the pin for its life, so it restores no affinity.  The join waits
// for each worker, and the end of the row calls it after the stop.
class RawWorkers {
public:
    template <OpKind Kind>
    void start(RowHome& home, bench::Spread const& spread, ThreadCount threads, ConsumerPlace place) {
        std::uint32_t const consumer_slot = kHasConsumer<Kind> ? threads.raw() - 1 : threads.raw();
        for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
            Role const role = slot == consumer_slot ? Role::Consumer : Role::Producer;
            LineIndex const line_index = LineIndex::check(slot).expect("a slot of a row is below kMaxThreads");
            bench::CpuId const cpu = cpu_of_slot(spread, bench::ThreadSlot{slot}, role, place);
            Seat const seat = role == Role::Producer ? seat_of<Kind>(bench::ThreadSlot{slot}, threads) : Seat{};
            // Each worker writes only the line of its own slot, through the frames of its loop.
            threads_[slot] = std::jthread{[&home, line_index, role, cpu, seat]() noexcept {
                bench::pin_calling_thread_to(cpu).expect("the pin of a worker to the CPU of its slot");
                if (role == Role::Consumer) {
                    run_consumer<Kind>(home, line_index);
                } else {
                    run_producer<Kind>(home, line_index, seat);
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

// The window and the latency of the main thread, and the result of the row.
// It stops the row at its end, so each worker ends its loop.  The caller then
// joins the workers.  The main thread runs its op while it waits for the
// counts of a worker.  A producer worker publishes its counts only after its
// last batch of kOpsPerCheck ops.  In a lanes op, that batch can wait for room
// until the consumer takes the next item of the lane of the main thread (the
// stall of fixy/concurrent/LaneRing.h), so a main thread that waits with no
// push holds the row for ever.
template <OpKind Kind, class Op>
[[nodiscard]] RowResult finish_row(RowControl& control, ThreadCount threads, ConsumerPlace place, bench::CpuId main_cpu,
                                   Op const& op) {
    Window const window = run_window_loop<Kind>(control, op, main_cpu);
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops = window.ops;
    for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
        ThreadLine const& line = line_of(control, bench::ThreadSlot{slot});
        while (!line.is_published.load_acquire())
            op();
        for (WindowIndex const index : ops.indices()) {
            std::uint64_t const added = line.window_ops.at(index).load_acquire();
            ops.with_element(index, [added](std::uint64_t& count) noexcept { count += added; });
        }
    }

    std::uint64_t all_ops = 0;
    std::uint64_t all_wall_ns = 0;
    ::fixy::FixedArray<double, bench::kWindows> window_mops{};
    for (WindowIndex const index : ops.indices()) {
        std::uint64_t const wall_ns = window.wall_ns.at(index);
        all_ops += ops.at(index);
        all_wall_ns += wall_ns;
        double const mops =
            wall_ns == 0 ? 0.0 : static_cast<double>(ops.at(index)) * 1000.0 / static_cast<double>(wall_ns);
        window_mops.with_element(index, [mops](double& value) noexcept { value = mops; });
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
                         .measure([&op] { op(); });
    if (threads.raw() <= 16) bench::print_thread_cpus("the row");
    control.phase.phase.store_release(Phase::Stop);
    return result;
}

// The end of each lane of a lanes op past the producers of the row, so the
// turn of the consumer holds only the lanes of the producers (the lanes).  The
// main thread runs it before the workers start.  Complexity: linear in the
// lanes.
template <OpKind Kind>
void end_lanes_past_the_producers(RowState& state, ThreadCount threads) noexcept {
    if constexpr (kIsLanes<Kind>) {
        std::uint32_t const producers = threads.raw() - 1;
        for (BenchLaneIndex const lane : ::fixy::IndexRange<static_cast<std::size_t>(kBenchLanes)>{}) {
            if (lane.raw() >= producers) state.lanes.end_lane(LanesOwner::lane(lane));
        }
    }
}

// Runs one row of an op on the first threads.count slots of the spread.  The
// state of a row holds each ring, more than half a megabyte, so it lives on the
// heap and not on the stack of the main thread.
template <OpKind Kind>
[[nodiscard]] RowResult run_row(bench::Spread const& spread, ThreadCount threads, ConsumerPlace place) {
    ::fixy::Box<RowHome> boxed = ::fixy::mint_box<RowHome>(eff::testing::test().alloc);
    RowHome& home = *boxed;
    RowState& state = home.state;
    for (::fixy::Index<kBatch> const index : state.batch.indices())
        state.batch.with_element(index, [index](Item& item) noexcept { item = Item{index.raw() + 1}; });
    end_lanes_past_the_producers<Kind>(state, threads);
    RawWorkers workers{};
    workers.start<Kind>(home, spread, threads, place);
    while (state.ready.value.load_acquire() + 1 < threads.raw())
        CRUCIBLE_SPIN_PAUSE;

    Seat const own_seat = seat_of<Kind>(bench::kMeasuringSlot, threads);
    bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).expect("a row has the slot of the main thread");
    RowResult result =
        finish_row<Kind>(state, threads, place, main_cpu, [&home, &own_seat] [[gnu::always_inline]] () noexcept {
            run_op<Kind>(home, ::fixy::index<bench::kMeasuringSlot.raw()>(), own_seat);
        });
    workers.join();
    return result;
}

// The tag of the root of the channel of a permissioned row.  Each row mints
// its root in its body, so the brand of the root stays in the scope that made
// it.
struct PermissionedRowTag final {};

// The form of the channel of a permissioned op.
template <OpKind Kind>
inline constexpr ::fixy::concurrent::ChannelForm kRowForm =
    Kind == OpKind::LanesPermissionedPush ? ::fixy::concurrent::ChannelForm::lanes
                                          : ::fixy::concurrent::ChannelForm::merge;

// The channel of a permissioned row of Threads threads and a root of type
// Root: a merge channel of kRingCapacity cells, or a lanes channel of one lane
// of kLaneCapacity cells for each producer.
template <OpKind Kind, std::uint32_t Threads, class Root>
struct RowChannel {
    using type = ::fixy::concurrent::channel_t<Item, kRingCapacity, Root>;
};
template <std::uint32_t Threads, class Root>
struct RowChannel<OpKind::LanesPermissionedPush, Threads, Root> {
    using type = ::fixy::concurrent::channel_t<Item, kLaneCapacity, Root, ::fixy::concurrent::LaneCount{Threads - 1}>;
};

// Opens the producer frame of the slot Slot of a permissioned row, and runs
// the body on its handle: a share that the pool of a merge lends, or the lane
// of the slot of a lanes channel.  The caller writes the body at the call, and
// the frame door owns it.  No exclusive is out in the bench, so an empty lend
// ends the process.  The handle ends at the end of the frame: a share goes
// back to the pool, and the end of a lane ends the lane.
template <OpKind Kind, std::size_t Slot, class Channel, class Body>
void with_slot_producer(Channel& channel, Body&& body) noexcept {
    eff::cap::Nondet const nondet = eff::testing::test().nondet;
    if constexpr (Kind == OpKind::LanesPermissionedPush) {
        auto lent = ::fixy::concurrent::with_producer(
            channel, nondet, typename Channel::lane_index{::fixy::index<Slot>()}, std::forward<Body>(body));
        static_cast<void>(std::move(lent).expect("the channel lends the lane of a slot while no exclusive is out"));
    } else {
        auto lent = ::fixy::concurrent::with_producer(channel, nondet, std::forward<Body>(body));
        static_cast<void>(std::move(lent).expect("the channel lends a producer while no exclusive is out"));
    }
}

// The options of the worker of a slot of a permissioned row: its name and the
// CPU of the slot.
[[nodiscard]] ::fixy::thread::ThreadOptions beside_options(bench::Spread const& spread,
                                                           bench::ThreadSlot slot) noexcept {
    return ::fixy::thread::ThreadOptions{
        .name = "scaling-beside",
        .affinity = ::fixy::AffinityMask::single(
            static_cast<std::uint16_t>(spread.of_slot(slot).expect("a row has at most the slots of the spread").raw())),
    };
}

// Runs the measure of the main thread with one worker for each body, on the
// slots from slot on, in the order of the bodies (the permissioned rows).  The
// continuation of each run_beside starts the next worker, so the measure runs
// in the continuation of the last one, and each worker joins when the call
// returns.
template <class Measure>
[[nodiscard]] RowResult run_beside_workers(bench::Spread const& /*spread*/, bench::ThreadSlot /*slot*/,
                                           Measure& measure) noexcept {
    return measure();
}

template <class Measure, class Body, class... Rest>
[[nodiscard]] RowResult run_beside_workers(bench::Spread const& spread, bench::ThreadSlot slot, Measure& measure,
                                           Body body, Rest... rest) noexcept {
    bench::ThreadSlot const next{slot.raw() + 1};
    auto values = ::fixy::thread::run_beside(Starter{eff::testing::bg()}, beside_options(spread, slot), std::move(body),
                                             [&spread, next, &measure, ... moved = std::move(rest)]() mutable noexcept {
                                                 return run_beside_workers(spread, next, measure, std::move(moved)...);
                                             })
                      .expect("the thread door starts a worker of a row");
    return std::move(values.at(::foundation::core::index<1>()));
}

// The body of the producer worker of the slot Slot of a permissioned row: it
// opens the frame of its end on its own stack and pushes into it until the
// row stops.  The lend comes before the worker counts itself ready, so it is
// outside the windows.  The end goes back to the pool of the channel at the
// end of its frame, and the end of a lane ends the lane.
template <OpKind Kind, std::size_t Slot, class Channel>
[[nodiscard]] auto make_beside_pusher(RowControl& control, Channel& channel) noexcept {
    return [&control, &channel]() noexcept {
        with_slot_producer<Kind, Slot>(channel, [&control](auto end) noexcept {
            // Each worker writes only the line of its own slot, through the frame of its loop.
            run_producer_loop<Kind>(
                control,
                [&control, &end] [[gnu::always_inline]] () noexcept {
                    push_waiting(control, [&end]() noexcept { return end.try_push(Item{1}); });
                },
                LineIndex{::fixy::index<Slot>()});
        });
    };
}

// One pop of the consumer end of a permissioned row: the pop of a merge takes
// the tag of Nondet, and the pop of a lanes channel, which is in the order of
// its lanes, takes no tag.  It adds the item that it took into sum.
template <OpKind Kind, class Consumer>
[[gnu::always_inline]] inline bool pop_permissioned(Consumer& from, std::uint64_t& sum) noexcept {
    if constexpr (Kind == OpKind::LanesPermissionedPush) {
        ::fixy::Option<Item> item = from.try_pop();
        bool const has_item = item.is_some();
        sum += sum_of(std::move(item));
        return has_item;
    } else {
        ::fixy::Option<Item> item = from.try_pop(eff::testing::test().nondet);
        bool const has_item = item.is_some();
        sum += sum_of(std::move(item));
        return has_item;
    }
}

// The body of the consumer worker of the last slot of a permissioned row: it
// moves its end onto its own stack and pops until the row stops.
template <OpKind Kind, std::uint32_t Threads, class Consumer>
[[nodiscard]] auto make_beside_popper(RowControl& control, Consumer&& consumer) noexcept {
    return [&control, owned = std::move(consumer)]() mutable noexcept {
        auto end = std::move(owned);
        // The consumer writes only the line of its own slot, and each field of a line is an Atomic.
        // The loop borrows the end as its source, so no body holds the end, and the end drops after the loop.
        control.lines.with_element(::fixy::index<Threads - 1>(),
                                   [](ThreadLine& line) noexcept { line.is_published.store_release(true); });
        std::uint64_t const item_sum = run_consumer_loop(
            control,
            [] [[gnu::always_inline]] (auto& from, ::fixy::View<Item> /*out*/, std::uint64_t& sum) noexcept {
                return pop_permissioned<Kind>(from, sum);
            },
            end, ::fixy::View<Item>{});
        control.lines.with_element(::fixy::index<Threads - 1>(),
                                   [item_sum](ThreadLine& line) noexcept { line.cell.store_release(item_sum); });
        ::foundation::linear::drop(std::move(end));
    };
}

// One row of a permissioned channel with Threads threads: the main thread and
// the workers of slots 1 to Threads - 2 push, each through a producer handle
// that the pool lends, and the worker of the last slot pops through the
// consumer handle.  The producer of slot k of a lanes channel pushes into
// lane k.  Each worker owns its end in its own frame, so no end shares a line
// with an object that the main thread writes.  The channel of a row lives in
// the frame of the row, and the row is not inline, so the stack holds the
// channel of one row at a time and not the channels of each count of threads.
//
// The frame of the producer of the main thread opens first, and the consumer
// frame opens in its body, so each end of the main thread lives until each
// worker has joined.  The workers start and join in the inner body.  A report
// owns its samples, so the row is no result of a frame: the inner body moves
// it into result.
template <OpKind Kind, std::uint32_t Threads, std::size_t... Producers>
[[nodiscard, gnu::noinline]] RowResult run_permissioned_row(bench::Spread const& spread,
                                                            std::index_sequence<Producers...> /*pushers*/) {
    auto root =
        perm::mint_permission_root<::fixy::concurrent::channel_tag::Whole<kRowForm<Kind>, PermissionedRowTag>>();
    using Channel = typename RowChannel<Kind, Threads, decltype(root)>::type;
    RowControl control{};
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<typename Channel::producer_tag, typename Channel::consumer_tag>(std::move(root));
    Channel channel{std::move(producer_root)};
    bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).expect("a row has the slot of the main thread");
    RowResult result{};
    with_slot_producer<Kind, 0>(channel, [&control, &spread, &channel, &result, main_cpu,
                                          consumer_perm = std::move(consumer_perm)](auto producer) mutable noexcept {
        ::fixy::concurrent::with_consumer(
            channel, std::move(consumer_perm),
            [&control, &spread, &channel, &result, &producer, main_cpu](auto consumer) noexcept {
                auto push = [&control, &producer] [[gnu::always_inline]] () noexcept {
                    push_waiting(control, [&producer]() noexcept { return producer.try_push(Item{1}); });
                };
                auto measure = [&control, &spread, &push, main_cpu]() noexcept {
                    while (control.ready.value.load_acquire() + 1 < Threads)
                        CRUCIBLE_SPIN_PAUSE;
                    return finish_row<Kind>(control, ThreadCount{Threads}, ConsumerPlace::LastSlot, main_cpu, push);
                };
                result = run_beside_workers(spread, bench::ThreadSlot{1}, measure,
                                            make_beside_pusher<Kind, Producers + 1>(control, channel)...,
                                            make_beside_popper<Kind, Threads>(control, std::move(consumer)));
            });
    });
    return result;
}

// The permissioned row of a count of threads, or no row for a count that the
// bench builds no row of.  The workers of a row hold the ends of the channel
// in the bodies of run_beside, so a count of threads is a count of bodies, and
// the rows take the counts 2, 4, 8 and 16.
template <OpKind Kind>
[[nodiscard]] ::fixy::Option<RowResult> run_permissioned_row_of(bench::Spread const& spread,
                                                                ThreadCount threads) noexcept {
    if (threads.raw() > spread.count()) return ::fixy::none;
    switch (threads.raw()) {
        case 2:
            return ::fixy::some(run_permissioned_row<Kind, 2>(spread, std::make_index_sequence<0>{}));
        case 4:
            return ::fixy::some(run_permissioned_row<Kind, 4>(spread, std::make_index_sequence<2>{}));
        case 8:
            return ::fixy::some(run_permissioned_row<Kind, 8>(spread, std::make_index_sequence<6>{}));
        case 16:
            return ::fixy::some(run_permissioned_row<Kind, 16>(spread, std::make_index_sequence<14>{}));
        default:
            return ::fixy::none;
    }
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
    bench::print("  {} threads{}: {}.{}{} Mops/s, efficiency {}.{}{}", row.threads.raw(), where, row.centi_mops / 100,
                 row.centi_mops % 100 / 10, row.centi_mops % 10, efficiency / 100, efficiency % 100 / 10,
                 efficiency % 10);
    bench::print(", {} windows: median {}.{}{} Mops/s, spread {}.{}%", row.windows.windows, median / 100,
                 median % 100 / 10, median % 10, spread / 10, spread % 10);
    bench::print(", window pause {} us, other thread delay {} us, {} paused windows{}\n",
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
    bench::print("\n=== {} ===\n", title);
    ::fixy::FixedList<RowResult, kMaxRows> rows{};
    if constexpr (kIsSpsc<Kind>) {
        if (spread.count() < 2) return;
        static_cast<void>(rows.append(run_row<Kind>(spread, ThreadCount{2}, ConsumerPlace::LastSlot))
                              .expect("an op has at most kMaxRows rows"));
        if (other_l3_cpu_of(spread).is_some()) {
            static_cast<void>(rows.append(run_row<Kind>(spread, ThreadCount{2}, ConsumerPlace::OtherL3))
                                  .expect("an op has at most kMaxRows rows"));
        }
    } else if constexpr (Kind == OpKind::MpscPermissionedPush || Kind == OpKind::LanesPermissionedPush) {
        for (ThreadCount const threads : counts) {
            ::fixy::Option<RowResult> row = run_permissioned_row_of<Kind>(spread, threads);
            if (row.is_none()) continue;
            static_cast<void>(rows.append(std::move(row).expect("the test above found a row"))
                                  .expect("an op has at most kMaxRows rows"));
        }
    } else {
        for (ThreadCount const threads : counts) {
            // A row of an op with a consumer needs a producer beside its consumer.
            if (producers_of<Kind>(threads) == 0) continue;
            // A lanes op has a lane for each producer (the lanes), and a slow
            // op has a slow producer beside the main thread (the slow producer).
            if (kIsLanes<Kind> && producers_of<Kind>(threads) > static_cast<std::uint64_t>(kBenchLanes)) continue;
            if (kHasSlowProducer<Kind> && producers_of<Kind>(threads) < 2) continue;
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

// The groups of ops of a run (the groups of ops).
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
            groups.with_element(
                ::fixy::Index<3>::check(static_cast<std::size_t>(group)).expect("a group is below three"),
                [](bool& is_set) noexcept { is_set = true; });
            is_named = true;
        }
    }
    if (!is_named) {
        groups.with_mut([](auto run) noexcept {
            for (bool& group : run)
                group = true;
        });
    }
    return groups;
}

// Reads the counts of the rows (the counts of threads).  An argument that
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

// The bench with the arguments of main (the counts of threads).
int run_bench(::fixy::os::Args const& arguments) {
    bench::print_system_info();
    bench::elevate_priority();
    bench::Spread const spread = bench::Spread::of_calling_thread();
    ::fixy::FixedList<ThreadCount, kMaxRows> const counts = counts_of(arguments, spread);
    if (spread.source() == bench::SpreadSource::Claim) {
        bench::print("  slots: the {} claimed cores, slot k on core k, from cpu{}\n", spread.count(),
                     spread.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none()).raw());
    } else {
        bench::print("  NOTE: no claimed core.  The {} slots take one CPU of each core of the affinity, then the "
                     "SMT siblings, and other work can use them, so each row is noisy\n",
                     spread.count());
    }
    bench::print("  the consumer in another L3 group: cpu{}\n",
                 other_l3_cpu_of(spread).value_or(bench::CpuId::none()).raw());
    for (ThreadCount const threads : counts) {
        if (threads.raw() > bench::kPauseThreads) {
            bench::print("  NOTE: a row of {} threads has more threads than the {} that the pause check reads\n",
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
        run_op_rows<OpKind::MpscPermissionedPush>(
            spread, counts.view(), "mpsc permissioned push: the merge channel, each end in the body of its worker",
            reports);
        run_op_rows<OpKind::MpscBatchPush>(spread, counts.view(),
                                           "mpsc batch push: batches of 16, and batch pops of up to 64", reports);
        if (kHasMpscReference) {
            run_op_rows<OpKind::MpscReferencePush>(
                spread, counts.view(), "mpsc reference push: the pinned MPMCQueue of Erik Rigtorp", reports);
        }
        run_op_rows<OpKind::LanesPush>(spread, counts.view(),
                                       "lanes push: one SPSC lane for each producer, read in the order of the lanes",
                                       reports);
        run_op_rows<OpKind::LanesPermissionedPush>(
            spread, counts.view(), "lanes permissioned push: the lanes channel, each end in the body of its worker",
            reports);
        run_op_rows<OpKind::MpscSlowPush>(spread, counts.view(), "mpsc slow push: the mpsc push with one slow producer",
                                          reports);
        run_op_rows<OpKind::LanesSlowPush>(spread, counts.view(),
                                           "lanes slow push: the lanes push with one slow producer", reports);
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
