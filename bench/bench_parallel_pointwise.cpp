// The parallel form of the pointwise walk (fixy/concurrent/ParallelPointwise.h)
// for 1, 2, 4 and 8 shards, each shard on a core of its own.
//
// THE CORES
//   The bench states nine threads with CORES 9 in bench/CMakeLists.txt: the
//   eight threads of the shards and the measuring thread.  The shards wait on
//   each other (WaitOnEachOther), so mint_parallel_for gives each shard a
//   thread of its own and runs no shard on the measuring thread.  A claim of
//   nine cores spans the two L3 groups of bench.slice.  The measuring thread
//   takes the core of slot 0, the quietest core of the claim (THE CORES OF A
//   RUN of bench_harness.h).  A claim of eight cores of one group and one core
//   of the other has the lone core last in that order, and the measuring
//   thread does not take it.  A row of S shards takes the first S other cores
//   in the order of the claim, the quietest first (Cores::first_slots_apart).  The threads start with the placement
//   OneCpuEach of fixy/os/ThreadTasks.h over those S cores, so the thread of
//   shard k runs on the core k of the mask.  A thread
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
// THE ROW OF THE RULE
//   The rows of 1 to 8 shards wait on each other, so each shard has a thread
//   of its own, and the parallelism rule does not decide their threads.  The
//   last row of each size has 8 independent shards, so the rule of
//   fixy/concurrent/ParallelismRule.h gives its threads, as in a walk that
//   ships.  A line gives the threads that the rule gives for the bytes of the
//   walk, and the ratio of the row to the reference.
//
// THE START COST
//   The last three rows start and join 2, 4 and 8 threads of
//   mint_parallel_for with an empty body, with the placement of the walk.  A
//   line gives the median of each and the cost of one more thread from 2 to 8
//   threads.  mint_parallel_for starts its threads in each call, so this cost
//   is in each parallel row, and the parallelism rule reads its measure.
//
// THE REFERENCE
//   The first row of each size is the raw loop of the add over the three
//   runs, on the measuring core: the loop that the compiler vectorizes at the
//   width of the target, with no plan, no shard and no thread.  A ratio above
//   1 is a row faster than that loop.
//
// THE CORE OF EACH SHARD
//   Before the rows, a probe for 2, 4 and 8 shards runs the shards of
//   mint_parallel_for with the coupling and the thread options of the walk.
//   Each shard reads the CPU of its own thread in procfs, and a line gives
//   that CPU and its L3 group, named by the first CPU of the group.  The
//   probe runs threads of its own with the placement of the walk, and not the
//   threads of a timed row.  The walk of one shard starts no thread, because
//   the parallelism rule gives it one thread, so it runs on the measuring
//   thread, and its line gives the measuring core.  The JSON tail of the
//   bench gives each row of the harness.
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
// The rows of the harness: the reference, four counts of shards and the row of
// the rule for each of the two sizes, and the three rows of the start cost.
constexpr std::size_t kRows = 15;
// The CPU of a shard of the probe that did not read its CPU.
constexpr std::uint64_t kNoCpu = ~std::uint64_t{0};

// One size of the bench: the side of its square planes.
struct Level {
    std::size_t side = 0;

    [[nodiscard]] constexpr std::size_t count() const noexcept { return side * side; }
    // The bytes of one pass: two planes read and one written.
    [[nodiscard]] constexpr std::size_t pass_bytes() const noexcept { return 3 * sizeof(float) * count(); }
};

// The names of the six rows of one size, one literal each.
struct RowNames {
    bench::Run reference;
    bench::Run one;
    bench::Run two;
    bench::Run four;
    bench::Run eight;
    bench::Run rule;
};

// The cores of the claim, the core of the measuring thread, and whether the
// claim has a core for each thread of the largest row.
struct Placement {
    bench::Cores cores;
    int measuring_core = -1;
    bool is_claimed = false;
};

// The options of the threads of a row of Shards shards: the first Shards cores
// of the claim apart from the measuring core, one for each thread, or the
// affinity of the process with no claim (THE CORES).
template <std::size_t Shards>
[[nodiscard]] ::fixy::thread::ThreadOptions options_of(Placement const& placement) noexcept {
    if (!placement.is_claimed) return ::fixy::thread::ThreadOptions{};
    using CoreCount = ::fixy::Quantity<::fixy::unit::Element, std::size_t>;
    crucible::warden::CpuSet const shards =
        placement.cores.first_slots_apart(bench::CpuId{placement.measuring_core}, CoreCount{Shards});
    return ::fixy::thread::ThreadOptions{.affinity = bench::detail::mask_of(shards),
                                         .placement = ::fixy::thread::ThreadPlacement::OneCpuEach};
}

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
template <BodyCoupling Coupling, std::size_t Shards, class Out>
[[gnu::noinline]] void parallel_add(Walk const& walk, Out& out) noexcept {
    BgCtx const ctx{eff::testing::bg()};
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    bool const is_written = ::fixy::concurrent::write_pointwise_in_parallel_with_thread_options<Coupling, Shards>(
                                ctx, test, options_of<Shards>(walk.placement), out, AddOp{}, walk.left, walk.right)
                                .is_ok();
    if (!is_written) ::fixy::fatal("the three planes of the bench have one shape");
}

// Measures the row of Shards shards of a coupling.
template <BodyCoupling Coupling, std::size_t Shards, class Out>
[[nodiscard]] bench::Report measure_shards(bench::Run run, Walk const& walk, Out& out) {
    return run.core(walk.placement.measuring_core).batch(1).warmup(16).max_wall_ms(kWallMs).measure([&walk, &out] {
        parallel_add<Coupling, Shards>(walk, out);
    });
}

// The threads that the parallelism rule gives the 8 independent shards of a
// walk of one size, for the bytes of one pass (THE ROW OF THE RULE).
[[nodiscard]] std::size_t rule_threads_of(Level const& level) noexcept {
    std::size_t const plane_bytes = sizeof(float) * level.count();
    ::fixy::concurrent::WorkBudget const budget{
        .read_bytes = 2 * plane_bytes, .write_bytes = plane_bytes, .item_count = level.count()};
    return ::fixy::spawn::choose_parallel_for_threads(
        BodyCoupling::Independent, ::fixy::concurrent::ParallelismRule::host_facts(), budget, kShardThreads);
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

// The tag of the region of the probe of the shards: no element, because a
// shard of the probe reads none.
struct ShardProbeCells final {
    using permission_row = eff::Row<>;
};

// Prints the CPU and the L3 group of each shard of a walk of Shards shards
// (THE CORE OF EACH SHARD).  The shards wait on each other, as the shards of
// the walk do, so each shard has a thread of its own.  The CPU of a shard is
// field 39 of /proc/thread-self/stat, read in its thread.
template <std::size_t Shards>
void print_shard_cores(Placement const& placement) noexcept {
    BgCtx const ctx{eff::testing::bg()};
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    ::fixy::FixedArray<::fixy::Atomic<std::uint64_t>, Shards> cpus{};
    auto whole = ::fixy::spawn::mint_parallel_for_with_thread_options<BodyCoupling::WaitOnEachOther, Shards>(
        ctx, test, ::fixy::concurrent::WorkBudget{.read_bytes = 64},
        ::fixy::mint_owned_region<std::uint32_t>(nullptr, std::size_t{0},
                                                 ::foundation::permissions::mint_permission_root<ShardProbeCells>()),
        [&cpus](auto& shard) noexcept {
            static_cast<void>(shard);
            constexpr std::size_t index = std::remove_cvref_t<decltype(shard)>::tag_type::index;
            ::fixy::FixedText<1024> stat{};
            bool const is_read = ::foundation::os::read_file("/proc/thread-self/stat", stat).is_ok();
            std::uint64_t const cpu = is_read ? bench::detail::processor_of_stat(stat.view()).value_or(kNoCpu) : kNoCpu;
            cpus.at(::fixy::index<index>()).store_release(cpu);
        },
        options_of<Shards>(placement));
    ::foundation::linear::drop(std::move(whole));
    // The rows of the harness go through stdio, and report() does not.
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "  {} shards, probed with the options of the walk:", Shards);
    for (auto const [index, cell] : cpus.enumerate()) {
        std::uint64_t const cpu = cell->load_acquire();
        if (cpu == kNoCpu) {
            ::fixy::report(::fixy::Sink::Out, " shard {} no cpu,", index.raw());
            continue;
        }
        bench::CpuId const core{::fixy::narrow<int>(cpu).value_or(-1)};
        ::fixy::report(::fixy::Sink::Out, " shard {} cpu{} L3 of cpu{},", index.raw(), cpu,
                       bench::read_l3_group(core).first().value_or(-1));
    }
    ::fixy::report(::fixy::Sink::Out, "\n");
}

// Prints the core of the walk of one shard: the measuring core, because the
// walk of one shard runs on the measuring thread (THE CORE OF EACH SHARD).
void print_one_shard_core(Placement const& placement) noexcept {
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "  1 shard, on the measuring thread: cpu{} L3 of cpu{}\n",
                   placement.measuring_core,
                   bench::read_l3_group(bench::CpuId{placement.measuring_core}).first().value_or(-1));
}

// Measures the start and the join of Shards threads of mint_parallel_for with
// an empty body, one thread for each shard, with the options of the walk
// (THE START COST).
template <std::size_t Shards>
[[nodiscard]] bench::Report measure_starts(bench::Run run, Placement const& placement) {
    ::fixy::thread::ThreadOptions const options = options_of<Shards>(placement);
    return run.core(placement.measuring_core).batch(1).warmup(16).max_wall_ms(kWallMs).measure([&options] {
        BgCtx const ctx{eff::testing::bg()};
        ::fixy::TestRunnerCtx const test{eff::testing::test()};
        auto whole = ::fixy::spawn::mint_parallel_for_with_thread_options<BodyCoupling::WaitOnEachOther, Shards>(
            ctx, test, ::fixy::concurrent::WorkBudget{.read_bytes = 64},
            ::fixy::mint_owned_region<std::uint32_t>(
                nullptr, std::size_t{0}, ::foundation::permissions::mint_permission_root<ShardProbeCells>()),
            [](auto& shard) noexcept { static_cast<void>(shard); }, options);
        ::foundation::linear::drop(std::move(whole));
    });
}

// The three rows of THE START COST, then a line with the median of each and the
// cost of one more thread from 2 to 8 threads.  The rows go to all.
void run_starts(Placement const& placement, ::fixy::FixedList<bench::Report, kRows>& all) {
    bench::Report two = measure_starts<2>(bench::Run{"start and join of 2 threads [empty shards]"}, placement);
    bench::Report four = measure_starts<4>(bench::Run{"start and join of 4 threads [empty shards]"}, placement);
    bench::Report eight = measure_starts<8>(bench::Run{"start and join of 8 threads [empty shards]"}, placement);
    two.print_text();
    four.print_text();
    eight.print_text();
    bench::detail::flush_standard_output();
    double const each = (eight.pct.p50 - two.pct.p50) / 6.0;
    ::fixy::report(::fixy::Sink::Out,
                   "  start and join with an empty body: p50 {} ns for 2 threads, {} ns for 4, {} ns for 8, and {} ns "
                   "for each thread from 2 to 8\n",
                   static_cast<std::uint64_t>(two.pct.p50 + 0.5), static_cast<std::uint64_t>(four.pct.p50 + 0.5),
                   static_cast<std::uint64_t>(eight.pct.p50 + 0.5),
                   static_cast<std::uint64_t>(each > 0.0 ? each + 0.5 : 0.0));
    bench::add_report(all, std::move(two));
    bench::add_report(all, std::move(four));
    bench::add_report(all, std::move(eight));
}

// The six rows of one size: the rows of the harness, then the line of each
// count of shards and of the row of the rule.  The rows go to all, for the
// JSON tail of the bench.
void run_level(Level const& level, RowNames names, Placement const& placement,
               ::fixy::FixedList<bench::Report, kRows>& all) {
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

    bench::Report reference =
        names.reference.core(placement.measuring_core)
            .batch(1)
            .warmup(16)
            .max_wall_ms(kWallMs)
            .measure([&out, &left, &right] {
                add_raw(out.view(), ::fixy::View<float const>{left.view()}, ::fixy::View<float const>{right.view()});
                bench::do_not_optimize(out);
            });

    constexpr BodyCoupling kWaiting = BodyCoupling::WaitOnEachOther;
    ::fixy::FixedList<bench::Report, 4> reports{};
    bench::Report rule{};
    out = ::fixy::with_mut(std::move(out), [&](auto run) noexcept {
        ::fixy::FixedArray<std::size_t, 2> const sizes{level.side, level.side};
        ::fixy::FixedArray<std::size_t, 2> const strides{level.side, 1};
        auto unique = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense plane has no overlap");
        static_cast<void>(reports.append(measure_shards<kWaiting, 1>(std::move(names.one), walk, unique)));
        static_cast<void>(reports.append(measure_shards<kWaiting, 2>(std::move(names.two), walk, unique)));
        static_cast<void>(reports.append(measure_shards<kWaiting, 4>(std::move(names.four), walk, unique)));
        static_cast<void>(reports.append(measure_shards<kWaiting, 8>(std::move(names.eight), walk, unique)));
        rule = measure_shards<BodyCoupling::Independent, kShardThreads>(std::move(names.rule), walk, unique);
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
    rule.print_text();
    bench::detail::flush_standard_output();
    auto const rule_ratio = static_cast<std::uint64_t>(basis.reference_p50_ns / rule.pct.p50 * 100.0 + 0.5);
    ::fixy::report(::fixy::Sink::Out,
                   "  {}x{}, {} independent shards: the rule gives {} threads for {} bytes, p50 {} ns, {}.{}{} of the "
                   "raw loop\n",
                   level.side, level.side, kShardThreads, rule_threads_of(level), level.pass_bytes(),
                   static_cast<std::uint64_t>(rule.pct.p50 + 0.5), rule_ratio / 100, rule_ratio % 100 / 10,
                   rule_ratio % 10);
    bench::add_report(all, std::move(reference));
    for (bench::Report& measured : reports) {
        bench::add_report(all, std::move(measured));
    }
    bench::add_report(all, std::move(rule));
}

}  // namespace

int main() {
    const bench::Cores cores = bench::Cores::of_calling_thread();
    bench::print_system_info();
    bench::elevate_priority();
    const bool is_claimed = cores.count() >= kClaimedCores;
    const bench::CpuId measuring = is_claimed ? cores.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none())
                                              : bench::CpuId{bench::env_core()};
    Placement const placement{.cores = cores, .measuring_core = measuring.raw(), .is_claimed = is_claimed};
    bench::detail::flush_standard_output();
    if (is_claimed) {
        ::fixy::report(::fixy::Sink::Out,
                       "  a row of S shards takes the first S cores of the order of the claim apart from cpu{}, which "
                       "the measuring thread takes\n",
                       placement.measuring_core);
    } else {
        ::fixy::report(::fixy::Sink::Out,
                       "  NOTE: {} isolated cores, fewer than {}: the threads of the shards keep the affinity of the "
                       "process, and the rows are noisy.  Run the bench through loop bench, which claims its cores.\n",
                       cores.count(), kClaimedCores);
    }
    print_one_shard_core(placement);
    print_shard_cores<2>(placement);
    print_shard_cores<4>(placement);
    print_shard_cores<8>(placement);
    ::fixy::FixedList<bench::Report, kRows> reports{};
    run_level(Level{.side = 1024},
              RowNames{.reference = bench::Run{"raw loop add 1024x1024 [reference]"},
                       .one = bench::Run{"parallel add 1024x1024 [1 shard]"},
                       .two = bench::Run{"parallel add 1024x1024 [2 shards]"},
                       .four = bench::Run{"parallel add 1024x1024 [4 shards]"},
                       .eight = bench::Run{"parallel add 1024x1024 [8 shards]"},
                       .rule = bench::Run{"parallel add 1024x1024 [8 independent shards, the rule]"}},
              placement, reports);
    run_level(Level{.side = 4096},
              RowNames{.reference = bench::Run{"raw loop add 4096x4096 [reference]"},
                       .one = bench::Run{"parallel add 4096x4096 [1 shard]"},
                       .two = bench::Run{"parallel add 4096x4096 [2 shards]"},
                       .four = bench::Run{"parallel add 4096x4096 [4 shards]"},
                       .eight = bench::Run{"parallel add 4096x4096 [8 shards]"},
                       .rule = bench::Run{"parallel add 4096x4096 [8 independent shards, the rule]"}},
              placement, reports);
    run_starts(placement, reports);
    bench::emit_reports_json(reports.view(), bench::env_json());
    return 0;
}
