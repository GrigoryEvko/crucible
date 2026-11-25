// Tests of foundation/core/RunSet.h: the three states of a run, the shared
// counts, the copy, and the refused write.

#include <foundation/core/RunSet.h>

#include <foundation/Platform.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::ArenaRunSet;
using ::foundation::core::BumpArena;
using ::foundation::core::RunOf;
using ::foundation::core::RunSet;
using ::foundation::core::View;
using Bytes = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;

// The family of the arenas of the runs of the tests.
struct[[= ::foundation::brand::arena_family_brand{}]] RunArena final {};

struct Meta {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

// The shape of the arrays of one traced op: slot 0 counts the inputs, slot
// 1 counts the outputs, and slot 2 counts the scalars.
using OpRuns = RunSet<std::uint16_t, RunOf<std::uint32_t, 1>, RunOf<std::uint32_t, 0>, RunOf<Meta, 0>, RunOf<Meta, 1>,
                      RunOf<std::int64_t, 2>>;

// The shape above for const elements, with each run of an arena of RunArena.
using ArenaOpRuns = ArenaRunSet<std::uint16_t, RunArena, RunOf<std::uint32_t const, 1>, RunOf<std::uint32_t const, 0>,
                                RunOf<Meta const, 0>, RunOf<Meta const, 1>, RunOf<std::int64_t const, 2>>;

// The sum of the elements of a View.
template <class T>
[[nodiscard]] std::uint64_t sum_of(View<T> view) noexcept {
    std::uint64_t total = 0;
    for (T const& element : view) {
        total += static_cast<std::uint64_t>(element);
    }
    return total;
}

// A count above zero with no elements: the run is absent, its View is
// empty, and the count stays.
void test_absent_run_keeps_its_count() {
    OpRuns runs{};
    runs.set_count<0>(3);
    CRUCIBLE_FATAL_INVARIANT(runs.count<0>() == 3);
    CRUCIBLE_FATAL_INVARIANT(!runs.has_run<1>() && !runs.has_run<2>());
    CRUCIBLE_FATAL_INVARIANT(runs.view<1>().size() == 0 && runs.view<2>().size() == 0);
    CRUCIBLE_FATAL_INVARIANT(runs.view<1>().get(0).is_none());
    OpRuns const& read = runs;
    CRUCIBLE_FATAL_INVARIANT(!read.has_run<1>() && read.view<1>().size() == 0);
}

// A count of zero: each run of the slot holds its zero elements, with no
// write and after the write of an empty View.
void test_empty_run_has_its_elements() {
    OpRuns runs{};
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<0>() && runs.has_run<4>());
    CRUCIBLE_FATAL_INVARIANT(runs.view<0>().size() == 0 && runs.count<1>() == 0);
    runs.set<4>(View<std::int64_t>{});
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<4>() && runs.view<4>().size() == 0);
}

// The elements of the count: the View holds them, a write by the owner of the
// run reaches them, and the other run of the slot stays absent.  The set
// itself gives only const elements.
void test_full_run_gives_its_elements() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<RunArena> arena = ::foundation::core::mint_bump_arena<RunArena>(Bytes{4096});
    OpRuns runs{};
    runs.set_count<0>(4);
    runs.set_count<1>(2);
    runs.set_count<2>(1);
    auto outputs = arena.array<std::uint32_t>(alloc, 4, 5);
    auto inputs = arena.array<std::uint32_t>(alloc, 2, 9);
    // A plain View of an arena run: the View carries no brand of its arena.
    runs.set<1>(outputs.view());
    // A plain View of an arena run: the View carries no brand of its arena.
    runs.set<0>(inputs.view());
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<0>() && runs.has_run<1>());
    CRUCIBLE_FATAL_INVARIANT(!runs.has_run<2>() && !runs.has_run<3>() && !runs.has_run<4>());
    CRUCIBLE_FATAL_INVARIANT(runs.view<1>().size() == 4 && sum_of(runs.view<1>()) == 20);
    CRUCIBLE_FATAL_INVARIANT(runs.view<0>().size() == 2 && sum_of(runs.view<0>()) == 18);
    // A plain View of an arena run: the View carries no brand of its arena.
    *outputs.view().get(3).expect("the run holds four elements") = 1;
    OpRuns const& read = runs;
    CRUCIBLE_FATAL_INVARIANT(sum_of(read.view<1>()) == 16);
    CRUCIBLE_FATAL_INVARIANT(*read.view<1>().get(3).expect("the run holds four elements") == 1);
}

// Two runs share one count, and a new count drops the runs of its slot
// only.
void test_new_count_drops_the_runs_of_its_slot() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<RunArena> arena = ::foundation::core::mint_bump_arena<RunArena>(Bytes{4096});
    OpRuns runs{};
    runs.set_count<0>(3);
    runs.set_count<1>(1);
    auto outputs = arena.array<std::uint32_t>(alloc, 3, 1);
    auto output_metas = arena.array(alloc, 3, Meta{.key = 4});
    auto inputs = arena.array<std::uint32_t>(alloc, 1, 6);
    // A plain View of an arena run: the View carries no brand of its arena.
    runs.set<1>(outputs.view());
    // A plain View of an arena run: the View carries no brand of its arena.
    runs.set<2>(output_metas.view());
    // A plain View of an arena run: the View carries no brand of its arena.
    runs.set<0>(inputs.view());
    CRUCIBLE_FATAL_INVARIANT(runs.view<2>().size() == 3 && runs.view<1>().size() == 3);
    runs.set_count<0>(2);
    CRUCIBLE_FATAL_INVARIANT(!runs.has_run<1>() && !runs.has_run<2>());
    CRUCIBLE_FATAL_INVARIANT(runs.view<1>().size() == 0 && runs.view<2>().size() == 0);
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<0>() && sum_of(runs.view<0>()) == 6);
    auto new_outputs = arena.array<std::uint32_t>(alloc, 2, 2);
    // A plain View of an arena run: the View carries no brand of its arena.
    runs.set<1>(new_outputs.view());
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<1>() && sum_of(runs.view<1>()) == 4);
}

// A copy holds the same runs and counts, and a new count of the copy does
// not change the source.
void test_copy_holds_the_same_runs() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<RunArena> arena = ::foundation::core::mint_bump_arena<RunArena>(Bytes{4096});
    OpRuns runs{};
    runs.set_count<2>(2);
    auto scalars = arena.array<std::int64_t>(alloc, 2, -3);
    // A plain View of an arena run: the View carries no brand of its arena.
    runs.set<4>(scalars.view());
    OpRuns copy = runs;
    CRUCIBLE_FATAL_INVARIANT(copy.count<2>() == 2 && sum_of(copy.view<4>()) == sum_of(runs.view<4>()));
    copy.set_count<2>(0);
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<4>() && runs.view<4>().size() == 2);
    OpRuns assigned{};
    assigned = runs;
    CRUCIBLE_FATAL_INVARIANT(assigned.has_run<4>() && assigned.view<4>().size() == 2);
}

// A set of an arena holds the runs that its arena gave: an absent run, a run
// of no elements, a full run, a new count that drops the runs of its slot, and
// a copy of the set in the arena itself.
void test_arena_set_holds_runs_of_its_arena() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<RunArena> arena = ::foundation::core::mint_bump_arena<RunArena>(Bytes{4096});
    ArenaOpRuns runs{};
    runs.set_count<0>(2);
    runs.set_count<1>(3);
    CRUCIBLE_FATAL_INVARIANT(!runs.has_run<1>() && !runs.has_run<0>() && runs.has_run<4>());
    CRUCIBLE_FATAL_INVARIANT(runs.view<1>().size() == 0 && runs.view<4>().size() == 0);
    auto inputs = arena.array<std::uint32_t>(alloc, 2, 9);
    auto outputs = arena.array<std::uint32_t>(alloc, 3, 5);
    runs.set<1>(inputs);
    runs.set<0>(outputs);
    runs.set<2>(arena.array(alloc, 2, Meta{.key = 7}));
    runs.set<4>(arena.array<std::int64_t>(alloc, 0, 0));
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<0>() && runs.has_run<1>() && runs.has_run<2>() && !runs.has_run<3>());
    CRUCIBLE_FATAL_INVARIANT(sum_of(runs.view<0>()) == 15 && sum_of(runs.view<1>()) == 18);
    CRUCIBLE_FATAL_INVARIANT(runs.view<2>().get(1).expect("the run holds two elements")->key == 7);
    // A write by the owner of the run reaches the set.
    *outputs.view().get(0).expect("the run holds three elements") = 1;
    CRUCIBLE_FATAL_INVARIANT(sum_of(runs.view<0>()) == 11);
    auto const held = arena.array(alloc, 1, runs);
    ArenaOpRuns const& copy = *held.view().get(0).expect("the run holds one set");
    runs.set_count<0>(0);
    CRUCIBLE_FATAL_INVARIANT(runs.view<1>().size() == 0 && runs.view<2>().size() == 0 && runs.has_run<0>());
    CRUCIBLE_FATAL_INVARIANT(copy.count<0>() == 2 && sum_of(copy.view<1>()) == 18);
}

// A View of a different count ends the process.  Each set runs in a child
// through expect_dies of the Check family.
void test_wrong_count_ends_the_process(::fixy::TestRunnerCtx const& test) {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<RunArena> arena = ::foundation::core::mint_bump_arena<RunArena>(Bytes{4096});
    OpRuns runs{};
    runs.set_count<0>(3);
    auto short_arena_run = arena.array<std::uint32_t>(alloc, 2, 0);
    // A plain View of an arena run: the View carries no brand of its arena.
    View<std::uint32_t> const short_run = short_arena_run.view();
    ::fixy::check::expect_dies(
        test, [&runs, &short_run] noexcept { runs.set<1>(short_run); },
        ::foundation::core::TextView{
            "foundation: fatal: RunSet::set got a run of 2 elements for a slot of the count 3"});
    auto long_arena_run = arena.array<std::uint32_t>(alloc, 4, 0);
    // A plain View of an arena run: the View carries no brand of its arena.
    View<std::uint32_t> const long_run = long_arena_run.view();
    ::fixy::check::expect_dies(
        test, [&runs, &long_run] noexcept { runs.set<1>(long_run); },
        ::foundation::core::TextView{
            "foundation: fatal: RunSet::set got a run of 4 elements for a slot of the count 3"});
    ::fixy::check::expect_dies(
        test, [&runs] noexcept { runs.set<1>(View<std::uint32_t>{}); },
        ::foundation::core::TextView{
            "foundation: fatal: RunSet::set got a run of 0 elements for a slot of the count 3"});
    CRUCIBLE_FATAL_INVARIANT(!runs.has_run<1>() && runs.count<0>() == 3);
    ArenaOpRuns arena_runs{};
    arena_runs.set_count<0>(3);
    ::fixy::check::expect_dies(
        test, [&arena_runs, &short_arena_run] noexcept { arena_runs.set<1>(short_arena_run); },
        ::foundation::core::TextView{
            "foundation: fatal: RunSet::set got a run of 2 elements for a slot of the count 3"});
    CRUCIBLE_FATAL_INVARIANT(!arena_runs.has_run<1>() && arena_runs.count<0>() == 3);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_absent_run_keeps_its_count();
    test_empty_run_has_its_elements();
    test_full_run_gives_its_elements();
    test_new_count_drops_the_runs_of_its_slot();
    test_copy_holds_the_same_runs();
    test_arena_set_holds_runs_of_its_arena();
    test_wrong_count_ends_the_process(test);
    return 0;
}
