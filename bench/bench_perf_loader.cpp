// Worked example demonstrating the BPF observability surface through the
// `crucible::perf::Senses` aggregator.
//
// What this bench measures, end-to-end:
//
//   (A) ONE-SHOT load cost: `Senses::load_all()` and `load_subset()`.  Both
//       are libbpf-bound; expected wall-clock is ~50-200 ms per program ×
//       7 programs (5 legacy + 2 BTF-typed) for the verifier + attach +
//       mmap path — total in the ~300-1500 ms range with the BTF facades
//       loaded.  Printed as a banner — not a bench-iteration body.
//       Putting libbpf load into a `.measure(...)` loop would be a
//       textbook misuse: each iteration would re-attach and blow past
//       the 1-second total budget on the first call.
//
//   (B) Per-call accessor cost: `s.sense_hub()`, `s.sched_switch()`, etc.
//       Each is a single load from the optional's bit (held in cache),
//       sub-ns in steady state — the cost a production caller pays per
//       facade access.  Demonstrates that the aggregator adds zero overhead
//       on the read path vs hand-holding 7 std::optional<Facade>.
//
//   (C) Coverage diagnostics: `s.coverage()` plus `attached_count()`.  Used
//       by runtime observation for drift attribution ("can't read CPU stalls — PmuSample
//       is missing") and by bench banners.  Both sub-ns; we bench them
//       separately because attached_count() does 7 conditional adds and
//       could in principle be slower than the bare struct read.
//
//   (D) The "real" hot read: `SenseHub::read()` over 96 counters = 768 B
//       = 12 cache lines.  Target ~50 ns on warm L1; this is the cost a
//       caller pays per snapshot.  Skipped (with a banner explaining how
//       to fix it) if sense_hub didn't attach — typical when the bench
//       runs with no CAP_BPF + CAP_PERFMON (run it through the launcher
//       /usr/local/bin/crucible-caps, as `loop bench` does).
//
//   (E) Snapshot diff: `Snapshot::operator-()` runs 96 sub_sat — exercised
//       at every runtime observation sample (paired snapshot before/after a workload).
//       Target ~100 ns; a reasonable upper bound on the steady-state cost
//       of windowed counter deltas.
//
//   (K) The read of an event of a ring through its ticket
//       (SharedView::read_ticket), on a ring of sealed memory that needs no
//       BPF program.  Each row reads the 64 newest events of 32 bytes of a
//       ring of TIMELINE_CAPACITY slots.  The acquire-load row reads the commit
//       word of each event, and the read_ticket row copies each event.  A
//       seqlock of the same words in atomic words of the base is the reference,
//       and a plain copy of the same 2 KiB, which takes no check, is the floor.
//       Each event takes six loads of read_ticket: the commit word, four words
//       and the commit word again.  A core with three loads in each cycle
//       (Zen 4) does them in two cycles.  The write rows do the same for
//       SharedView::write_ticket, whose compare-exchange keeps two writers out
//       of one slot.  Each write row writes the same 64 slots.
//
// Reading the output:
//   • [batch-avg] flag → auto-batched percentiles are over batch means.
//     Sub-5-ns ops can't be timed individually; the harness amortizes
//     rdtsc cost over a batch and reports per-op means.
//   • Variance > 5 % → throttling or migration; results invalid (CLAUDE.md
//     §VIII "Measurement discipline").
//   • Cold-start tail in `max` → first call paged in libbpf's text + the
//       BPF programs' .rodata; matches CLAUDE.md's "max from cold-start
//       cache miss" note in §VIII.
//
// What this bench does not measure:
//   • End-to-end "from event to userspace counter" latency — that's
//     dominated by tracepoint dispatch and BPF program execution, not
//     by the userspace facade.
//   • Per-facade .read_recent() / timeline-ring drain costs — those have
//     their own bench targets where the BTF-typed event shape matters.
//
// To run with capabilities (recommended), through the launcher that the
// owner installs one time (utils/assay/loop/caps.py):
//   python3 utils/bin/loop bench bench_perf_loader
// or by hand, as one command:
//   /usr/local/bin/crucible-caps --inh-caps +bpf,+perfmon,+dac_read_search
//       --ambient-caps +bpf,+perfmon,+dac_read_search -- ./build-bench/bench/bench_perf_loader
//
// To run as root (alternative):
//   sudo ./build-bench/bench/bench_perf_loader

#include <cstddef>
#include <cstdint>
#include <meta>
#include <utility>

#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/Senses.h>
#include <crucible/perf/SenseHub.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/OwnedMmap.h>
#include <fixy/os/Process.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/ReflectionList.h>

#include "bench_harness.h"

namespace {

namespace eff = ::foundation::effects;

// The nanoseconds in one millisecond.
inline constexpr std::uint64_t kNsPerMs = 1000000;

// The milliseconds between two readings of the monotonic clock, in
// nanoseconds.  It times the one-shot libbpf loads without a bench loop: a
// load takes 50-200 ms, so a bench loop would never finish.
[[nodiscard]] std::uint64_t elapsed_ms(::fixy::Side<bench::side::ReadingBefore, std::uint64_t> start_ns,
                                       ::fixy::Side<bench::side::ReadingAfter, std::uint64_t> end_ns) noexcept {
    return end_ns.value() > start_ns.value() ? (end_ns.value() - start_ns.value()) / kNsPerMs : 0;
}

void print_coverage(const crucible::perf::CoverageReport& cov) {
    // The denominator is the number of facade flags in CoverageReport,
    // read by reflection, so a facade added to the aggregator moves it.
    static constexpr std::size_t kFacadeCount =
        ::foundation::reflect::reflection_list(
            std::meta::nonstatic_data_members_of(^^crucible::perf::CoverageReport,
                                                 std::meta::access_context::current()))
            .size();
    bench::print("  attached: {}/{}", cov.attached_count(), kFacadeCount);
    if (cov.attached_count() == 0) {
        // No attached facade is the most common surprise.  Tell the user how to
        // fix it instead of leaving them to grep.  Two recovery paths:
        //   • the launcher /usr/local/bin/crucible-caps  (as `loop bench` runs it)
        //   • sudo ./bench_perf_loader  (per-invocation)
        bench::print(" — run it through /usr/local/bin/crucible-caps, as `loop bench` does, or invoke under sudo");
    }
    // Each flag prints as the C form %d of a bool printed it: 1 or 0.
    bench::print("\n  sense_hub={} sched_switch={} pmu_sample={} lock_contention={} syscall_latency={} "
                 "sched_tp_btf={} syscall_tp_btf={}\n",
                 cov.sense_hub_attached ? 1 : 0, cov.sched_switch_attached ? 1 : 0, cov.pmu_sample_attached ? 1 : 0,
                 cov.lock_contention_attached ? 1 : 0, cov.syscall_latency_attached ? 1 : 0,
                 cov.sched_tp_btf_attached ? 1 : 0, cov.syscall_tp_btf_attached ? 1 : 0);
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the BPF programs of the perf facades write the timeline rings that the bench reads")
namespace {

using ::fixy::mmap::RingTicket;

// The count of the 64 tickets before the write index end whose slot in a
// timeline ring of Capacity slots holds the whole record of the ticket, newest
// first.  Capacity is a power of two.  The body is inline in each drain row, so
// the row times the loads and no call.  The window of the fixed extent Capacity
// checks the count of the view one time, and its type holds the count, so a
// read of a ticket takes no check.  A view of fewer than Capacity slots gives
// no window and the count zero.  A ticket below zero has no record.
template <std::uint64_t Capacity, class Events>
[[nodiscard, gnu::always_inline]] inline std::uint64_t
count_live_newest(Events const& view, ::fixy::Side<bench::side::ReadingAfter, std::uint64_t> end) noexcept {
    using Event = std::remove_const_t<typename Events::element_type>;
    std::uint64_t live = 0;
    for (auto const ring : view.template window<Capacity>(0)) {
        for (std::uint64_t i = 0; i < 64; ++i) {
            if (ring.template has_ticket<&Event::commit>(RingTicket::mask(end.value() - 1 - i))) ++live;
        }
    }
    return live;
}

// The count of the tickets from pre up to post whose slot in a timeline ring
// of Capacity slots holds the whole record of the ticket, at most Capacity of
// them, oldest first.  When the window holds more than Capacity new tickets,
// the ring wrote over the records of the older ones, and the walk takes the
// newest Capacity of them.  The window of the fixed extent Capacity holds the
// count in its type, as in count_live_newest, so a read of a ticket takes no
// check.  Complexity: linear in the window.
template <std::uint64_t Capacity, class Events>
[[nodiscard]] std::size_t count_live_window(Events const& events,
                                            ::fixy::Side<bench::side::ReadingBefore, std::uint64_t> pre,
                                            ::fixy::Side<bench::side::ReadingAfter, std::uint64_t> post) noexcept {
    using Event = std::remove_const_t<typename Events::element_type>;
    const std::uint64_t delta = (post.value() >= pre.value()) ? (post.value() - pre.value()) : 0;
    const std::size_t to_walk = (delta > Capacity) ? Capacity : static_cast<std::size_t>(delta);
    std::size_t live = 0;
    for (auto const ring : events.template window<Capacity>(0)) {
        for (std::size_t i = 0; i < to_walk; ++i) {
            if (ring.template has_ticket<&Event::commit>(RingTicket::mask(post.value() - to_walk + i))) ++live;
        }
    }
    return live;
}

// The sum of the words of the whole records of the 64 tickets before the write
// index end of a timeline ring of Capacity slots, newest first.  Each event is
// a copy through its ticket, so the row times the whole read of an event, and
// the sum keeps each word of the copy alive.  Capacity is a power of two.  The
// body is inline in each row, and the window is as in count_live_newest.
template <std::uint64_t Capacity, class Events>
[[nodiscard, gnu::always_inline]] inline std::uint64_t
sum_tickets_newest(Events const& view, ::fixy::Side<bench::side::ReadingAfter, std::uint64_t> end) noexcept {
    using Event = std::remove_const_t<typename Events::element_type>;
    std::uint64_t sum = 0;
    for (auto const ring : view.template window<Capacity>(0)) {
        for (std::uint64_t i = 0; i < 64; ++i) {
            for (Event const event : ring.template read_ticket<&Event::commit>(RingTicket::mask(end.value() - 1 - i))) {
                sum += event.off_cpu_ns + event.tid + event.on_cpu + event.ts_ns + event.commit;
            }
        }
    }
    return sum;
}

// Writes event as the records of the 64 tickets before the write index end of
// a ring of Capacity slots, newest first, and gives the count of the writes
// that did not drop.  The window is as in count_live_newest.
template <std::uint64_t Capacity, class Events>
[[nodiscard, gnu::always_inline]] inline std::uint64_t
fill_tickets_newest(Events const& view, ::fixy::Side<bench::side::ReadingAfter, std::uint64_t> end,
                    typename Events::element_type const& event) noexcept {
    using Event = typename Events::element_type;
    std::uint64_t written = 0;
    for (auto const ring : view.template window<Capacity>(0)) {
        for (std::uint64_t i = 0; i < 64; ++i) {
            ::fixy::mmap::RingWrite const end_of_write =
                ring.template write_ticket<&Event::commit>(RingTicket::mask(end.value() - 1 - i), event);
            written += end_of_write == ::fixy::mmap::RingWrite::written ? 1U : 0U;
        }
    }
    return written;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// ── (K) The ring of sealed memory ──────────────────────────────────────
namespace {

using RingEvent = crucible::perf::TimelineSchedEvent;
inline constexpr std::size_t kRingEvents = crucible::perf::TIMELINE_CAPACITY;
inline constexpr std::size_t kRingBytes = kRingEvents * sizeof(RingEvent);
using FullRing = ::fixy::mmap::SharedView<RingEvent, kRingEvents>;
using RingOf = ::fixy::mmap::SharedView<RingEvent>;

// The write index of a ring that one write filled: the newest 64 slots are
// contiguous, and each of them holds an event.
inline constexpr std::uint64_t kRingEnd = kRingEvents;

// The region of the mapping of the ring.
struct SealedRingRegion {
    using permission_row = eff::Row<>;
};

using ReadWriteRing = ::fixy::atom::mmap::with_prot<::fixy::mmap::prot::ReadWrite>;
using SharedRing = ::fixy::atom::mmap::with_share<::fixy::mmap::share::Shared>;

// The event of the slot index: each word is not zero, so the sum of the words
// of the events depends on each word.
[[nodiscard]] constexpr RingEvent event_of_slot(std::uint64_t index) noexcept {
    return RingEvent{.off_cpu_ns = index * 3 + 1,
                     .tid = static_cast<std::uint32_t>(index + 1),
                     .on_cpu = static_cast<std::uint32_t>(index % 8 + 1),
                     .ts_ns = index + 1,
                     .commit = index * 5 + 1};
}

// The region of a mint, or the error of the mint.
template <class Region>
[[nodiscard]] ::fixy::Result<Region, ::foundation::os::Errno>
region_of(::fixy::Result<::fixy::Linear<Region>, ::foundation::os::Errno> minted) {
    CRUCIBLE_TRY(made, std::move(minted));
    return std::move(made).consume();
}

// Runs a row on the ring of sealed memory.  The mapping ends before the
// permission of its region, which the caller keeps.  body gets the view of
// the ring and gives a number that keeps its loads alive.  A kernel that
// gives no memory or no map gives the empty view, and the row then times the
// check of its count.
template <class Body, class Brand>
[[nodiscard]] bench::Report
run_on_sealed_ring(::foundation::permissions::Permission<SealedRingRegion, Brand> const& owner, ::fixy::TextView name,
                   Body body) {
    auto const on_view = [&](RingOf const view) {
        return bench::run(name, [&] { bench::do_not_optimize(body(view)); });
    };
    ::fixy::InitLoadCtx const startup{::foundation::effects::testing::init()};
    auto const length = ::fixy::fs::FileOffset{static_cast<std::int64_t>(kRingBytes)};
    ::fixy::fs::SealedMemory const memory =
        ::fixy::fs::mint_sealed_memory(startup, length)
            .match([](::fixy::Linear<::fixy::fs::SealedMemory> made) { return std::move(made).consume(); },
                   [](::foundation::os::Errno) { return ::fixy::fs::SealedMemory{}; });
    auto mapped =
        region_of(::fixy::mmap::mint_mmap<ReadWriteRing, SharedRing>(startup, owner, memory.ref(), kRingBytes));
    if (mapped.is_err()) {
        bench::print("[note] no ring of sealed memory for the row '{}', so it times an empty view\n", name);
        return on_view(RingOf{});
    }
    auto mapping = std::move(mapped).expect("the mint gave a mapping");
    for (FullRing const ring : mapping.template view_at<RingEvent, kRingEvents>(0)) {
        for (auto const slot : FullRing::indices()) {
            static_cast<void>(ring.template write_ticket<&RingEvent::commit>(slot, event_of_slot(slot.raw())));
        }
        return on_view(RingOf{ring});
    }
    return on_view(RingOf{});
}

// Runs a row on the ring of sealed memory, and ends the permission of the
// region after the mapping ended.
template <class Body>
[[nodiscard]] bench::Report run_sealed_ring(::fixy::TextView name, Body body) {
    auto owner = ::foundation::permissions::mint_permission_root<SealedRingRegion>();
    bench::Report report = run_on_sealed_ring(owner, name, body);
    ::foundation::linear::drop(std::move(owner));
    return report;
}

}  // namespace

namespace {

// The words of the events of a ring in plain atomic words, four for each
// event.  The seqlock of the reference rows reads and writes them, with no
// record type between.
inline constexpr std::size_t kEventWords = sizeof(RingEvent) / sizeof(std::uint64_t);
inline constexpr std::size_t kStampWord = offsetof(RingEvent, ts_ns) / sizeof(std::uint64_t);
using PlainWords = ::fixy::FixedArray<::fixy::Atomic<std::uint64_t>, kRingEvents * kEventWords>;
using PlainWord = ::fixy::Index<kRingEvents * kEventWords>;
using EventWords = ::fixy::FixedArray<std::uint64_t, kEventWords>;
using EventWord = ::fixy::Index<kEventWords>;
using RingSlot = ::fixy::Index<kRingEvents>;
using PlainEvents = ::fixy::FixedArray<RingEvent, kRingEvents>;
inline constexpr EventWord stamp_word = ::fixy::index<kStampWord>();

alignas(64) constinit PlainWords plain_words{};

// The 64 newest events as plain events, for the floor.
alignas(64) constinit ::fixy::FixedArray<RingEvent, 64> newest_events{};

// The position of the word of an event of a slot, in the plain words.
[[nodiscard, gnu::always_inline]] inline PlainWord word_of(RingSlot slot, EventWord word) noexcept {
    return PlainWord::mask(slot.raw() * kEventWords + word.raw());
}

// Stores value in the word of an event of a slot, with a release store.
[[gnu::always_inline]] inline void store_word(RingSlot slot, EventWord word, std::uint64_t value) noexcept {
    plain_words.with_element(word_of(slot, word),
                             [value](::fixy::Atomic<std::uint64_t>& held) noexcept { held.store_release(value); });
}

// The slot of the event of the index of a drain, newest first.
[[nodiscard, gnu::always_inline]] inline RingSlot
newest_slot(::fixy::Side<bench::side::ReadingAfter, std::uint64_t> end, std::uint64_t index) noexcept {
    return RingSlot::mask(end.value() - 1 - index);
}

void fill_plain_events() noexcept {
    for (RingSlot const slot : PlainEvents::indices()) {
        EventWords const words = ::fixy::bit_cast<EventWords>(event_of_slot(slot.raw()));
        for (EventWord const at : EventWords::indices())
            store_word(slot, at, words.at(at));
    }
    for (auto const index : decltype(newest_events)::indices())
        newest_events.set(index, event_of_slot(kRingEvents - 64 + index.raw()));
}

// The sum of the words of the 64 newest events through the standard seqlock
// read: an acquire load of the stamp, an acquire load of each other word, and a
// second load of the stamp.  It is the reference of sum_tickets_newest, with
// five loads for each event.
[[nodiscard, gnu::always_inline]] inline std::uint64_t
sum_seqlock_newest(::fixy::Side<bench::side::ReadingAfter, std::uint64_t> end) noexcept {
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < 64; ++i) {
        RingSlot const slot = newest_slot(end, i);
        std::uint64_t const first = plain_words.at(word_of(slot, stamp_word)).load_acquire();
        if (first == 0) continue;
        std::uint64_t const off_cpu_ns = plain_words.at(word_of(slot, ::fixy::index<0>())).load_acquire();
        std::uint64_t const ids = plain_words.at(word_of(slot, ::fixy::index<1>())).load_acquire();
        std::uint64_t const pad = plain_words.at(word_of(slot, ::fixy::index<3>())).load_acquire();
        if (plain_words.at(word_of(slot, stamp_word)).load_acquire() != first) continue;
        sum += off_cpu_ns + static_cast<std::uint32_t>(ids) + (ids >> 32) + first + pad;
    }
    return sum;
}

// Writes event into the 64 newest events through the standard seqlock write:
// an exchange of the stamp to zero, a release store of each other word and a
// release store of the stamp.  It is the reference of fill_tickets_newest.
[[gnu::always_inline]] inline void fill_seqlock_newest(::fixy::Side<bench::side::ReadingAfter, std::uint64_t> end,
                                                       RingEvent const& event) noexcept {
    EventWords const from = ::fixy::bit_cast<EventWords>(event);
    for (std::uint64_t i = 0; i < 64; ++i) {
        RingSlot const slot = newest_slot(end, i);
        plain_words.with_element(word_of(slot, stamp_word), [](::fixy::Atomic<std::uint64_t>& stamp) noexcept {
            static_cast<void>(stamp.exchange_acq_rel(0));
        });
        store_word(slot, ::fixy::index<0>(), from.at(::fixy::index<0>()));
        store_word(slot, ::fixy::index<1>(), from.at(::fixy::index<1>()));
        store_word(slot, ::fixy::index<3>(), from.at(::fixy::index<3>()));
        store_word(slot, stamp_word, event.ts_ns);
    }
}

// A copy of the 64 newest events with no check, which is the floor.
void copy_newest() noexcept {
    ::fixy::FixedArray<RingEvent, 64> copied{};
    copied.copy_from(newest_events.view());
    bench::do_not_optimize(copied);
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();
    const bench::JsonTail json = bench::env_json();

    bench::print("=== senses ===\n");

    // ── (A) ONE-SHOT loads ─────────────────────────────────────────────
    //
    // Senses::load_all loads every available subprogram independently;
    // partial loads are tolerated and recorded in coverage().  We time
    // THE load we actually keep — measuring a temporary load() and then
    // doing it again would (a) double the libbpf attach cost (~130 ms on
    // this hardware) and (b) double the partial-attach noise on stderr.
    const std::uint64_t t0 = bench::detail::read_monotonic_ns();
    auto s = crucible::perf::Senses::load_all(::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    const std::uint64_t t1 = bench::detail::read_monotonic_ns();
    const auto cov = s.coverage();
    bench::print("load_all: {} ms\n", elapsed_ms(::fixy::side_as<bench::side::ReadingBefore>(t0),
                                                 ::fixy::side_as<bench::side::ReadingAfter>(t1)));
    print_coverage(cov);

    // load_subset cost-discipline path: callers that want predictable
    // overhead pick a low-rate pair (sense_hub @ ~0.5 % + pmu_sample @
    // ~0.05 %) and skip the timeline rings.  The result is discarded
    // immediately — we're showing the load cost shape, not retaining
    // a second handle on top of `s` above.
    const std::uint64_t t2 = bench::detail::read_monotonic_ns();
    {
        auto sub = crucible::perf::Senses::load_subset(::fixy::InitLoadCtx{::foundation::effects::testing::init()},
                                                       crucible::perf::SensesMask{
                                                           .sense_hub = true,
                                                           .pmu_sample = true,
                                                       });
        bench::do_not_optimize(sub);
    }
    const std::uint64_t t3 = bench::detail::read_monotonic_ns();
    bench::print(
        "load_subset(sense_hub|pmu_sample): {} ms\n",
        elapsed_ms(::fixy::side_as<bench::side::ReadingBefore>(t2), ::fixy::side_as<bench::side::ReadingAfter>(t3)));

    // ── (B), (C), (D), (E) — per-call benches ──────────────────────────
    //
    // Each report is a single bench::run() against a closure that touches
    // exactly one Senses surface.  Bodies guard the .read() / diff calls
    // on `for (auto const h : ...)` so the bench is meaningful even when
    // sense_hub didn't attach: in that case the read body costs only one
    // load + one predicted-not-taken branch (~1-2 ns), which is itself a
    // useful measurement of "the safety check is ~free".

    // Pre-read a baseline snapshot OUTSIDE the bench body — operator- is
    // what we're benching, not the read.  A null-attached sense_hub leaves
    // baseline default-constructed (zeroed); the diff body skips on null
    // anyway so this is moot in that branch.
    crucible::perf::Snapshot baseline{};
    for (auto const h : s.sense_hub()) {
        baseline = h->read(::foundation::effects::testing::test().nondet);
    }

    // The events of the reference rows of (K).
    fill_plain_events();

    ::fixy::FixedArray<bench::Report, 42> reports{
        // (B) Per-facade accessor — sub-ns single optional-bit load.
        [&] {
            return bench::run("senses.sense_hub()", [&] {
                const auto p = s.sense_hub();
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            return bench::run("senses.sched_switch()", [&] {
                const auto p = s.sched_switch();
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            return bench::run("senses.pmu_sample()", [&] {
                const auto p = s.pmu_sample();
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            return bench::run("senses.lock_contention()", [&] {
                const auto p = s.lock_contention();
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            return bench::run("senses.syscall_latency()", [&] {
                const auto p = s.syscall_latency();
                bench::do_not_optimize(p);
            });
        }(),
        // The BTF-typed facades.  Same shape as the legacy facades —
        // single optional-bit load — so the cost is the same sub-ns load,
        // and the bench covers the whole accessor surface.
        [&] {
            return bench::run("senses.sched_tp_btf()", [&] {
                const auto p = s.sched_tp_btf();
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            return bench::run("senses.syscall_tp_btf()", [&] {
                const auto p = s.syscall_tp_btf();
                bench::do_not_optimize(p);
            });
        }(),
        // (C) Coverage diagnostic — 7 bools by value (≤ 4 B).  runtime observation reads
        // this once per drift-detection sample.
        [&] {
            return bench::run("senses.coverage()", [&] {
                const auto c = s.coverage();
                bench::do_not_optimize(c);
            });
        }(),
        [&] {
            return bench::run("coverage.attached_count() [7 cond. adds]", [&] {
                const std::size_t n = s.coverage().attached_count();
                bench::do_not_optimize(n);
            });
        }(),
        // (D) The actual hot read — 12 cache lines mmap'd from BPF.  Body
        // guards on s.sense_hub() so a null-attached sense_hub measures
        // the bare safety branch (~1-2 ns) instead of segfaulting.
        [&] {
            return bench::run("sense_hub->read() [12 cache lines]", [&] {
                for (auto const h : s.sense_hub()) {
                    const auto snap = h->read(::foundation::effects::testing::test().nondet);
                    bench::do_not_optimize(snap);
                }
            });
        }(),
        // (E) Snapshot diff — 96 × sub_sat over a 768 B baseline.  Two
        // snapshots are diffed at every runtime observation sample to get windowed
        // counters; this report is the steady-state cost of that diff.
        [&] {
            return bench::run("snapshot - snapshot [96 sub_sat]", [&] {
                for (auto const h : s.sense_hub()) {
                    const auto current = h->read(::foundation::effects::testing::test().nondet);
                    const auto delta = current - baseline;
                    bench::do_not_optimize(delta);
                }
            });
        }(),
        // (F) Per-facade snapshot() — the consumer-shaped capture of
        // (scalar metric, timeline_index) on each timeline facade.
        // Every consumer that wants windowed deltas calls snapshot()
        // pre + post and computes `post - pre`; these benches show
        // the per-facade per-snapshot cost.  When attached, each
        // `snapshot()` is dominated by the scalar accessor's
        // `bpf_map_lookup_elem` (~1 µs); when un-attached it costs
        // ~1 ns (single null check).
        [&] {
            return bench::run("sched_switch->snapshot() [1 syscall + 1 mmap-load]", [&] {
                for (auto const h : s.sched_switch()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                }
            });
        }(),
        [&] {
            return bench::run("pmu_sample->snapshot() [1 mmap-load]", [&] {
                for (auto const h : s.pmu_sample()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                }
            });
        }(),
        [&] {
            return bench::run("lock_contention->snapshot() [1 syscall + 1 mmap-load]", [&] {
                for (auto const h : s.lock_contention()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                }
            });
        }(),
        [&] {
            return bench::run("syscall_latency->snapshot() [1 syscall + 1 mmap-load]", [&] {
                for (auto const h : s.syscall_latency()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                }
            });
        }(),
        [&] {
            return bench::run("sched_tp_btf->snapshot() [1 syscall + 1 mmap-load]", [&] {
                for (auto const h : s.sched_tp_btf()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                }
            });
        }(),
        [&] {
            return bench::run("syscall_tp_btf->snapshot() [1 syscall + 1 mmap-load]", [&] {
                for (auto const h : s.syscall_tp_btf()) {
                    const auto snap = h->snapshot();
                    bench::do_not_optimize(snap);
                }
            });
        }(),
        // (G) Snapshot::operator- — sub_sat cost on each facade's
        // Snapshot.  Should be ~1-2 ns (one or two __builtin_sub_overflow).
        // Cost is dominated by the paired snapshot() calls (which the
        // bench body must call to get a meaningful baseline), so the
        // delta itself is essentially free relative to the surrounding
        // syscall.  Per-facade coverage proves no facade's operator-
        // got accidentally elided by templated dead-code-elimination.
        [&] {
            crucible::perf::SchedSwitch::Snapshot ss_pre{};
            for (auto const h : s.sched_switch())
                ss_pre = h->snapshot();
            return bench::run("sched_switch::Snapshot::operator- [2 sub_sat]", [&] {
                for (auto const h : s.sched_switch()) {
                    const auto post = h->snapshot();
                    const auto delta = post - ss_pre;
                    bench::do_not_optimize(delta);
                }
            });
        }(),
        [&] {
            crucible::perf::PmuSample::Snapshot pm_pre{};
            for (auto const h : s.pmu_sample())
                pm_pre = h->snapshot();
            return bench::run("pmu_sample::Snapshot::operator- [1 sub_sat]", [&] {
                for (auto const h : s.pmu_sample()) {
                    const auto post = h->snapshot();
                    const auto delta = post - pm_pre;
                    bench::do_not_optimize(delta);
                }
            });
        }(),
        [&] {
            crucible::perf::LockContention::Snapshot lc_pre{};
            for (auto const h : s.lock_contention())
                lc_pre = h->snapshot();
            return bench::run("lock_contention::Snapshot::operator- [2 sub_sat]", [&] {
                for (auto const h : s.lock_contention()) {
                    const auto post = h->snapshot();
                    const auto delta = post - lc_pre;
                    bench::do_not_optimize(delta);
                }
            });
        }(),
        [&] {
            crucible::perf::SyscallLatency::Snapshot sl_pre{};
            for (auto const h : s.syscall_latency())
                sl_pre = h->snapshot();
            return bench::run("syscall_latency::Snapshot::operator- [2 sub_sat]", [&] {
                for (auto const h : s.syscall_latency()) {
                    const auto post = h->snapshot();
                    const auto delta = post - sl_pre;
                    bench::do_not_optimize(delta);
                }
            });
        }(),
        [&] {
            crucible::perf::SchedTpBtf::Snapshot stp_pre{};
            for (auto const h : s.sched_tp_btf())
                stp_pre = h->snapshot();
            return bench::run("sched_tp_btf::Snapshot::operator- [2 sub_sat]", [&] {
                for (auto const h : s.sched_tp_btf()) {
                    const auto post = h->snapshot();
                    const auto delta = post - stp_pre;
                    bench::do_not_optimize(delta);
                }
            });
        }(),
        [&] {
            crucible::perf::SyscallTpBtf::Snapshot syt_pre{};
            for (auto const h : s.syscall_tp_btf())
                syt_pre = h->snapshot();
            return bench::run("syscall_tp_btf::Snapshot::operator- [2 sub_sat]", [&] {
                for (auto const h : s.syscall_tp_btf()) {
                    const auto post = h->snapshot();
                    const auto delta = post - syt_pre;
                    bench::do_not_optimize(delta);
                }
            });
        }(),
        // (I) timeline_view() — the drain-path entry.  Each call
        // returns a View<const TimelineXxxEvent> over the mmap'd
        // ring buffer (no syscall, no copy — just a View constructor
        // wrapping the cached mmap base).  Cost: ~1-3 ns.  Consumer
        // pattern: call timeline_view() once + read timeline_write_index()
        // once + walk the index range checking ts_ns != 0 per event.
        // The walk cost is workload-dependent; this bench measures
        // the per-call entry-point cost, which is the consumer's
        // baseline overhead even when the workload produced zero new
        // events in the window.
        [&] {
            return bench::run("sched_switch->timeline_view() [View ctor]", [&] {
                for (auto const h : s.sched_switch()) {
                    const auto v = h->timeline_view();
                    bench::do_not_optimize(v);
                }
            });
        }(),
        [&] {
            return bench::run("pmu_sample->timeline_view() [View ctor]", [&] {
                for (auto const h : s.pmu_sample()) {
                    const auto v = h->timeline_view();
                    bench::do_not_optimize(v);
                }
            });
        }(),
        [&] {
            return bench::run("lock_contention->timeline_view() [View ctor]", [&] {
                for (auto const h : s.lock_contention()) {
                    const auto v = h->timeline_view();
                    bench::do_not_optimize(v);
                }
            });
        }(),
        [&] {
            return bench::run("syscall_latency->timeline_view() [View ctor]", [&] {
                for (auto const h : s.syscall_latency()) {
                    const auto v = h->timeline_view();
                    bench::do_not_optimize(v);
                }
            });
        }(),
        [&] {
            return bench::run("sched_tp_btf->timeline_view() [View ctor]", [&] {
                for (auto const h : s.sched_tp_btf()) {
                    const auto v = h->timeline_view();
                    bench::do_not_optimize(v);
                }
            });
        }(),
        [&] {
            return bench::run("syscall_tp_btf->timeline_view() [View ctor]", [&] {
                for (auto const h : s.syscall_tp_btf()) {
                    const auto v = h->timeline_view();
                    bench::do_not_optimize(v);
                }
            });
        }(),
        // (J) Per-facade drain walk — read snapshot.timeline_index +
        // walk the last N events, ts_ns ACQUIRE-load on each.  This
        // is the consumer's hot per-iteration cost: producer-side
        // events are already mmap'd; the consumer pays N × cache-line
        // load for the walk plus one acquire fence per event.
        //
        // Walk size = 64 events (one cache line worth per ts_ns
        // column on x86-64 = 8 ts_ns reads × 8 events per line).
        // Tight enough to cleanly separate the drain cost from the
        // surrounding bench overhead.
        [&] {
            return bench::run("sched_switch drain 64 events [64x acquire-load]", [&] {
                for (auto const h : s.sched_switch()) {
                    const auto view = h->timeline_view();
                    if (view.size() == 0) {
                        bench::do_not_optimize(view);
                        return;
                    }
                    const std::uint64_t live = count_live_newest<crucible::perf::TIMELINE_CAPACITY>(
                        view, ::fixy::side_as<bench::side::ReadingAfter>(h->timeline_write_index()));
                    bench::do_not_optimize(live);
                }
            });
        }(),
        [&] {
            return bench::run("pmu_sample drain 64 events [64x acquire-load]", [&] {
                for (auto const h : s.pmu_sample()) {
                    const auto view = h->timeline_view();
                    if (view.size() == 0) {
                        bench::do_not_optimize(view);
                        return;
                    }
                    const std::uint64_t live = count_live_newest<crucible::perf::PMU_SAMPLE_CAPACITY>(
                        view, ::fixy::side_as<bench::side::ReadingAfter>(h->timeline_write_index()));
                    bench::do_not_optimize(live);
                }
            });
        }(),
        [&] {
            return bench::run("lock_contention drain 64 events [64x acquire-load]", [&] {
                for (auto const h : s.lock_contention()) {
                    const auto view = h->timeline_view();
                    if (view.size() == 0) {
                        bench::do_not_optimize(view);
                        return;
                    }
                    const std::uint64_t live = count_live_newest<crucible::perf::TIMELINE_CAPACITY>(
                        view, ::fixy::side_as<bench::side::ReadingAfter>(h->timeline_write_index()));
                    bench::do_not_optimize(live);
                }
            });
        }(),
        [&] {
            return bench::run("syscall_latency drain 64 events [64x acquire-load]", [&] {
                for (auto const h : s.syscall_latency()) {
                    const auto view = h->timeline_view();
                    if (view.size() == 0) {
                        bench::do_not_optimize(view);
                        return;
                    }
                    const std::uint64_t live = count_live_newest<crucible::perf::TIMELINE_CAPACITY>(
                        view, ::fixy::side_as<bench::side::ReadingAfter>(h->timeline_write_index()));
                    bench::do_not_optimize(live);
                }
            });
        }(),
        [&] {
            return bench::run("sched_tp_btf drain 64 events [64x acquire-load]", [&] {
                for (auto const h : s.sched_tp_btf()) {
                    const auto view = h->timeline_view();
                    if (view.size() == 0) {
                        bench::do_not_optimize(view);
                        return;
                    }
                    const std::uint64_t live = count_live_newest<crucible::perf::TIMELINE_CAPACITY>(
                        view, ::fixy::side_as<bench::side::ReadingAfter>(h->timeline_write_index()));
                    bench::do_not_optimize(live);
                }
            });
        }(),
        [&] {
            return bench::run("syscall_tp_btf drain 64 events [64x acquire-load]", [&] {
                for (auto const h : s.syscall_tp_btf()) {
                    const auto view = h->timeline_view();
                    if (view.size() == 0) {
                        bench::do_not_optimize(view);
                        return;
                    }
                    const std::uint64_t live = count_live_newest<crucible::perf::TIMELINE_CAPACITY>(
                        view, ::fixy::side_as<bench::side::ReadingAfter>(h->timeline_write_index()));
                    bench::do_not_optimize(live);
                }
            });
        }(),
        // (K) The copy of an event through its ticket.
        [&] {
            return bench::run("sched_switch drain 64 events [64x read_ticket]", [&] {
                for (auto const h : s.sched_switch()) {
                    const auto view = h->timeline_view();
                    const std::uint64_t sum = sum_tickets_newest<crucible::perf::TIMELINE_CAPACITY>(
                        view, ::fixy::side_as<bench::side::ReadingAfter>(h->timeline_write_index()));
                    bench::do_not_optimize(sum);
                }
            });
        }(),
        [&] {
            return run_sealed_ring("sealed ring drain 64 events [64x acquire-load]", [](RingOf const view) {
                return count_live_newest<kRingEvents>(view, ::fixy::side_as<bench::side::ReadingAfter>(kRingEnd));
            });
        }(),
        [&] {
            return run_sealed_ring("sealed ring drain 64 events [64x read_ticket]", [](RingOf const view) {
                return sum_tickets_newest<kRingEvents>(view, ::fixy::side_as<bench::side::ReadingAfter>(kRingEnd));
            });
        }(),
        [&] {
            return bench::run("seqlock drain 64 events [reference]", [&] {
                bench::do_not_optimize(sum_seqlock_newest(::fixy::side_as<bench::side::ReadingAfter>(kRingEnd)));
            });
        }(),
        [&] { return bench::run("plain copy of 64 events, no check [floor]", [&] { copy_newest(); }); }(),
        [&] {
            // Each turn writes the tickets of the next lap, so each write is
            // the next write of its slot and none drops.
            return run_sealed_ring("sealed ring write 64 events [64x write_ticket]",
                                   [end = kRingEnd](RingOf const view) mutable {
                                       end += kRingEvents;
                                       return fill_tickets_newest<kRingEvents>(
                                           view, ::fixy::side_as<bench::side::ReadingAfter>(end), event_of_slot(7));
                                   });
        }(),
        [&] {
            return bench::run("seqlock write 64 events [reference]", [&] {
                fill_seqlock_newest(::fixy::side_as<bench::side::ReadingAfter>(kRingEnd), event_of_slot(7));
            });
        }(),
    };

    bench::emit_reports_text(reports.view());

    // The cost of a read of a whole event over the read of its stamp, over the
    // seqlock and over the floor, and the cost of a write over the seqlock.
    bench::compare(reports.at(::fixy::index<36>()), reports.at(::fixy::index<37>())).print_text();
    bench::compare(reports.at(::fixy::index<38>()), reports.at(::fixy::index<37>())).print_text();
    bench::compare(reports.at(::fixy::index<39>()), reports.at(::fixy::index<37>())).print_text();
    bench::compare(reports.at(::fixy::index<41>()), reports.at(::fixy::index<40>())).print_text();

    // Diagnostic banner for the read-skipped case so a user staring at
    // "1.5 ns" for sense_hub->read() knows it didn't actually attach.
    if (!cov.sense_hub_attached) {
        bench::print("\n[note] sense_hub not attached; the last two reports "
                     "measure only the safety branch (~1-2 ns), NOT the\n"
                     "       12-cache-line read.  Grant CAP_BPF+CAP_PERFMON "
                     "to see the real read cost.\n");
    }

    // ── (H) Consumer-shaped workload demo ──────────────────────────────
    //
    // Bench is the single consumer of the perf facades for now; this
    // section demonstrates the canonical pre/work/post/delta read
    // pattern that production consumers (Keeper, WorkloadProfiler,
    // runtime observation, DeadlineWatchdog) will eventually use.
    //
    // Snapshots all 7 facades, runs a workload that should nudge every
    // metric (CPU spin → ctx_switches via timer interrupts; getpid loop
    // → total_syscalls; short spin gate → contention-shaped CPU pressure),
    // snapshots again, and prints `post - pre` deltas.
    //
    // When CAP_BPF is missing the deltas are all zero — the print
    // banner is still useful as a "what fields are available" reference
    // for prospective consumers writing against the same API.
    bench::print("\n=== consumer-shaped workload demo ===\n");
    bench::print("Pattern: snapshot pre → run nudge workload → snapshot post → print delta\n");

    // Pre snapshots
    crucible::perf::Snapshot sh_pre{};
    crucible::perf::SchedSwitch::Snapshot ss_pre{};
    crucible::perf::PmuSample::Snapshot pm_pre{};
    crucible::perf::LockContention::Snapshot lc_pre{};
    crucible::perf::SyscallLatency::Snapshot sl_pre{};
    crucible::perf::SchedTpBtf::Snapshot stp_pre{};
    crucible::perf::SyscallTpBtf::Snapshot syt_pre{};
    for (auto const h : s.sense_hub())
        sh_pre = h->read(::foundation::effects::testing::test().nondet);
    for (auto const h : s.sched_switch())
        ss_pre = h->snapshot();
    for (auto const h : s.pmu_sample())
        pm_pre = h->snapshot();
    for (auto const h : s.lock_contention())
        lc_pre = h->snapshot();
    for (auto const h : s.syscall_latency())
        sl_pre = h->snapshot();
    for (auto const h : s.sched_tp_btf())
        stp_pre = h->snapshot();
    for (auto const h : s.syscall_tp_btf())
        syt_pre = h->snapshot();

    // Workload: short CPU spin (timer-interrupt-driven ctx switches),
    // syscall driver (raw_syscalls/sys_exit events), and an atomic-flag
    // spin-contention phase.  No parked waits here: this bench should not
    // teach production code to use futex/cv waits.
    // Sized for ~50 ms total wall-clock so the deltas are large enough
    // to be visible above noise floor on a quiet system.
    {
        std::uint64_t spin_acc = 0;
        const std::uint64_t spin_until = bench::detail::read_monotonic_ns() + 30 * kNsPerMs;
        while (bench::detail::read_monotonic_ns() < spin_until) {
            for (int i = 0; i < 1024; ++i) {
                spin_acc += static_cast<std::uint64_t>(i) * std::uint64_t{2654435761};
            }
            bench::do_not_optimize(spin_acc);
        }
        // Each read of the id of the process is one getpid call.
        const ::fixy::InitLoadCtx startup{eff::testing::init()};
        for (int i = 0; i < 200; ++i)
            static_cast<void>(::fixy::process::current_process(startup));
        // The gate is closed before the releaser starts.  The releaser spins
        // for 1 ms and opens it, and the main thread spins until then.
        ::fixy::Atomic<bool> is_gate_closed{true};
        auto releaser = [&is_gate_closed]() noexcept {
            const std::uint64_t release_at = bench::detail::read_monotonic_ns() + kNsPerMs;
            while (bench::detail::read_monotonic_ns() < release_at) {
                CRUCIBLE_SPIN_PAUSE;
            }
            is_gate_closed.store_release(false);
        };
        auto waiter = [&is_gate_closed]() noexcept {
            while (is_gate_closed.load_acquire()) {
                CRUCIBLE_SPIN_PAUSE;
            }
        };
        using Starter = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
        ::fixy::thread::run_beside(Starter{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, std::move(releaser),
                                   waiter)
            .expect("the thread door starts the releaser of the gate");
    }

    // Post snapshots
    crucible::perf::Snapshot sh_post{};
    crucible::perf::SchedSwitch::Snapshot ss_post{};
    crucible::perf::PmuSample::Snapshot pm_post{};
    crucible::perf::LockContention::Snapshot lc_post{};
    crucible::perf::SyscallLatency::Snapshot sl_post{};
    crucible::perf::SchedTpBtf::Snapshot stp_post{};
    crucible::perf::SyscallTpBtf::Snapshot syt_post{};
    for (auto const h : s.sense_hub())
        sh_post = h->read(::foundation::effects::testing::test().nondet);
    for (auto const h : s.sched_switch())
        ss_post = h->snapshot();
    for (auto const h : s.pmu_sample())
        pm_post = h->snapshot();
    for (auto const h : s.lock_contention())
        lc_post = h->snapshot();
    for (auto const h : s.syscall_latency())
        sl_post = h->snapshot();
    for (auto const h : s.sched_tp_btf())
        stp_post = h->snapshot();
    for (auto const h : s.syscall_tp_btf())
        syt_post = h->snapshot();

    // Print deltas — what a consumer sees per-window.
    if (cov.sense_hub_attached) {
        using Idx = crucible::perf::Idx;
        const auto sh_d = sh_post - sh_pre;
        bench::print("  SenseHub.delta:        ctx_vol={}  ctx_invol={}  migrations={}  futex_waits={}\n",
                     std::uint64_t{sh_d[Idx::SCHED_CTX_VOL]}, std::uint64_t{sh_d[Idx::SCHED_CTX_INVOL]},
                     std::uint64_t{sh_d[Idx::SCHED_MIGRATIONS]}, std::uint64_t{sh_d[Idx::FUTEX_WAIT_COUNT]});
    }
    if (cov.sched_switch_attached) {
        const auto d = ss_post - ss_pre;
        bench::print("  SchedSwitch.delta:     ctx_switches={}  timeline_events={}\n", std::uint64_t{d.ctx_switches},
                     std::uint64_t{d.timeline_index});
    }
    if (cov.pmu_sample_attached) {
        const auto d = pm_post - pm_pre;
        bench::print("  PmuSample.delta:       samples={}\n", std::uint64_t{d.samples});
    }
    if (cov.lock_contention_attached) {
        const auto d = lc_post - lc_pre;
        bench::print("  LockContention.delta:  wait_count={}  timeline_events={}\n", std::uint64_t{d.wait_count},
                     std::uint64_t{d.timeline_index});
    }
    if (cov.syscall_latency_attached) {
        const auto d = sl_post - sl_pre;
        bench::print("  SyscallLatency.delta:  total_syscalls={}  timeline_events={}\n",
                     std::uint64_t{d.total_syscalls}, std::uint64_t{d.timeline_index});
    }
    if (cov.sched_tp_btf_attached) {
        const auto d = stp_post - stp_pre;
        bench::print("  SchedTpBtf.delta:      ctx_switches={}  timeline_events={}\n", std::uint64_t{d.ctx_switches},
                     std::uint64_t{d.timeline_index});
    }
    if (cov.syscall_tp_btf_attached) {
        const auto d = syt_post - syt_pre;
        bench::print("  SyscallTpBtf.delta:    total_syscalls={}  timeline_events={}\n",
                     std::uint64_t{d.total_syscalls}, std::uint64_t{d.timeline_index});
    }
    if (cov.attached_count() == 0) {
        bench::print("  (no facades attached; deltas would have been printed here.\n"
                     "   Grant CAP_BPF+CAP_PERFMON to see real metric movement.)\n");
    }

    // Drain pass — consumer pattern of "walk the new events in the ring
    // buffer and process each" (count_live_window).
    using bench::side::ReadingAfter;
    using bench::side::ReadingBefore;
    if (cov.sched_switch_attached) {
        for (auto const h : s.sched_switch()) {
            const std::size_t n = count_live_window<crucible::perf::TIMELINE_CAPACITY>(
                h->timeline_view(), ::fixy::side_as<ReadingBefore>(ss_pre.timeline_index),
                ::fixy::side_as<ReadingAfter>(ss_post.timeline_index));
            bench::print("  SchedSwitch.drain:     {} live event(s) in window\n", n);
        }
    }
    if (cov.pmu_sample_attached) {
        for (auto const h : s.pmu_sample()) {
            const std::size_t n = count_live_window<crucible::perf::PMU_SAMPLE_CAPACITY>(
                h->timeline_view(), ::fixy::side_as<ReadingBefore>(pm_pre.samples),
                ::fixy::side_as<ReadingAfter>(pm_post.samples));
            bench::print("  PmuSample.drain:       {} live sample(s) in window\n", n);
        }
    }
    if (cov.lock_contention_attached) {
        for (auto const h : s.lock_contention()) {
            const std::size_t n = count_live_window<crucible::perf::TIMELINE_CAPACITY>(
                h->timeline_view(), ::fixy::side_as<ReadingBefore>(lc_pre.timeline_index),
                ::fixy::side_as<ReadingAfter>(lc_post.timeline_index));
            bench::print("  LockContention.drain:  {} live event(s) in window\n", n);
        }
    }
    if (cov.syscall_latency_attached) {
        for (auto const h : s.syscall_latency()) {
            const std::size_t n = count_live_window<crucible::perf::TIMELINE_CAPACITY>(
                h->timeline_view(), ::fixy::side_as<ReadingBefore>(sl_pre.timeline_index),
                ::fixy::side_as<ReadingAfter>(sl_post.timeline_index));
            bench::print("  SyscallLatency.drain:  {} live event(s) in window\n", n);
        }
    }
    if (cov.sched_tp_btf_attached) {
        for (auto const h : s.sched_tp_btf()) {
            const std::size_t n = count_live_window<crucible::perf::TIMELINE_CAPACITY>(
                h->timeline_view(), ::fixy::side_as<ReadingBefore>(stp_pre.timeline_index),
                ::fixy::side_as<ReadingAfter>(stp_post.timeline_index));
            bench::print("  SchedTpBtf.drain:      {} live event(s) in window\n", n);
        }
    }
    if (cov.syscall_tp_btf_attached) {
        for (auto const h : s.syscall_tp_btf()) {
            const std::size_t n = count_live_window<crucible::perf::TIMELINE_CAPACITY>(
                h->timeline_view(), ::fixy::side_as<ReadingBefore>(syt_pre.timeline_index),
                ::fixy::side_as<ReadingAfter>(syt_post.timeline_index));
            bench::print("  SyscallTpBtf.drain:    {} live event(s) in window\n", n);
        }
    }

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
