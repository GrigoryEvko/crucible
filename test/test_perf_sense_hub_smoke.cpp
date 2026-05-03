// Sentinel TU for the promoted crucible::perf::SenseHub
// observability substrate (GAPS-004a, 2026-05-03).
//
// What this test asserts:
//   1. <crucible/perf/SenseHub.h> is reachable through the public
//      crucible include path (header is shipped, not bench-local).
//   2. The Snapshot type definitions (Idx enum + Snapshot struct +
//      saturating delta operator) link cleanly with no perf-runtime
//      symbols required.  No bpf_object*, no libbpf calls — pure
//      header-only types.
//   3. When CRUCIBLE_HAVE_BPF=1, SenseHub::load() is callable and
//      returns std::optional<SenseHub> (whether populated or not
//      depends on runtime kernel/cap config; the test only asserts
//      the call link-works and the optional has the documented
//      shape).  When CRUCIBLE_HAVE_BPF=0, we skip the load() call
//      entirely — the symbol is genuinely absent and even an
//      unreached call would fail at link time.
//   4. Idx enum has the documented 96-entry layout that the BPF
//      kernel side commits to.  Any drift between the header and
//      the .bpf.c sense_idx enum would silently mis-decode counters,
//      so we check at compile time that the well-known fence-post
//      ordinals haven't shifted.
//
// Methodology: raw main() + cstdio.  Same style as the rest of the
// test/ tree so we don't drag in gtest just for ~10 assertions.

#include <crucible/perf/SenseHub.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>   // setenv (POSIX); needed for CRUCIBLE_PERF_QUIET hint below
#include <optional>
#include <type_traits>

namespace {

// ── (4) Layout fence-posts — must match BPF program's enum sense_idx
//        in include/crucible/perf/bpf/sense_hub.bpf.c.

static_assert(crucible::perf::NET_TCP_ESTABLISHED == 0,
    "Idx 0 anchors the Network State cache line; cannot move");
static_assert(crucible::perf::SCHED_CTX_INVOL == 21,
    "SCHED_CTX_INVOL is the canonical 'preempt' counter — bench "
    "harness reads slot 21 by name; ABI-stable");
static_assert(crucible::perf::FUTEX_WAIT_NS == 26,
    "FUTEX_WAIT_NS is referenced by AdaptiveScheduler future "
    "wiring (GAPS-004h); ABI-stable");
static_assert(crucible::perf::NUM_COUNTERS == 96,
    "Snapshot is committed at 96 u64 = 768 B = 12 cache lines; "
    "growing this number breaks the mmap wire contract with the "
    "BPF_F_MMAPABLE array map");

// Snapshot tightness invariant duplicated from the header so the
// test fails loudly if anyone breaks the wire contract.
static_assert(sizeof(crucible::perf::Snapshot) ==
              crucible::perf::NUM_COUNTERS * sizeof(uint64_t));

// ── (1) + (2) Type-shape sanity — header is reachable + types compile.

static_assert(std::is_trivially_copyable_v<crucible::perf::Snapshot>,
    "Snapshot must memcpy cleanly so the bench harness can stash "
    "pre/post deltas into POD slots without a real copy ctor");

static_assert(!std::is_copy_constructible_v<crucible::perf::SenseHub>,
    "SenseHub owns a unique BPF object + mmap; copying would "
    "double-close — the deleted copy ctor is load-bearing");

static_assert(std::is_move_constructible_v<crucible::perf::SenseHub>,
    "SenseHub must be movable so the process-wide singleton can "
    "be constructed-then-emplaced into std::optional");

}  // namespace

int main() {
    // ── (2) Snapshot delta semantics — identity case.
    //
    // Two empty snapshots subtract to zero per slot — no underflow
    // reach, no garbage, exercises the saturating operator-.  Names
    // `before` / `after` follow CLAUDE.md §XVII identifier discipline
    // (no single-character variables outside loop induction).
    crucible::perf::Snapshot before;
    crucible::perf::Snapshot after;
    auto identity_delta = after - before;
    for (uint32_t i = 0; i < crucible::perf::NUM_COUNTERS; ++i) {
        if (identity_delta.counters[i] != 0u) {
            std::fprintf(stderr,
                "perf::Snapshot::operator- — empty-empty delta non-zero "
                "at slot %u\n", i);
            return 1;
        }
    }

    // ── (2) Saturation on gauge underflow.
    //
    // Real-world: a gauge slot (e.g. FD_CURRENT) drops between two
    // reads — older > newer.  Header documents that we saturate to
    // zero rather than wrap.  Set FD_CURRENT in `older` only and
    // confirm the delta saturates cleanly.
    crucible::perf::Snapshot older;
    crucible::perf::Snapshot newer;
    older.counters[crucible::perf::FD_CURRENT] = 1024;
    newer.counters[crucible::perf::FD_CURRENT] = 7;  // closed FDs
    auto gauge_delta = newer - older;
    if (gauge_delta.counters[crucible::perf::FD_CURRENT] != 0u) {
        std::fprintf(stderr,
            "perf::Snapshot::operator- — gauge underflow not "
            "saturated to zero: got %llu\n",
            static_cast<unsigned long long>(
                gauge_delta.counters[crucible::perf::FD_CURRENT]));
        return 1;
    }

    // ── (2) Monotonic-counter forward delta — the common case.
    //
    // Most counters (SCHED_CTX_INVOL, NET_TX_BYTES, MEM_PAGE_FAULTS_*)
    // are kernel-side fetch-and-add, monotonically increasing.
    // Reading newer - older must yield the additive delta exactly,
    // without saturating, for the bench harness to display correct
    // per-iteration counts.  Regression-witness: if a future
    // refactor accidentally swaps the operand order or applies
    // saturation in both directions, this test catches it.
    crucible::perf::Snapshot run_pre;
    crucible::perf::Snapshot run_post;
    run_pre.counters[crucible::perf::SCHED_CTX_INVOL]    = 1000;
    run_post.counters[crucible::perf::SCHED_CTX_INVOL]   = 1042;
    run_pre.counters[crucible::perf::NET_TX_BYTES]       = 8'000'000;
    run_post.counters[crucible::perf::NET_TX_BYTES]      = 8'000'064;
    auto run_delta = run_post - run_pre;
    if (run_delta.counters[crucible::perf::SCHED_CTX_INVOL] != 42u ||
        run_delta.counters[crucible::perf::NET_TX_BYTES]    != 64u) {
        std::fprintf(stderr,
            "perf::Snapshot::operator- — monotonic forward delta wrong: "
            "SCHED_CTX_INVOL got %llu (want 42), NET_TX_BYTES got %llu "
            "(want 64)\n",
            static_cast<unsigned long long>(
                run_delta.counters[crucible::perf::SCHED_CTX_INVOL]),
            static_cast<unsigned long long>(
                run_delta.counters[crucible::perf::NET_TX_BYTES]));
        return 1;
    }

    // ── (3) load() symbol is link-reachable when CRUCIBLE_HAVE_BPF=1.
    //
    // The actual return value depends on runtime caps (CAP_BPF +
    // CAP_PERFMON + CAP_DAC_READ_SEARCH) and the kernel's tracepoint
    // availability — we don't assert load() succeeds, only that the
    // call links and produces an std::optional<SenseHub> of the
    // correct type.  Set CRUCIBLE_PERF_QUIET=1 unconditionally so a
    // load failure (the common case in restricted CI sandboxes)
    // doesn't pollute stderr with a misleading "BPF unavailable"
    // diagnostic.
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    // load() takes an `effects::Init` cap tag (1 byte, EBO-collapsed)
    // post-GAPS-004a.  main() is bona-fide init context, so the tag
    // is constructible here without ceremony.  This call site doubles
    // as a structural test that the cap-gated signature compiles
    // cleanly from a normal init frame — a regression that swapped
    // Init for Bg or removed the parameter entirely would fail-build
    // here, not silently propagate to production callers.
    std::optional<crucible::perf::SenseHub> hub =
        crucible::perf::SenseHub::load(::crucible::effects::Init{});
    static_assert(std::is_same_v<
        decltype(hub),
        std::optional<crucible::perf::SenseHub>>);
    // hub may be empty (no caps) or populated (caps granted) — both
    // are valid outcomes; a populated hub also exposes attached/
    // failure counters that should be self-consistent.
    if (hub.has_value()) {
        // attached_programs() / attach_failures() return
        // Refined<bounded_above<64>, size_t> — the bound is enforced
        // structurally at construction inside the implementation.
        // Verify the wrapper TYPE is what we expect (regression-witness
        // for any future relaxation of the bound), then unwrap with
        // .value() to compare counts.
        const auto attached_refined = hub->attached_programs();
        const auto failures_refined = hub->attach_failures();
        static_assert(std::is_same_v<
            decltype(attached_refined),
            const crucible::safety::Refined<
                crucible::safety::bounded_above<64>, std::size_t>>);
        static_assert(std::is_same_v<
            decltype(failures_refined),
            const crucible::safety::Refined<
                crucible::safety::bounded_above<64>, std::size_t>>);
        const std::size_t attached = attached_refined.value();
        const std::size_t failures = failures_refined.value();
        // Total programs (attached + failed) cannot be 0 if load()
        // returned a populated hub — the loader rejects that case
        // upstream.
        if (attached == 0 && failures == 0) {
            std::fprintf(stderr,
                "perf::SenseHub::load() returned populated hub with "
                "zero programs — should have been std::nullopt\n");
            return 1;
        }
        // Bound check at runtime — Refined enforces this at
        // construction under semantic=enforce, but defending the
        // assertion explicitly catches the case where the bound is
        // relaxed without anyone updating the consumer.
        if (attached > 64 || failures > 64) {
            std::fprintf(stderr,
                "perf::SenseHub: counter exceeded inplace_vector cap "
                "(attached=%zu failures=%zu)\n", attached, failures);
            return 1;
        }
        // Exercise counters_view() — the Borrowed<...> typed span.
        // When hub.has_value() the view must be non-empty (NUM_COUNTERS
        // elements wide).  Borrowed is empty iff its underlying span_
        // is empty; check via the size of the inner span.
        const auto view = hub->counters_view();
        // Borrowed exposes its span via no public accessor we want to
        // depend on for this smoke test; the size discriminator is
        // simply that load() succeeded → the mmap is live → view
        // borrows the full counter array.  This call site primarily
        // verifies the symbol resolves and the type matches.
        static_assert(std::is_same_v<
            decltype(view),
            const crucible::safety::Borrowed<
                const volatile std::uint64_t,
                crucible::perf::SenseHub>>);
    }
#endif

    std::printf("perf::SenseHub smoke OK\n");
    return 0;
}
