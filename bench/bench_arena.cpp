// Arena allocator — per-sample tail-latency benchmark.
//
// Auto-batching is enabled (bench_harness default) because every Arena
// op is sub-5 ns on modern x86; rdtsc would dominate per-op timing.
// Percentiles are over batch means, flagged as "[batch-avg]" in output.
//
// Each entry of the fast path runs in epochs of its arena.  Before each
// sample, the epoch ends and a new epoch opens, outside the timed region.  A
// sample then starts at the first block, and the arena keeps its blocks from
// the earlier samples.  No sample meets a malloc of a new block or the first
// touch of a page, so each entry times one path of the arena and nothing
// else.

#include <cstdio>
#include <cstdlib>
#include <utility>

#include <crucible/Arena.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <foundation/effects/Effect.h>

#include "bench_harness.h"

namespace {

// The body of an entry in epochs: the operation, and the turn of the epoch
// that Run::measure calls before each sample.
template <typename Operation, typename Turn>
struct EpochBody {
    Operation operation;
    Turn turn;

    void operator()() const { operation(); }
    void before_sample() const noexcept { turn(); }
};

// Measures operation with run in epochs of arena.  The operation must only
// allocate from arena.
template <typename Operation>
[[nodiscard]] bench::Report measure_in_epochs(bench::Run run, crucible::Arena& arena, Operation operation) {
    if (const int core = bench::env_core(); core >= 0) (void)run.core(core);
    auto test = foundation::effects::testing::test();
    auto epoch = fixy::mint_box<crucible::ArenaEpoch>(test.alloc, arena.open_epoch());
    auto turn = [&] noexcept {
        std::move(*epoch).end();
        epoch = fixy::mint_box<crucible::ArenaEpoch>(test.alloc, arena.open_epoch());
    };
    bench::Report report = run.measure(EpochBody{operation, turn});
    std::move(*epoch).end();
    return report;
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    // measure_in_epochs pins from CRUCIBLE_BENCH_CORE (else Pin::Auto default).

    std::printf("=== arena ===\n");

    // Each entry below builds a fresh Arena + test context, measures one op,
    // and moves the Report into the array (Report's copy ctor is deleted,
    // move ctor is `= default` — the IIFE-lambda pattern works via NRVO +
    // move-construction into the aggregate-init slot).
    bench::Report reports[] = {
        [&] {
            crucible::Arena arena(1u << 24);
            auto test = foundation::effects::testing::test();
            return measure_in_epochs(bench::Run("arena.alloc(8)"), arena, [&] {
                auto* p = arena.alloc(test.alloc, fixy::mint_refined<fixy::positive>(size_t{8}));
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            crucible::Arena arena(1u << 24);
            auto test = foundation::effects::testing::test();
            return measure_in_epochs(bench::Run("arena.alloc(64)"), arena, [&] {
                auto* p = arena.alloc(test.alloc, fixy::mint_refined<fixy::positive>(size_t{64}));
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            crucible::Arena arena(1u << 24);
            auto test = foundation::effects::testing::test();
            return measure_in_epochs(bench::Run("arena.alloc(64, align=64)"), arena, [&] {
                auto* p = arena.alloc(test.alloc, fixy::mint_refined<fixy::positive>(size_t{64}),
                                      fixy::mint_refined<fixy::power_of_two>(size_t{64}));
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            crucible::Arena arena(1u << 24);
            auto test = foundation::effects::testing::test();
            return measure_in_epochs(bench::Run("arena.alloc_obj<uint64_t>"), arena, [&] {
                auto* p = arena.alloc_obj<uint64_t>(test.alloc);
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            crucible::Arena arena(1u << 24);
            auto test = foundation::effects::testing::test();
            return measure_in_epochs(bench::Run("arena.alloc_array<uint64_t>(100)"), arena, [&] {
                auto* p = arena.alloc_array<uint64_t>(test.alloc, 100);
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            crucible::Arena arena(1u << 24);
            auto test = foundation::effects::testing::test();
            return measure_in_epochs(bench::Run("arena.alloc_array<uint64_t>(0) nullptr"), arena, [&] {
                auto* p = arena.alloc_array<uint64_t>(test.alloc, 0);
                bench::do_not_optimize(p);
            });
        }(),
        [&] {
            // One turn of a loop of epochs, and its one allocation takes the
            // slow path.  The epoch opens at the first block of 4 KB.  The
            // request of 8 KB does not fit that block, so the arena moves to
            // the next block.  The arena keeps the next block from the earlier
            // turns, so the slow path calls no malloc.  The time includes the
            // open and the end of the epoch.  Each call leaves the arena with
            // no epoch, so the entry runs with no measure_in_epochs.
            crucible::Arena arena(4096);
            auto test = foundation::effects::testing::test();
            auto run = bench::Run("arena epoch turn with alloc(8192) to the next block");
            if (const int core = bench::env_core(); core >= 0) (void)run.core(core);
            return run.measure([&] {
                auto epoch = arena.open_epoch();
                auto* p = arena.alloc(test.alloc, fixy::mint_refined<fixy::positive>(size_t{8192}));
                bench::do_not_optimize(p);
                std::move(epoch).end();
            });
        }(),
        [&] {
            crucible::Arena arena(1u << 24);
            auto test = foundation::effects::testing::test();
            return measure_in_epochs(bench::Run("arena.copy_string(\"relu\")"), arena, [&] {
                auto* p = arena.copy_string(test.alloc, "relu");
                bench::do_not_optimize(p);
            });
        }(),
    };

    bench::emit_reports(reports, json);
    return 0;
}
