// The match of the reductions library and its nets, part four: the parallel
// forms of fixy/concurrent/ParallelTree.h.  A parallel form divides its run
// into grains of parallel_tree_grain elements, so a run of some grains holds
// more than 16,384 leaves.  A net of that size does not fit a constant
// evaluation.  So this unit reads the plan of the run, which write_tree_plan
// and write_scan_plan give at run time.  The nets of the other parts come from
// the same plans (build_combine_net of reductions_net.h), and the checks part
// shows that evaluate_tree_plan and evaluate_scan_plan follow those nets.
//
//   The symbolic run   reduce_by_tree_in_parallel on symbols combines each
//                      step of the tree plan of the run one time, in port
//                      order, and gives the root
//   The bits           sum_by_tree_in_parallel, scan_sums_in_parallel and
//                      sum_segments_by_tree_in_parallel give the bits of the
//                      evaluation of the plan with an add, on Philox draws
//   The golden digests each parallel form gives the digest that this unit
//                      holds, over runs of some grains
//
// The runs hold one grain and one element, four grains with a short last
// grain, and nine grains with a short last grain.  The coupling
// WaitOnEachOther runs 1, 3 and 8 shards, each on its own thread, and three
// shards cut the grains unevenly.  The coupling Independent runs 8 shards on
// the threads of the parallelism rule.

#include <fixy/Core.h>
#include <fixy/concurrent/ParallelTree.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Scan.h>

#include "../foundation/golden_digest.h"
#include "../foundation/reductions_net.h"
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "parallel_tree_test.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using namespace ::crucible::test::reductions_net;

namespace spawn = ::fixy::spawn;
namespace tree = ::crucible::test::parallel_tree;

using ::crucible::test::PhiloxStream;
using ::fixy::Atomic;
using ::fixy::HeapArray;
using ::fixy::View;

inline constexpr std::size_t kGrain = ::fixy::concurrent::parallel_tree_grain;
inline constexpr ::fixy::FixedArray<std::size_t, 3> kCounts{kGrain + 1, 3 * kGrain + 37, 8 * kGrain + 7};
inline constexpr std::size_t kLongest = 8 * kGrain + 7;

// Gives true when body gives true for each coupling and count of shards of
// the test.
template <class Body>
[[nodiscard]] bool is_each_coupling(Body const& body) noexcept {
    return body.template operator()<spawn::BodyCoupling::WaitOnEachOther, 1>()
        && body.template operator()<spawn::BodyCoupling::WaitOnEachOther, 3>()
        && body.template operator()<spawn::BodyCoupling::WaitOnEachOther, 8>()
        && body.template operator()<spawn::BodyCoupling::Independent, 8>();
}

template <class T, std::size_t Count>
[[nodiscard]] HeapArray<T> draw_run(PhiloxStream& stream) noexcept {
    HeapArray<T> run = ::fixy::mint_heap_array(tree::test_alloc(), Count, T{});
    for (T& drawn : run) {
        drawn = draw_value<T>(stream);
    }
    return run;
}

// The compare of the bits is symmetric, so the two runs can swap.
template <class T>
[[nodiscard]] bool has_same_run_bits(View<T const> left, View<T const> right) noexcept {
    if (left.size() != right.size()) return false;
    for (auto const [first, second] : left.walk().try_zip(right.walk()).expect("the two runs have one count")) {
        if (!has_same_bits(*first, *second)) return false;
    }
    return true;
}

// ── The plans ───────────────────────────────────────────────────────────

// The tree plan of a run: one step for each leaf after the first.
template <class T>
[[nodiscard]] HeapArray<core::TreeStep> plan_tree_of(View<T const> run) noexcept {
    HeapArray<core::TreeStep> steps = ::fixy::mint_heap_array(tree::test_alloc(), run.size() - 1, core::TreeStep{});
    bool is_written = false;
    steps = ::fixy::with_mut(std::move(steps),
                             [&](auto out) noexcept { is_written = core::write_tree_plan(std::move(out)).is_ok(); });
    CRUCIBLE_TEST_ASSERT(kTest, is_written);
    return steps;
}

// The value of the tree plan of a run with an add.  A run of one element
// gives that element.
template <class T>
[[nodiscard]] T sum_by_plan(View<T const> run) noexcept {
    HeapArray<core::TreeStep> const steps = plan_tree_of(run);
    HeapArray<T> scratch = ::fixy::mint_heap_array(tree::test_alloc(), steps.size(), T{});
    T sum{};
    scratch = ::fixy::with_mut(std::move(scratch), [&](auto values) noexcept {
        sum = core::evaluate_tree_plan(View<core::TreeStep const>{steps.view()}, run, std::move(values), AddValues{})
                  .expect("a plan of the count of its leaves");
    });
    return sum;
}

// The outputs of the scan plan of a run with an add.  The empty value of the
// exclusive scan is +0, as the sum scans give it.
template <class T>
[[nodiscard]] HeapArray<T> scan_by_plan(core::ScanKind kind, View<T const> run) noexcept {
    std::size_t const step_count = core::count_scan_plan_steps(run.size(), kind).expect("a run below the plan limit");
    HeapArray<core::TreeStep> steps = ::fixy::mint_heap_array(tree::test_alloc(), step_count, core::TreeStep{});
    HeapArray<core::PlanNode> outputs = ::fixy::mint_heap_array(tree::test_alloc(), run.size(), core::PlanNode{});
    bool is_written = false;
    steps = ::fixy::with_mut(std::move(steps), [&](auto step_out) noexcept {
        outputs = ::fixy::with_mut(std::move(outputs), [&](auto output_out) noexcept {
            is_written = core::write_scan_plan(kind, std::move(step_out), std::move(output_out)).is_ok();
        });
    });
    HeapArray<T> scratch = ::fixy::mint_heap_array(tree::test_alloc(), step_count, T{});
    HeapArray<T> scanned = ::fixy::mint_heap_array(tree::test_alloc(), run.size(), T{});
    bool is_evaluated = false;
    scratch = ::fixy::with_mut(std::move(scratch), [&](auto values) noexcept {
        scanned = ::fixy::with_mut(std::move(scanned), [&](auto out) noexcept {
            is_evaluated = core::evaluate_scan_plan(View<core::TreeStep const>{steps.view()},
                                                    View<core::PlanNode const>{outputs.view()}, run, std::move(values),
                                                    std::move(out), T{}, AddValues{})
                               .is_ok();
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_written && is_evaluated);
    return scanned;
}

// ── The symbolic run of the parallel reduce ─────────────────────────────
//
// A tree plan reads each node in one step at most.  So the step that reads a
// node as its left operand finds the step of a combine in O(1): step_of_left
// holds that step plus one, and 0 for a node that no step reads on the left.

// The combine of the symbolic run.  A combine of left and right takes the step
// that reads left on its left port, when that step reads right on its right
// port, and gives the node of the step.  Each other combine is stray: it adds
// one to strays and gives no_add, which no step reads.  Each shard combines
// with a copy, so the counts are atomic.
struct PlanCombine {
    View<core::TreeStep const> steps;
    View<std::uint32_t const> step_of_left;
    View<Atomic<std::uint32_t>> fired;
    View<Atomic<std::uint32_t>> strays;
    std::uint32_t leaves = 0;

    [[nodiscard]] Symbol operator()(Symbol const& left, Symbol const& right) const noexcept {
        std::uint32_t const held =
            left.node < step_of_left.size()
                ? *step_of_left.get(left.node).expect("a test reads an element at or past the count of its View")
                : 0;
        if (held != 0
            && static_cast<std::uint32_t>(
                   steps.get(held - 1).expect("a test reads an element at or past the count of its View")->right)
                   == right.node) {
            static_cast<void>(fired.get(held - 1)
                                  .expect("a test reads an element at or past the count of its View")
                                  ->fetch_add_acq_rel(1));
            return Symbol{leaves + held - 1};
        }
        static_cast<void>(
            strays.get(0).expect("a test reads an element at or past the count of its View")->fetch_add_acq_rel(1));
        return Symbol{no_add};
    }
};

// reduce_by_tree_in_parallel on the symbols of the run takes each step of the
// tree plan of the run one time, makes no stray combine, and gives the root,
// on each coupling.
[[nodiscard]] bool does_parallel_reduce_follow(View<Symbol const> run) noexcept {
    HeapArray<core::TreeStep> const steps = plan_tree_of(run);
    HeapArray<std::uint32_t> step_of_left =
        ::fixy::mint_heap_array(tree::test_alloc(), run.size() + steps.size(), std::uint32_t{0});
    std::uint32_t step = 0;
    for (core::TreeStep const& one : steps) {
        ++step;
        *step_of_left.view()
             .get(static_cast<std::size_t>(one.left))
             .expect("a test reads an element at or past the count of its View") = step;
    }
    auto const leaves = static_cast<std::uint32_t>(run.size());
    std::uint32_t const root = leaves + step - 1;
    auto const zero = [](std::size_t) noexcept { return Atomic<std::uint32_t>{0}; };
    return is_each_coupling([&]<spawn::BodyCoupling Coupling, std::size_t Shards>() noexcept {
        tree::BgCtx const ctx{tree::eff::testing::bg()};
        HeapArray<Atomic<std::uint32_t>> fired =
            ::foundation::core::mint_heap_array_with<Atomic<std::uint32_t>>(tree::test_alloc(), steps.size(), zero);
        HeapArray<Atomic<std::uint32_t>> strays =
            ::foundation::core::mint_heap_array_with<Atomic<std::uint32_t>>(tree::test_alloc(), 1, zero);
        PlanCombine const combine{View<core::TreeStep const>{steps.view()},
                                  View<std::uint32_t const>{step_of_left.view()}, fired.view(), strays.view(), leaves};
        Symbol const result =
            ::fixy::concurrent::reduce_by_tree_in_parallel<Coupling, Shards>(ctx, tree::test_alloc(), run, combine)
                .expect("the run has leaves");
        bool is_each_fired =
            strays.view().get(0).expect("a test reads an element at or past the count of its View")->load_acquire()
            == 0;
        for (Atomic<std::uint32_t> const& count : fired) {
            is_each_fired = is_each_fired && count.load_acquire() == 1;
        }
        return is_each_fired && result.node == root;
    });
}

[[nodiscard]] bool does_each_parallel_reduce_follow() noexcept {
    HeapArray<Symbol> symbols = ::fixy::mint_heap_array(tree::test_alloc(), kLongest, Symbol{});
    std::uint32_t node = 0;
    for (Symbol& symbol : symbols) {
        symbol.node = node;
        ++node;
    }
    bool is_each = true;
    for (std::size_t const count : kCounts) {
        is_each = is_each
               && does_parallel_reduce_follow(
                      View<Symbol const>{symbols.view()}.window(0, count).expect("the run fits the symbols"));
    }
    return is_each;
}

// ── The bits of the parallel sums and scans ─────────────────────────────

// sum_by_tree_in_parallel gives the bits of the tree plan of the run, on each
// coupling.
template <class T>
[[nodiscard]] bool does_parallel_sum_follow(View<T const> run) noexcept {
    T const from_plan = sum_by_plan(run);
    return is_each_coupling([&]<spawn::BodyCoupling Coupling, std::size_t Shards>() noexcept {
        tree::BgCtx const ctx{tree::eff::testing::bg()};
        return has_same_bits(
            ::fixy::concurrent::sum_by_tree_in_parallel<Coupling, Shards>(ctx, tree::test_alloc(), run), from_plan);
    });
}

// scan_sums_in_parallel of each kind gives the bits of the scan plan of the
// run at each output, on each coupling.
template <class T>
[[nodiscard]] bool does_parallel_scan_follow(core::ScanKind kind, View<T const> run) noexcept {
    HeapArray<T> const from_plan = scan_by_plan(kind, run);
    return is_each_coupling([&]<spawn::BodyCoupling Coupling, std::size_t Shards>() noexcept {
        tree::BgCtx const ctx{tree::eff::testing::bg()};
        HeapArray<T> scanned = ::fixy::mint_heap_array(tree::test_alloc(), run.size(), T{});
        scanned = ::fixy::with_mut(std::move(scanned), [&](auto out) noexcept {
            ::fixy::concurrent::scan_sums_in_parallel<Coupling, Shards>(ctx, tree::test_alloc(), kind, run,
                                                                        std::move(out));
        });
        return has_same_run_bits(View<T const>{scanned.view()}, View<T const>{from_plan.view()});
    });
}

template <class T>
[[nodiscard]] bool does_each_parallel_sum_and_scan_follow(PhiloxStream& stream) noexcept {
    HeapArray<T> const draws = draw_run<T, kLongest>(stream);
    bool is_each = true;
    for (std::size_t const count : kCounts) {
        View<T const> const run = View<T const>{draws.view()}.window(0, count).expect("the run fits the draws");
        is_each = is_each && does_parallel_sum_follow(run) && does_parallel_scan_follow(core::ScanKind::Inclusive, run)
               && does_parallel_scan_follow(core::ScanKind::Exclusive, run);
    }
    return is_each;
}

// ── The bits of the parallel sums of segments ───────────────────────────

inline constexpr std::size_t kSegments = 40;
inline constexpr std::size_t kSegmentElements = kSegments * 2 * kGrain;

// The offsets of kSegments segments with drawn counts: a quarter of them are
// empty, and some hold more than one grain.  The segments fit a run of
// kSegmentElements elements.
[[nodiscard]] HeapArray<std::size_t> draw_segment_offsets(PhiloxStream& stream) noexcept {
    HeapArray<std::size_t> offsets = ::fixy::mint_heap_array(tree::test_alloc(), kSegments + 1, std::size_t{0});
    std::size_t end = 0;
    for (std::size_t& offset : offsets) {
        offset = end;
        end += stream.below(4) == 0 ? 0 : stream.below(2 * kGrain);
    }
    return offsets;
}

// sum_segments_by_tree_in_parallel gives at each segment the bits of the tree
// plan of the segment, and +0 for an empty segment, on each coupling.
[[nodiscard]] bool does_parallel_segment_sum_follow(PhiloxStream& stream) noexcept {
    HeapArray<float> const draws = draw_run<float, kSegmentElements>(stream);
    HeapArray<std::size_t> const offsets = draw_segment_offsets(stream);
    View<std::size_t const> const checked{offsets.view()};
    View<float const> const elements =
        View<float const>{draws.view()}
            .window(0, *checked.get(kSegments).expect("a test reads an element at or past the count of its View"))
            .expect("the segments fit the draws");
    HeapArray<float> from_plan = ::fixy::mint_heap_array(tree::test_alloc(), kSegments, 0.0f);
    for (std::size_t segment = 0; segment < kSegments; ++segment) {
        std::size_t const first =
            *checked.get(segment).expect("a test reads an element at or past the count of its View");
        std::size_t const count =
            *checked.get(segment + 1).expect("a test reads an element at or past the count of its View") - first;
        *from_plan.view().get(segment).expect("a test reads an element at or past the count of its View") =
            count == 0 ? 0.0f : sum_by_plan(elements.window(first, count).expect("a segment of the run"));
    }
    return is_each_coupling([&]<spawn::BodyCoupling Coupling, std::size_t Shards>() noexcept {
        tree::BgCtx const ctx{tree::eff::testing::bg()};
        HeapArray<float> sums = ::fixy::mint_heap_array(tree::test_alloc(), kSegments, 0.0f);
        sums = ::fixy::with_mut(std::move(sums), [&](auto out) noexcept {
            ::fixy::concurrent::sum_segments_by_tree_in_parallel<Coupling, Shards>(ctx, elements, checked,
                                                                                   std::move(out));
        });
        return has_same_run_bits(View<float const>{sums.view()}, View<float const>{from_plan.view()});
    });
}

// ── The golden digests ──────────────────────────────────────────────────
//
// The golden bits test of foundation stays below one grain.  So this part
// holds the golden digest of each parallel form over runs of some grains, by
// the rule of golden_digest.h.  The matches above show that the plan of each
// run gives the same bits, so each digest also holds the bits of the plans.
// The forms run on 8 shards, each on its own thread.

inline constexpr ::fixy::FixedArray<std::size_t, 5> kGoldenCounts{kGrain + 1, 2 * kGrain, 3 * kGrain + 37,
                                                                  5 * kGrain + 1000, kLongest};
inline constexpr spawn::BodyCoupling kGoldenCoupling = spawn::BodyCoupling::WaitOnEachOther;

// Each parallel form gives its golden digest.  Each digest that differs
// prints, so one run shows each of them.
[[nodiscard]] bool are_parallel_digests_golden() noexcept {
    using ::crucible::test::golden::fold_output;
    using ::crucible::test::golden::fold_outputs;
    using ::crucible::test::golden::is_golden;
    using ::crucible::test::golden::kFnvOffset;
    PhiloxStream stream{0x601D0101u};
    HeapArray<float> const floats = draw_run<float, kSegmentElements>(stream);
    HeapArray<std::uint64_t> words = ::fixy::mint_heap_array(tree::test_alloc(), kLongest, std::uint64_t{0});
    for (std::uint64_t& word : words) {
        word = stream.next_wide();
    }
    HeapArray<std::size_t> const offsets = draw_segment_offsets(stream);
    View<float const> const float_view{floats.view()};
    View<std::uint64_t const> const word_view{words.view()};
    tree::BgCtx const ctx{tree::eff::testing::bg()};

    std::uint64_t sums = kFnvOffset;
    std::uint64_t reduces = kFnvOffset;
    for (std::size_t const count : kGoldenCounts) {
        sums = fold_output(sums, ::fixy::concurrent::sum_by_tree_in_parallel<kGoldenCoupling, 8>(
                                     ctx, tree::test_alloc(), float_view.window(0, count).expect("the run fits")));
        reduces = fold_output(reduces, ::fixy::concurrent::reduce_by_tree_in_parallel<kGoldenCoupling, 8>(
                                           ctx, tree::test_alloc(), word_view.window(0, count).expect("the run fits"),
                                           ::crucible::test::golden::Mix{})
                                           .expect("the run has elements"));
    }

    std::uint64_t scans = kFnvOffset;
    View<float const> const scan_run = float_view.window(0, 3 * kGrain + 37).expect("the run fits");
    for (core::ScanKind const kind :
         ::fixy::FixedArray<core::ScanKind, 2>{core::ScanKind::Inclusive, core::ScanKind::Exclusive}) {
        HeapArray<float> scanned = ::fixy::mint_heap_array(tree::test_alloc(), scan_run.size(), 0.0f);
        scanned = ::fixy::with_mut(std::move(scanned), [&](auto out) noexcept {
            ::fixy::concurrent::scan_sums_in_parallel<kGoldenCoupling, 8>(ctx, tree::test_alloc(), kind, scan_run,
                                                                          std::move(out));
        });
        scans = fold_outputs(scans, View<float const>{scanned.view()});
    }

    View<std::size_t const> const checked{offsets.view()};
    HeapArray<float> segment_sums = ::fixy::mint_heap_array(tree::test_alloc(), kSegments, 0.0f);
    segment_sums = ::fixy::with_mut(std::move(segment_sums), [&](auto out) noexcept {
        ::fixy::concurrent::sum_segments_by_tree_in_parallel<kGoldenCoupling, 8>(
            ctx,
            float_view
                .window(0, *checked.get(kSegments).expect("a test reads an element at or past the count of its View"))
                .expect("the segments fit the run"),
            checked, std::move(out));
    });
    std::uint64_t const segments = fold_outputs(kFnvOffset, View<float const>{segment_sums.view()});

    bool const is_sum_golden = is_golden("sum_by_tree_in_parallel of float", sums, 0xF9C1E83A119F9F4Eu);
    bool const is_reduce_golden = is_golden("reduce_by_tree_in_parallel with a mix", reduces, 0x16332C815E486A72u);
    bool const is_scan_golden = is_golden("scan_sums_in_parallel of float", scans, 0x33878626746F0EB7u);
    bool const is_segment_golden =
        is_golden("sum_segments_by_tree_in_parallel of float", segments, 0x2F64B99E0558E387u);
    return is_sum_golden && is_reduce_golden && is_scan_golden && is_segment_golden;
}

}  // namespace

int main() {
    PhiloxStream stream{0x7E570B01u};
    bool const is_reduce = does_each_parallel_reduce_follow();
    bool const is_float = does_each_parallel_sum_and_scan_follow<float>(stream);
    bool const is_double = does_each_parallel_sum_and_scan_follow<double>(stream);
    bool const is_segment = does_parallel_segment_sum_follow(stream);
    bool const is_golden = are_parallel_digests_golden();
    CRUCIBLE_TEST_ASSERT(kTest, is_reduce);
    CRUCIBLE_TEST_ASSERT(kTest, is_float);
    CRUCIBLE_TEST_ASSERT(kTest, is_double);
    CRUCIBLE_TEST_ASSERT(kTest, is_segment);
    CRUCIBLE_TEST_ASSERT(kTest, is_golden);
    return 0;
}
