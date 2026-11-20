// The run of the allocation scaling bench: it reads the arguments, runs the
// rows of each allocator of the build, and prints the time ratios.
// bench_alloc_scaling_rows.h holds the rows and THE ARGUMENTS.

#include <fixy/os/Args.h>

#include "bench_alloc_scaling_rows.h"

namespace bench_alloc {

// The layout of the Tally of bench_alloc_scaling_rows.h.  A gap word lies
// between two counters, and each Tally holds its own cache line.  With
// adjacent counters GCC joins the updates of two counters into one 16-byte
// load and one 16-byte store.  The 8-byte store of the live bytes of a take
// then cannot forward to the 16-byte load of the next give, and each op of
// each allocator waits for the store to reach the cache.
static_assert(alignof(Tally) == 64 && sizeof(Tally) == 64, "each Tally holds one cache line of its own");
static_assert(std::meta::offset_of(^^Tally::allocations).bytes == 0
                  && std::meta::offset_of(^^Tally::gap_after_allocations).bytes == 8
                  && std::meta::offset_of(^^Tally::releases).bytes == 16
                  && std::meta::offset_of(^^Tally::gap_after_releases).bytes == 24
                  && std::meta::offset_of(^^Tally::live_bytes).bytes == 32,
              "a gap word lies between two counters of a Tally, so GCC joins no two updates");

namespace {

// The allocator or the row that an argument names, or none.
[[nodiscard]] ::fixy::Option<AllocatorKind> allocator_of(::fixy::TextView argument) noexcept {
    ::fixy::FixedArray<AllocatorKind, kAllocatorKinds> const kinds{AllocatorKind::base, AllocatorKind::jemalloc,
                                                                   AllocatorKind::mimalloc, AllocatorKind::snmalloc,
                                                                   AllocatorKind::glibc};
    for (AllocatorKind const kind : kinds) {
        if (argument == name_of(kind)) return ::fixy::some(kind);
    }
    return ::fixy::none;
}

[[nodiscard]] ::fixy::Option<RowKind> row_of(::fixy::TextView argument) noexcept {
    for (std::size_t raw = 0; raw < kRowKinds; ++raw) {
        auto const kind = static_cast<RowKind>(raw);
        if (argument == name_of(kind)) return ::fixy::some(kind);
    }
    return ::fixy::none;
}

// The selection of the arguments, and each count of threads (THE ARGUMENTS).
// Complexity: linear in the arguments.
[[nodiscard]] Arguments arguments_of(::fixy::os::Args const& arguments, bench::Spread const& spread) noexcept {
    Arguments result{};
    bool has_allocator = false;
    bool has_row = false;
    bool has_count = false;
    auto const add = [&result, &spread](ThreadCount threads) noexcept {
        if (threads.raw() > spread.count() || threads.raw() > kMaxThreads) {
            ::fixy::fatal("a row of {} threads is larger than the {} slots of the run, or than {}", threads.raw(),
                          spread.count(), kMaxThreads);
        }
        static_cast<void>(result.counts.append(threads).expect("a run has at most kMaxRows counts"));
    };
    add(ThreadCount{1});
    if (spread.count() >= 2) add(ThreadCount{2});
    bool is_program = true;
    for (::fixy::CText const argument : arguments) {
        if (is_program) {
            is_program = false;
            continue;
        }
        ::fixy::Option<AllocatorKind> const allocator = allocator_of(argument.view());
        if (allocator.is_some()) {
            *result.selection.allocators
                 .get(static_cast<std::size_t>(::fixy::Option<AllocatorKind>{allocator}.value_or(AllocatorKind::base)))
                 .expect("an allocator kind is below kAllocatorKinds") = true;
            has_allocator = true;
            continue;
        }
        ::fixy::Option<RowKind> const row = row_of(argument.view());
        if (row.is_some()) {
            *result.selection.rows.get(static_cast<std::size_t>(::fixy::Option<RowKind>{row}.value_or(RowKind::larson)))
                 .expect("a row kind is below kRowKinds") = true;
            has_row = true;
            continue;
        }
        std::uint32_t const threads =
            ::fixy::parse<std::uint32_t>(argument.view())
                .match([](std::uint32_t parsed) noexcept { return parsed; },
                       [&argument](::fixy::ParseError) noexcept -> std::uint32_t {
                           ::fixy::fatal("the argument '{}' names no allocator, no row and no count of threads",
                                         argument.view());
                       });
        has_count = true;
        if (threads > 2) add(ThreadCount{threads});
    }
    if (!has_count) {
        for (std::uint32_t threads = 4; threads <= 16 && threads <= spread.count(); threads *= 2)
            add(ThreadCount{threads});
    }
    if (!has_allocator) {
        for (bool& allocator : result.selection.allocators)
            allocator = true;
    }
    if (!has_row) {
        for (bool& row : result.selection.rows)
            row = true;
    }
    return result;
}

// The summary of the base with the key of a summary of a reference, or none.
[[nodiscard]] ::fixy::Option<Summary> base_of(RunRecord const& record, Summary const& other) noexcept {
    for (Summary const& summary : record.summaries) {
        bool const is_same_row = summary.row == other.row && summary.threads == other.threads
                              && summary.placement == other.placement && summary.size == other.size;
        if (summary.allocator == AllocatorKind::base && is_same_row) return ::fixy::some(summary);
    }
    return ::fixy::none;
}

// Prints the time of the base over the time of each reference for each row
// that the two ran (THE TIME RATIOS).  Complexity: quadratic in the rows.
void print_time_ratios(RunRecord const& record) noexcept {
    bool has_reference = false;
    for (Summary const& summary : record.summaries)
        has_reference = has_reference || summary.allocator != AllocatorKind::base;
    if (!has_reference) return;
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out,
                   "\n=== the time of the base over the time of each reference: above 1.00 the base is slower ===\n");
    for (Summary const& summary : record.summaries) {
        if (summary.allocator == AllocatorKind::base) continue;
        ::fixy::Option<Summary> const base = base_of(record, summary);
        if (base.is_none() || summary.item_ns <= 0.0) continue;
        double const base_ns = ::fixy::Option<Summary>{base}.value_or(Summary{}).item_ns;
        auto const centi_ratio = static_cast<std::uint64_t>(base_ns / summary.item_ns * 100.0 + 0.5);
        ::fixy::report(::fixy::Sink::Out, "  {} {}, {} threads, {} bytes{}{}: {}.{}{}\n", name_of(summary.allocator),
                       name_of(summary.row), summary.threads.raw(), summary.size,
                       summary.row == RowKind::pair ? ::fixy::TextView{", "} : ::fixy::TextView{},
                       summary.row == RowKind::pair ? name_of(summary.placement) : ::fixy::TextView{},
                       centi_ratio / 100, centi_ratio % 100 / 10, centi_ratio % 10);
    }
}

int run_bench(::fixy::os::Args const& arguments) {
    bench::print_system_info();
    bench::elevate_priority();
    bench::Spread const spread = bench::Spread::of_calling_thread();
    Arguments const selected = arguments_of(arguments, spread);
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
    ::fixy::report(::fixy::Sink::Out, "  the references of this build:{}{}{} glibc of the host\n",
#if defined(CRUCIBLE_BENCH_JEMALLOC)
                   ::fixy::TextView{" jemalloc 5.4.0"},
#else
                   ::fixy::TextView{" (no jemalloc)"},
#endif
#if defined(CRUCIBLE_BENCH_MIMALLOC)
                   ::fixy::TextView{" mimalloc 3.7.0"},
#else
                   ::fixy::TextView{" (no mimalloc)"},
#endif
#if defined(CRUCIBLE_BENCH_SNMALLOC)
                   ::fixy::TextView{" snmalloc 0.7.6"}
#else
                   ::fixy::TextView{" (no snmalloc)"}
#endif
    );
    for (ThreadCount const threads : selected.counts) {
        if (threads.raw() > bench::kPauseThreads) {
            ::fixy::report(::fixy::Sink::Out,
                           "  NOTE: a row of {} threads has more threads than the {} that the pause check reads\n",
                           threads.raw(), bench::kPauseThreads);
        }
    }
    ::fixy::Box<RunRecord> record = ::fixy::mint_box<RunRecord>(bench_alloc());
    run_base_rows(spread, selected, *record);
#if defined(CRUCIBLE_BENCH_JEMALLOC)
    run_jemalloc_rows(spread, selected, *record);
#endif
#if defined(CRUCIBLE_BENCH_MIMALLOC)
    run_mimalloc_rows(spread, selected, *record);
#endif
#if defined(CRUCIBLE_BENCH_SNMALLOC)
    run_snmalloc_rows(spread, selected, *record);
#endif
    run_glibc_rows(spread, selected, *record);
    print_time_ratios(*record);
    bench::emit_reports_json(record->reports.view(), bench::env_json());
    return 0;
}

}  // namespace

}  // namespace bench_alloc

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return bench_alloc::run_bench(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
