// Each read of a cell that another thread writes runs on each turn of a
// loop.  [[gnu::pure]] or [[gnu::const]] on such a read tells the compiler
// that the read changes nothing, and that its result depends only on its
// arguments and on memory that the loop does not write.  The compiler then
// reads one time for each loop, folds two reads into one, and removes a
// wait loop with an empty body.
//
// test/CMakeLists.txt compiles this unit with -O2 -fno-inline
// -finstrument-functions and no sanitizer in each preset.  Each read is
// then a call that the optimizer can move, and each function of the unit
// calls the entry hook below.  The hook counts the calls that run.  A loop
// of eight reads must count eight times the calls of one read.  A local
// pure read is the control.  It must count the same calls for one turn and
// for eight turns.  Else the flags of the unit do not hold, and the other
// checks prove nothing.
//
// The readers below are the reads of the cells of the base, and the read of
// the ring that Vigil::flush waits on.  The count cannot see an attribute on
// TraceRing::size, classify_static or classify_kernel.  A loop over each of
// them keeps the call on each turn, with the attribute too.

#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <fixy/handle/PublishCommit.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the entry hook of -finstrument-functions counts the calls that a loop runs")

namespace {

// Volatile, so that no read of a count folds into a constant, and no loop
// has a result that no code reads.
volatile std::uint64_t entries_seen = 0;
volatile std::uint64_t last_matches = 0;

}  // namespace

// The compiler declares the two hooks, with C linkage and no noexcept.
extern "C" [[gnu::no_instrument_function]] void __cyg_profile_func_enter(void* /*function*/, void* /*call_site*/) {
    entries_seen = entries_seen + 1;
}

extern "C" [[gnu::no_instrument_function]] void __cyg_profile_func_exit(void* /*function*/, void* /*call_site*/) {}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

struct ProbeTag final {};

// The one writer of the commit cell of the probe.  Its read of its own count
// is the relaxed read of the cell.
struct ProbeWriter final {
    template <class Cell>
    [[nodiscard, gnu::always_inline, gnu::no_instrument_function]] static std::uint64_t
    peek(Cell const& cell) noexcept {
        return cell.peek_relaxed();
    }
};

// The control: a read that says it is pure, of a plain value.
struct PureControl final {
    std::uint64_t value = 0;
    [[nodiscard, gnu::pure]] std::uint64_t read_pure() const noexcept { return value; }
};

// The calls that `turns` reads run.  Each reader below is a lambda with
// always_inline and no_instrument_function, so the loop of this function
// holds the direct call of the read and no hook.  The optimizer can then
// move a pure read out of the loop.
template <class Read, class Value>
[[nodiscard, gnu::noinline, gnu::no_instrument_function]] std::uint64_t calls_of_reads(Read const& read, Value expected,
                                                                                       int turns) noexcept {
    entries_seen = 0;
    std::uint64_t matches = 0;
    for (int turn = 0; turn < turns; ++turn) {
        if (read() == expected) ++matches;
    }
    last_matches = matches;
    return entries_seen;
}

// A read on each turn runs eight times the calls of one turn.  A read that
// runs no call proves nothing, so it fails too.
template <class Read, class Value>
[[nodiscard]] int failures_of(fixy::TextView name, Read const& read, Value expected) noexcept {
    const std::uint64_t one_turn = calls_of_reads(read, expected, 1);
    const std::uint64_t eight_turns = calls_of_reads(read, expected, 8);
    if (one_turn != 0 && eight_turns == 8 * one_turn) return 0;
    fixy::report(fixy::Sink::Err,
                 "{}: one turn runs {} calls and eight turns run {}, so the read is not on each turn\n", name, one_turn,
                 eight_turns);
    return 1;
}

// The pure control runs its call one time for each loop.
[[nodiscard]] int failures_of_control() noexcept {
    const PureControl control{};
    const auto read = [&control] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept {
        return control.read_pure();
    };
    const std::uint64_t one_turn = calls_of_reads(read, std::uint64_t{0}, 1);
    const std::uint64_t eight_turns = calls_of_reads(read, std::uint64_t{0}, 8);
    if (one_turn != 0 && eight_turns == one_turn) return 0;
    fixy::report(fixy::Sink::Err,
                 "the pure control runs {} calls for one turn and {} for eight turns.  The unit does not have the "
                 "flags -O2 -fno-inline -finstrument-functions, so no check here proves anything\n",
                 one_turn, eight_turns);
    return 1;
}

}  // namespace

int main() {
    int failures = failures_of_control();

    const fixy::handle::PublishCommitCell<ProbeTag, ProbeWriter> commit{};
    failures += failures_of(
        fixy::TextView{"PublishCommitCell::load_acquire"},
        [&commit] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept { return commit.load_acquire(); },
        0u);
    failures += failures_of(
        fixy::TextView{"PublishCommitCell::peek_relaxed"},
        [&commit] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept { return ProbeWriter::peek(commit); },
        0u);

    const fixy::AtomicMonotonic<std::uint64_t> monotonic = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    failures += failures_of(
        fixy::TextView{"AtomicMonotonic::get"},
        [&monotonic] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept { return monotonic.get(); }, 0u);
    failures += failures_of(
        fixy::TextView{"AtomicMonotonic::load_relaxed"},
        [&monotonic] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept {
            return monotonic.load_relaxed();
        },
        0u);

    const fixy::Atomic<std::uint64_t> atomic{0};
    failures += failures_of(
        fixy::TextView{"Atomic::load_acquire"},
        [&atomic] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept { return atomic.load_acquire(); },
        0u);

    const fixy::Tally tally{};
    failures += failures_of(
        fixy::TextView{"Tally::read"},
        [&tally] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept { return tally.read(); }, 0u);

    const fixy::concurrent::AtomicSnapshot<std::uint64_t> snapshot{};
    failures += failures_of(
        fixy::TextView{"AtomicSnapshot::load"},
        [&snapshot] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept { return snapshot.load(); }, 0u);

    // The rule reads the topology, which the first call probes one time.
    using fixy::concurrent::ParallelismRule;
    constexpr std::size_t kWorkingSet = 4096;
    failures += failures_of(
        fixy::TextView{"ParallelismRule::classify"},
        [] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept {
            return ParallelismRule::classify(kWorkingSet);
        },
        ParallelismRule::classify(kWorkingSet));
    failures += failures_of(
        fixy::TextView{"ParallelismRule::is_core_resident"},
        [] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept {
            return ParallelismRule::is_core_resident(kWorkingSet);
        },
        ParallelismRule::is_core_resident(kWorkingSet));

    // Vigil::flush waits for the background thread up to the count that this
    // read gives, and the foreground moves that count.
    const auto test = ::foundation::effects::testing::test();
    const ::fixy::Box<crucible::TraceRing> ring = ::fixy::mint_box<crucible::TraceRing>(test.alloc);
    const crucible::TraceRing& ring_view = *ring;
    failures += failures_of(
        fixy::TextView{"TraceRing::total_produced"},
        [&ring_view] [[gnu::always_inline, gnu::no_instrument_function]] () noexcept {
            return ring_view.total_produced();
        },
        std::uint64_t{0});
    return failures == 0 ? 0 : 1;
}
