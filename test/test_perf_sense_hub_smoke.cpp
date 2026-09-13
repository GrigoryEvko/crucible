#include <crucible/perf/SenseHub.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

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
static_assert(sizeof(crucible::perf::SenseHub) == sizeof(std::unique_ptr<DummyState>),
              "SenseHub must equal sizeof(std::unique_ptr<State>); a regression "
              "indicates a non-EBO field was added without [[no_unique_address]] "
              "(or a polymorphic vptr crept in via a virtual function)");

}  // namespace

int main() {
    // Two empty snapshots subtract to zero in every slot, with no
    // underflow and no garbage.
    crucible::perf::Snapshot before;
    crucible::perf::Snapshot after;
    auto identity_delta = after - before;
    for (uint32_t i = 0; i < crucible::perf::NUM_COUNTERS; ++i) {
        if (identity_delta.counters[i] != 0u) {
            std::fprintf(stderr,
                         "perf::Snapshot::operator- — empty-empty delta non-zero "
                         "at slot %u\n",
                         i);
            return 1;
        }
    }

    // A gauge slot can fall between two reads, leaving the older value
    // above the newer one.  The subtraction saturates to zero there
    // rather than wrapping.
    crucible::perf::Snapshot older;
    crucible::perf::Snapshot newer;
    older.counters[crucible::perf::FD_CURRENT] = 1024;
    newer.counters[crucible::perf::FD_CURRENT] = 7;  // closed FDs
    auto gauge_delta = newer - older;
    if (gauge_delta.counters[crucible::perf::FD_CURRENT] != 0u) {
        std::fprintf(stderr,
                     "perf::Snapshot::operator- — gauge underflow not "
                     "saturated to zero: got %llu\n",
                     static_cast<unsigned long long>(gauge_delta.counters[crucible::perf::FD_CURRENT]));
        return 1;
    }

    // Most counters only rise, being kernel-side fetch-and-add, so the
    // subtraction must give back the exact difference without
    // saturating.  A refactor that swapped the operands or saturated in
    // both directions breaks here.
    crucible::perf::Snapshot run_pre;
    crucible::perf::Snapshot run_post;
    run_pre.counters[crucible::perf::SCHED_CTX_INVOL] = 1000;
    run_post.counters[crucible::perf::SCHED_CTX_INVOL] = 1042;
    run_pre.counters[crucible::perf::NET_TX_BYTES] = 8'000'000;
    run_post.counters[crucible::perf::NET_TX_BYTES] = 8'000'064;
    auto run_delta = run_post - run_pre;
    if (run_delta.counters[crucible::perf::SCHED_CTX_INVOL] != 42u
        || run_delta.counters[crucible::perf::NET_TX_BYTES] != 64u) {
        std::fprintf(stderr,
                     "perf::Snapshot::operator- — monotonic forward delta wrong: "
                     "SCHED_CTX_INVOL got %llu (want 42), NET_TX_BYTES got %llu "
                     "(want 64)\n",
                     static_cast<unsigned long long>(run_delta.counters[crucible::perf::SCHED_CTX_INVOL]),
                     static_cast<unsigned long long>(run_delta.counters[crucible::perf::NET_TX_BYTES]));
        return 1;
    }

    // Without BPF support the loader symbol is genuinely absent, and
    // even an unreached call would fail to link, so the whole block is
    // compiled out.
    //
    // Whether the load succeeds depends on the process capabilities and
    // on the kernel's tracepoints, so only the link and the return type
    // are asserted.  The quiet flag is set first, because a failed load
    // is the common case in a restricted sandbox and its diagnostic
    // would be misleading here.
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    // The loader takes an init capability tag, and main is genuine init
    // context, so the tag costs nothing to produce here.  The call site
    // doubles as the check that the capability-gated signature still
    // compiles from an ordinary init frame.
    std::optional<crucible::perf::SenseHub> hub = crucible::perf::SenseHub::load(::crucible::effects::testing::init());
    static_assert(std::is_same_v<decltype(hub), std::optional<crucible::perf::SenseHub>>);
    // Either outcome is valid.  A populated hub also carries counters
    // that must agree with each other.
    if (hub.has_value()) {
        // Both counters come back refined against an upper bound that
        // the implementation enforces at construction.  Pinning the
        // wrapper type first catches a later relaxation of that bound.
        const auto attached_refined = hub->attached_programs();
        const auto failures_refined = hub->attach_failures();
        static_assert(
            std::is_same_v<decltype(attached_refined),
                           const crucible::safety::Refined<crucible::safety::bounded_above<64>, std::size_t>>);
        static_assert(
            std::is_same_v<decltype(failures_refined),
                           const crucible::safety::Refined<crucible::safety::bounded_above<64>, std::size_t>>);
        const std::size_t attached = attached_refined.value();
        const std::size_t failures = failures_refined.value();
        // A hub with no programs at all is rejected by the loader, so a
        // populated one cannot report zero of both.
        if (attached == 0 && failures == 0) {
            std::fprintf(stderr, "perf::SenseHub::load() returned populated hub with "
                                 "zero programs — should have been std::nullopt\n");
            return 1;
        }
        // The refinement enforces this at construction under enforced
        // contracts.  Checking it again here catches a bound that is
        // relaxed without the consumer being updated.
        if (attached > 64 || failures > 64) {
            std::fprintf(stderr,
                         "perf::SenseHub: counter exceeded inplace_vector cap "
                         "(attached=%zu failures=%zu)\n",
                         attached, failures);
            return 1;
        }
        // A successful load leaves the mapping live, so the view
        // borrows the whole counter array.
        const auto view = hub->counters_view();
        static_assert(
            std::is_same_v<decltype(view),
                           const crucible::safety::Borrowed<const volatile std::uint64_t, crucible::perf::SenseHub>>);
        // The view must span exactly the whole counter array.  A
        // mismatch means the wrapper lost the count, since a wrongly
        // sized map would have been rejected by the verifier.
        if (view.size() != crucible::perf::NUM_COUNTERS) {
            std::fprintf(stderr,
                         "perf::SenseHub::counters_view() — expected NUM_COUNTERS "
                         "(96) elements, got %zu\n",
                         view.size());
            return 1;
        }
        if (view.empty()) {
            std::fprintf(stderr, "perf::SenseHub::counters_view() — view should be "
                                 "non-empty when hub.has_value()\n");
            return 1;
        }

        // Every public accessor returns a documented empty value when
        // the state pointer is null, which makes a moved-from hub safe
        // to query.  That matters because client code can outlive the
        // optional that held the hub.  This block drives the null branch
        // of each accessor, so a move that breaks the guard fails here
        // rather than dereferencing null in production.
        crucible::perf::SenseHub moved_into = std::move(*hub);
        // The source now holds the moved-from state.
        const auto zeroed_snapshot = hub->read();
        const auto empty_view = hub->counters_view();
        const auto attached_after = hub->attached_programs();
        const auto failures_after = hub->attach_failures();

        for (uint32_t i = 0; i < crucible::perf::NUM_COUNTERS; ++i) {
            if (zeroed_snapshot.counters[i] != 0u) {
                std::fprintf(stderr,
                             "perf::SenseHub::read() on moved-from — slot %u "
                             "should be 0, got %llu\n",
                             i, static_cast<unsigned long long>(zeroed_snapshot.counters[i]));
                return 1;
            }
        }
        if (!empty_view.empty()) {
            std::fprintf(stderr,
                         "perf::SenseHub::counters_view() on moved-from — "
                         "expected empty Borrowed, got size=%zu\n",
                         empty_view.size());
            return 1;
        }
        if (attached_after.value() != 0u || failures_after.value() != 0u) {
            std::fprintf(stderr,
                         "perf::SenseHub::attached_programs/attach_failures "
                         "on moved-from — expected 0, got attached=%zu "
                         "failures=%zu\n",
                         attached_after.value(), failures_after.value());
            return 1;
        }
        // The symmetric witness: the recipient still holds the live
        // hub.
        if (moved_into.attached_programs().value() != attached) {
            std::fprintf(stderr,
                         "perf::SenseHub move semantics — recipient lost "
                         "attached count (was %zu, now %zu)\n",
                         attached, moved_into.attached_programs().value());
            return 1;
        }
    }
#endif

    std::printf("perf::SenseHub smoke OK\n");
    return 0;
}
