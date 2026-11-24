#include <crucible/perf/SenseHub.h>

#include <cstdint>
#include <type_traits>
#include <utility>
#include "perf_capabilities.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

// These ordinals must match the enum the BPF program compiles against.
// Drift between the two sides silently mis-decodes every counter.

static_assert(crucible::perf::NET_TCP_ESTABLISHED == 0, "Idx 0 anchors the Network State cache line; cannot move");
static_assert(crucible::perf::SCHED_CTX_INVOL == 21, "SCHED_CTX_INVOL is the canonical 'preempt' counter — bench "
                                                     "harness reads slot 21 by name; ABI-stable");
static_assert(crucible::perf::FUTEX_WAIT_NS == 26, "FUTEX_WAIT_NS is read by the scheduler wiring; ABI-stable");
static_assert(crucible::perf::NUM_COUNTERS == 96, "Snapshot is committed at 96 u64 = 768 B = 12 cache lines; "
                                                  "growing this number breaks the mmap wire contract with the "
                                                  "BPF_F_MMAPABLE array map");

// The header asserts this too.  Repeating it means a break in the wire
// contract fails the test loudly.
static_assert(sizeof(crucible::perf::Snapshot) == crucible::perf::NUM_COUNTERS * sizeof(uint64_t));

static_assert(std::is_trivially_copyable_v<crucible::perf::Snapshot>,
              "Snapshot must memcpy cleanly so the bench harness can stash "
              "pre/post deltas into POD slots without a real copy ctor");

static_assert(!std::is_copy_constructible_v<crucible::perf::SenseHub>,
              "SenseHub owns a unique BPF object + mmap; copying would "
              "double-close — the deleted copy ctor is load-bearing");

static_assert(std::is_move_constructible_v<crucible::perf::SenseHub>,
              "SenseHub must be movable so the process-wide singleton can "
              "be constructed-then-emplaced into std::optional");

// The hub holds one opaque pointer to its state and nothing else.
// Everything it owns lives behind that pointer, off its own footprint.
// A field added without the no-unique-address attribute would inflate
// it, and every consumer holding one in static storage would quietly pay
// the cache footprint.
struct DummyState {};
static_assert(sizeof(crucible::perf::SenseHub) == sizeof(::fixy::Option<::fixy::Box<DummyState>>),
              "SenseHub must equal the size of an Option of a Box of its state.  A larger size "
              "means that a field was added");

}  // namespace

int main() {
    // Two empty snapshots subtract to zero in every slot, with no
    // underflow and no garbage.
    crucible::perf::Snapshot before{};
    crucible::perf::Snapshot after{};
    auto identity_delta = after - before;
    for (::fixy::Index<crucible::perf::NUM_COUNTERS> const i : ::fixy::IndexRange<crucible::perf::NUM_COUNTERS>{}) {
        if (identity_delta.counters.at(i) != 0u) {
            ::fixy::report(::fixy::Sink::Err,
                           "perf::Snapshot::operator- — empty-empty delta non-zero "
                           "at slot {}\n",
                           i.raw());
            return 1;
        }
    }

    // A gauge slot can fall between two reads, leaving the older value
    // above the newer one.  The subtraction saturates to zero there
    // rather than wrapping.
    crucible::perf::Snapshot older{};
    crucible::perf::Snapshot newer{};
    older.counters.with_element(::fixy::index<crucible::perf::FD_CURRENT>(),
                                [](uint64_t& counter) noexcept { counter = 1024; });
    newer.counters.with_element(::fixy::index<crucible::perf::FD_CURRENT>(),
                                [](uint64_t& counter) noexcept { counter = 7; });  // closed FDs
    auto gauge_delta = newer - older;
    if (gauge_delta[crucible::perf::FD_CURRENT] != 0u) {
        ::fixy::report(::fixy::Sink::Err,
                       "perf::Snapshot::operator- — gauge underflow not "
                       "saturated to zero: got {}\n",
                       gauge_delta[crucible::perf::FD_CURRENT]);
        return 1;
    }

    // Most counters only rise, being kernel-side fetch-and-add, so the
    // subtraction must give back the exact difference without
    // saturating.  A refactor that swapped the operands or saturated in
    // both directions breaks here.
    crucible::perf::Snapshot run_pre{};
    crucible::perf::Snapshot run_post{};
    run_pre.counters.with_element(::fixy::index<crucible::perf::SCHED_CTX_INVOL>(),
                                  [](uint64_t& counter) noexcept { counter = 1000; });
    run_post.counters.with_element(::fixy::index<crucible::perf::SCHED_CTX_INVOL>(),
                                   [](uint64_t& counter) noexcept { counter = 1042; });
    run_pre.counters.with_element(::fixy::index<crucible::perf::NET_TX_BYTES>(),
                                  [](uint64_t& counter) noexcept { counter = 8000000; });
    run_post.counters.with_element(::fixy::index<crucible::perf::NET_TX_BYTES>(),
                                   [](uint64_t& counter) noexcept { counter = 8000064; });
    auto run_delta = run_post - run_pre;
    if (run_delta[crucible::perf::SCHED_CTX_INVOL] != 42u || run_delta[crucible::perf::NET_TX_BYTES] != 64u) {
        ::fixy::report(::fixy::Sink::Err,
                       "perf::Snapshot::operator- — monotonic forward delta wrong: "
                       "SCHED_CTX_INVOL got {} (want 42), NET_TX_BYTES got {} "
                       "(want 64)\n",
                       run_delta[crucible::perf::SCHED_CTX_INVOL], run_delta[crucible::perf::NET_TX_BYTES]);
        return 1;
    }

    // Without BPF support the loader symbol is genuinely absent, and
    // even an unreached call would fail to link, so the whole block is
    // compiled out.
    //
    // Whether the load succeeds depends on the process capabilities and
    // on the kernel's tracepoints, so only the link and the return type
    // are asserted.  The CMake list sets CRUCIBLE_PERF_QUIET=1, because a
    // failed load is the common case in a restricted sandbox and its
    // diagnostic would be misleading here.
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    // The loader takes the startup load context, and main is a startup
    // frame.  The call site is also the check that the gated signature
    // compiles from an ordinary startup frame.
    ::fixy::Option<crucible::perf::SenseHub> loaded =
        crucible::perf::load_sense_hub(::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    static_assert(std::is_same_v<decltype(loaded), ::fixy::Option<crucible::perf::SenseHub>>);
    // A process with the BPF capabilities must load.  A process without them
    // skips.  A populated hub also carries counters that must agree with
    // each other.
    if (loaded.is_none()) return crucible::test::verdict_of_no_load("perf::load_sense_hub()");
    {
        crucible::perf::SenseHub hub = std::move(loaded).expect("the test above found the hub");
        // Both counters come back refined against an upper bound that
        // the implementation enforces at construction.  Pinning the
        // wrapper type first catches a later relaxation of that bound.
        const auto attached_refined = hub.attached_programs();
        const auto failures_refined = hub.attach_failures();
        static_assert(
            std::is_same_v<decltype(attached_refined), const ::fixy::Refined<::fixy::bounded_above<64>, std::size_t>>);
        static_assert(
            std::is_same_v<decltype(failures_refined), const ::fixy::Refined<::fixy::bounded_above<64>, std::size_t>>);
        const std::size_t attached = attached_refined.value();
        const std::size_t failures = failures_refined.value();
        // A hub with no programs at all is rejected by the loader, so a
        // populated one cannot report zero of both.
        if (attached == 0 && failures == 0) {
            ::fixy::report(::fixy::Sink::Err, "perf::load_sense_hub() returned populated hub with "
                                              "zero programs — should have been none\n");
            return 1;
        }
        // The refinement enforces this at construction under enforced
        // contracts.  Checking it again here catches a bound that is
        // relaxed without the consumer being updated.
        if (attached > 64 || failures > 64) {
            ::fixy::report(::fixy::Sink::Err,
                           "perf::SenseHub: counter exceeded inplace_vector cap "
                           "(attached={} failures={})\n",
                           attached, failures);
            return 1;
        }
        // A successful load leaves the mapping live, so the view
        // borrows the whole counter array.
        const auto view = hub.counters_view(::foundation::effects::testing::test().nondet);
        static_assert(
            std::is_same_v<
                decltype(view),
                const ::fixy::Option<::fixy::mmap::SharedView<std::uint64_t const, crucible::perf::NUM_COUNTERS>>>);
        // The words span exactly the whole counter array, and the type holds
        // the count.  A wrongly sized map would have been rejected by the
        // verifier.
        static_assert(::fixy::mmap::SharedView<std::uint64_t const, crucible::perf::NUM_COUNTERS>::extent
                      == crucible::perf::NUM_COUNTERS);
        if (view.is_none()) {
            ::fixy::report(::fixy::Sink::Err, "perf::SenseHub::counters_view() — the words should be "
                                              "present when the load gave a hub\n");
            return 1;
        }

        // Every public accessor returns a documented empty value when
        // the state pointer is null, which makes a moved-from hub safe
        // to query.  That matters because client code can outlive the
        // Option that held the hub.  This block drives the null branch
        // of each accessor, so a move that breaks the guard fails here
        // rather than dereferencing null in production.
        crucible::perf::SenseHub moved_into = std::move(hub);
        // The source now holds the moved-from state.
        const auto zeroed_snapshot = hub.read(::foundation::effects::testing::test().nondet);
        const auto empty_view = hub.counters_view(::foundation::effects::testing::test().nondet);
        const auto attached_after = hub.attached_programs();
        const auto failures_after = hub.attach_failures();

        for (::fixy::Index<crucible::perf::NUM_COUNTERS> const i : ::fixy::IndexRange<crucible::perf::NUM_COUNTERS>{}) {
            if (zeroed_snapshot.counters.at(i) != 0u) {
                ::fixy::report(::fixy::Sink::Err,
                               "perf::SenseHub::read() on moved-from — slot {} "
                               "should be 0, got {}\n",
                               i.raw(), zeroed_snapshot.counters.at(i));
                return 1;
            }
        }
        if (empty_view.is_some()) {
            ::fixy::report(::fixy::Sink::Err, "perf::SenseHub::counters_view() on moved-from — "
                                              "expected no words\n");
            return 1;
        }
        if (attached_after.value() != 0u || failures_after.value() != 0u) {
            ::fixy::report(::fixy::Sink::Err,
                           "perf::SenseHub::attached_programs/attach_failures "
                           "on moved-from — expected 0, got attached={} "
                           "failures={}\n",
                           attached_after.value(), failures_after.value());
            return 1;
        }
        // The symmetric witness: the recipient still holds the live
        // hub.
        if (moved_into.attached_programs().value() != attached) {
            ::fixy::report(::fixy::Sink::Err,
                           "perf::SenseHub move semantics — recipient lost "
                           "attached count (was {}, now {})\n",
                           attached, moved_into.attached_programs().value());
            return 1;
        }
    }
#endif

    ::fixy::check::pass(kTest, "perf::SenseHub smoke OK\n");
    return 0;
}
