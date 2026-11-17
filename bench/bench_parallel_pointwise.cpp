// The parallel form of the pointwise walk (fixy/concurrent/ParallelPointwise.h)
// for 1, 2, 4 and 8 shards, each shard on a core of its own.
//
// THE CORES
//   The bench states nine threads with CORES 9 in bench/CMakeLists.txt: the
//   eight threads of the shards and the measuring thread.  The shards wait on
//   each other (WaitOnEachOther), so mint_parallel_for gives each shard a
//   thread of its own and runs no shard on the measuring thread.  A claim of
//   nine cores spans the two L3 groups of bench.slice.  The measuring thread
//   takes the core apart, whose L3 group holds the fewest cores of the claim
//   (bench::apart_core), and the shards take the eight other cores.  The
//   threads start with the placement OneCpuEach of fixy/os/ThreadTasks.h over
//   those eight cores, so the thread of shard k runs on the core k.  A thread
//   with a mask of several isolated cores stays on the core of the thread that
//   starts it, so with no such placement the shards share one core.  A shard
//   on the core of the measuring thread made the pause check refuse the row of
//   8 shards: the measuring thread waited 302 ms in 1242 arrivals on its run
//   queue in a loop of 3 s.
//
// THE ROWS
//   Each row writes out = left + right over three dense square planes of
//   floats: 1024 x 1024 (12 MiB in all, in the L3 cache of one group) and
//   4096 x 4096 (192 MiB, in memory).  Each call starts and joins the threads
//   of its shards, so each sample holds that cost.  After the rows of the
//   harness, a line for each count of shards gives its bandwidth over the
//   bytes of one pass, its speedup over the row of one shard at the median,
//   and its ratio to the reference.
//
// THE REFERENCE
//   The first row of each size is the raw loop of the add over the three
//   runs, on the measuring core: the loop that the compiler vectorizes at the
//   width of the target, with no plan, no shard and no thread.  A ratio above
//   1 is a row faster than that loop.
//
// With fewer than nine isolated cores, as under a run with no claim, the
// threads keep the affinity of the process, and the bench says that its rows
// are noisy.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParallelPointwise.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

namespace eff = ::foundation::effects;

using BodyCoupling = ::fixy::spawn::BodyCoupling;
using PlaneRead = ::fixy::StridedView<float const, 2>;
using Storage = ::fixy::HeapArray<float, 64>;
// The context of the parallel form: it owns Bg for the threads of the shards.
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// The threads of the largest row, and the cores that the bench states: one for
// each shard and one for the measuring thread.
constexpr std::size_t kShardThreads = 8;
constexpr std::size_t kClaimedCores = kShardThreads + 1;
constexpr std::size_t kWallMs = 3000;

// One size of the bench: the side of its square planes.
struct Level {
    std::size_t side = 0;

    [[nodiscard]] constexpr std::size_t count() const noexcept { return side * side; }
    // The bytes of one pass: two planes read and one written.
    [[nodiscard]] constexpr std::size_t pass_bytes() const noexcept { return 3 * sizeof(float) * count(); }
};

// The names of the five rows of one size, one literal each.
struct RowNames {
    bench::Run reference;
    bench::Run one;
    bench::Run two;
    bench::Run four;
    bench::Run eight;
};

// The options of the threads of the shards, and the core of the measuring
// thread.
struct Placement {
    ::fixy::thread::ThreadOptions options;
    int measuring_core = -1;
};

struct AddOp final {
    template <class X>
    constexpr X operator()(X left, X right) const noexcept {
        return left + right;
    }
};

// THE REFERENCE: the raw loop of the add over three runs of one count.  An
// add of floats is commutative, so a swap of left and right changes no bit.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the raw loop of the add is the reference of the parallel walk")
[[gnu::noipa, gnu::hot]] void add_raw(::fixy::View<float> out, ::fixy::View<float const> left,
                                      ::fixy::View<float const> right) noexcept {
    float* const target = &*out.begin();
    float const* const first = &*left.begin();
    float const* const second = &*right.begin();
    std::size_t const count = out.size();
    for (std::size_t position = 0; position < count; ++position) {
        target[position] = first[position] + second[position];
    }
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The dense plane of a run.
[[nodiscard]] PlaneRead plane_of(::fixy::View<float const> run, Level const& level) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{level.side, level.side};
    ::fixy::FixedArray<std::size_t, 2> const strides{level.side, 1};
    return ::fixy::mint_strided_view(run, sizes.view(), strides.view(), 0).expect("the plane fits its run");
}

// The inputs of each row of one size, and the placement.
struct Walk {
    Placement placement;
    PlaneRead left;
    PlaneRead right;
};

// The walk of one row.  Out is the unique view of a frame, so the kernel is a
// template, and each instance is one symbol.
template <std::size_t Shards, class Out>
[[gnu::noinline]] void parallel_add(Walk const& walk, Out& out) noexcept {
    BgCtx const ctx{eff::testing::bg()};
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    bool const is_written =
        ::fixy::concurrent::write_pointwise_in_parallel_with_thread_options<BodyCoupling::WaitOnEachOther, Shards>(
            ctx, test, walk.placement.options, out, AddOp{}, walk.left, walk.right)
            .is_ok();
    if (!is_written) ::fixy::fatal("the three planes of the bench have one shape");
}

// Measures the row of Shards shards.
template <std::size_t Shards, class Out>
[[nodiscard]] bench::Report measure_shards(bench::Run run, Walk const& walk, Out& out) {
    return run.core(walk.placement.measuring_core).batch(1).warmup(16).max_wall_ms(kWallMs).measure([&walk, &out] {
        parallel_add<Shards>(walk, out);
    });
}

// The medians that each line of a row compares with: the row of one shard and
// the reference, and the count of the shards of the row.
struct LineBasis {
    std::size_t shards = 1;
    double one_shard_p50_ns = 0.0;
    double reference_p50_ns = 0.0;
};

// Writes one line: the shards, the median, the bandwidth in GB/s, the speedup
// over the median of one shard, and the ratio to the median of the reference.
// The two ratios print with two decimals, from hundredths rounded to the
// nearest.
void print_line(LineBasis const& basis, Level const& level, bench::Report const& measured) noexcept {
    double const bandwidth = static_cast<double>(level.pass_bytes()) / measured.pct.p50;
    auto const tenths = static_cast<std::uint64_t>(bandwidth * 10.0 + 0.5);
    auto const speedup = static_cast<std::uint64_t>(basis.one_shard_p50_ns / measured.pct.p50 * 100.0 + 0.5);
    auto const ratio = static_cast<std::uint64_t>(basis.reference_p50_ns / measured.pct.p50 * 100.0 + 0.5);
    ::fixy::report(::fixy::Sink::Out,
                   "  {}x{}, {} shards: p50 {} ns, {}.{} GB/s, speedup {}.{}{} over one shard, {}.{}{} of the raw "
                   "loop\n",
                   level.side, level.side, basis.shards, static_cast<std::uint64_t>(measured.pct.p50 + 0.5),
                   tenths / 10, tenths % 10, speedup / 100, speedup % 100 / 10, speedup % 10, ratio / 100,
                   ratio % 100 / 10, ratio % 10);
}

// The five rows of one size: the rows of the harness, then the line of each
// count of shards.
void run_level(Level const& level, RowNames names, Placement const& placement) {
    auto const alloc = eff::testing::test().alloc;
    auto const fill = [](std::size_t position) noexcept { return static_cast<float>(position % 1021) * 0.25f + 0.5f; };
    Storage out = ::fixy::mint_heap_array_with<float, 64>(alloc, level.count(), fill);
    Storage const left = ::fixy::mint_heap_array_with<float, 64>(alloc, level.count(), fill);
    Storage const right = ::fixy::mint_heap_array_with<float, 64>(alloc, level.count(), fill);
    Walk const walk{
        .placement = placement,
        .left = plane_of(::fixy::View<float const>{left.view()}, level),
        .right = plane_of(::fixy::View<float const>{right.view()}, level),
    };

    bench::Report const reference =
        names.reference.core(placement.measuring_core)
            .batch(1)
            .warmup(16)
            .max_wall_ms(kWallMs)
            .measure([&out, &left, &right] {
                add_raw(out.view(), ::fixy::View<float const>{left.view()}, ::fixy::View<float const>{right.view()});
                bench::do_not_optimize(out);
            });

    ::fixy::FixedList<bench::Report, 4> reports{};
    out = ::fixy::with_mut(std::move(out), [&](auto run) noexcept {
        ::fixy::FixedArray<std::size_t, 2> const sizes{level.side, level.side};
        ::fixy::FixedArray<std::size_t, 2> const strides{level.side, 1};
        auto unique = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense plane has no overlap");
        static_cast<void>(reports.push(measure_shards<1>(std::move(names.one), walk, unique)));
        static_cast<void>(reports.push(measure_shards<2>(std::move(names.two), walk, unique)));
        static_cast<void>(reports.push(measure_shards<4>(std::move(names.four), walk, unique)));
        static_cast<void>(reports.push(measure_shards<8>(std::move(names.eight), walk, unique)));
    });

    reference.print_text();
    LineBasis basis{.reference_p50_ns = reference.pct.p50};
    for (bench::Report const& measured : reports) {
        measured.print_text();
        if (basis.shards == 1) basis.one_shard_p50_ns = measured.pct.p50;
        basis.shards *= 2;
    }
    basis.shards = 1;
    // The rows of the harness go through stdio, and report() does not.
    bench::detail::flush_standard_output();
    for (bench::Report const& measured : reports) {
        print_line(basis, level, measured);
        basis.shards *= 2;
    }
}

// The cores of the shards: each core of the claim but the core of the
// measuring thread.
[[nodiscard]] crucible::warden::CpuSet shard_cores_of(bench::Cores const& cores, bench::CpuId measuring) noexcept {
    crucible::warden::CpuSet shards{};
    for (int const cpu : cores.set()) {
        if (cpu != measuring.raw()) shards.insert(static_cast<std::size_t>(cpu));
    }
    return shards;
}

}  // namespace

int main() {
    const bench::Cores cores = bench::Cores::of_calling_thread();
    bench::print_system_info();
    bench::elevate_priority();
    const bool is_claimed = cores.count() >= kClaimedCores;
    const bench::CpuId measuring =
        is_claimed ? bench::apart_core(cores, [](bench::CpuId cpu) noexcept { return bench::read_l3_group(cpu); })
                         .value_or(bench::CpuId::none())
                   : bench::CpuId{bench::env_core()};
    Placement const placement{
        .options =
            is_claimed
                ? ::fixy::thread::ThreadOptions{.affinity = bench::detail::mask_of(shard_cores_of(cores, measuring)),
                                                .placement = ::fixy::thread::ThreadPlacement::OneCpuEach}
                : ::fixy::thread::ThreadOptions{},
        .measuring_core = measuring.raw(),
    };
    bench::detail::flush_standard_output();
    if (is_claimed) {
        ::fixy::report(::fixy::Sink::Out,
                       "  shard k on core k of the 8 cores of the claim apart from cpu{}, which the measuring thread "
                       "takes\n",
                       placement.measuring_core);
    } else {
        ::fixy::report(::fixy::Sink::Out,
                       "  NOTE: {} isolated cores, fewer than {}: the threads of the shards keep the affinity of the "
                       "process, and the rows are noisy.  Run the bench through loop bench, which claims its cores.\n",
                       cores.count(), kClaimedCores);
    }
    run_level(Level{.side = 1024},
              RowNames{.reference = bench::Run{"raw loop add 1024x1024 [reference]"},
                       .one = bench::Run{"parallel add 1024x1024 [1 shard]"},
                       .two = bench::Run{"parallel add 1024x1024 [2 shards]"},
                       .four = bench::Run{"parallel add 1024x1024 [4 shards]"},
                       .eight = bench::Run{"parallel add 1024x1024 [8 shards]"}},
              placement);
    run_level(Level{.side = 4096},
              RowNames{.reference = bench::Run{"raw loop add 4096x4096 [reference]"},
                       .one = bench::Run{"parallel add 4096x4096 [1 shard]"},
                       .two = bench::Run{"parallel add 4096x4096 [2 shards]"},
                       .four = bench::Run{"parallel add 4096x4096 [4 shards]"},
                       .eight = bench::Run{"parallel add 4096x4096 [8 shards]"}},
              placement);
    return 0;
}
