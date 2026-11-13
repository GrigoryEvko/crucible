// Tests of foundation/core/RunSet.h: the three states of a run, the shared
// counts, the copy, and the refused write.

#include <foundation/core/RunSet.h>

#include <foundation/Platform.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include "abort_probe.h"

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::BumpArena;
using ::foundation::core::RunOf;
using ::foundation::core::RunSet;
using ::foundation::core::View;

struct Meta {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

// The shape of the arrays of one traced op: slot 0 counts the inputs, slot
// 1 counts the outputs, and slot 2 counts the scalars.
using OpRuns = RunSet<std::uint16_t, RunOf<std::uint32_t, 1>, RunOf<std::uint32_t, 0>, RunOf<Meta, 0>, RunOf<Meta, 1>,
                      RunOf<std::int64_t, 2>>;

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

// The elements of the count: the View holds them, a write through the View
// reaches them, and the other run of the slot stays absent.
void test_full_run_gives_its_elements() {
    ::foundation::effects::Alloc const alloc{};
    BumpArena arena{4096};
    OpRuns runs{};
    runs.set_count<0>(4);
    runs.set_count<1>(2);
    runs.set_count<2>(1);
    runs.set<1>(arena.array<std::uint32_t>(alloc, 4, 5));
    runs.set<0>(arena.array<std::uint32_t>(alloc, 2, 9));
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<0>() && runs.has_run<1>());
    CRUCIBLE_FATAL_INVARIANT(!runs.has_run<2>() && !runs.has_run<3>() && !runs.has_run<4>());
    CRUCIBLE_FATAL_INVARIANT(runs.view<1>().size() == 4 && sum_of(runs.view<1>()) == 20);
    CRUCIBLE_FATAL_INVARIANT(runs.view<0>().size() == 2 && sum_of(runs.view<0>()) == 18);
    *runs.view<1>().get(3).expect("the run holds four elements") = 1;
    OpRuns const& read = runs;
    CRUCIBLE_FATAL_INVARIANT(sum_of(read.view<1>()) == 16);
    CRUCIBLE_FATAL_INVARIANT(*read.view<1>().get(3).expect("the run holds four elements") == 1);
}

// Two runs share one count, and a new count drops the runs of its slot
// only.
void test_new_count_drops_the_runs_of_its_slot() {
    ::foundation::effects::Alloc const alloc{};
    BumpArena arena{4096};
    OpRuns runs{};
    runs.set_count<0>(3);
    runs.set_count<1>(1);
    runs.set<1>(arena.array<std::uint32_t>(alloc, 3, 1));
    runs.set<2>(arena.array(alloc, 3, Meta{.key = 4}));
    runs.set<0>(arena.array<std::uint32_t>(alloc, 1, 6));
    CRUCIBLE_FATAL_INVARIANT(runs.view<2>().size() == 3 && runs.view<1>().size() == 3);
    runs.set_count<0>(2);
    CRUCIBLE_FATAL_INVARIANT(!runs.has_run<1>() && !runs.has_run<2>());
    CRUCIBLE_FATAL_INVARIANT(runs.view<1>().size() == 0 && runs.view<2>().size() == 0);
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<0>() && sum_of(runs.view<0>()) == 6);
    runs.set<1>(arena.array<std::uint32_t>(alloc, 2, 2));
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<1>() && sum_of(runs.view<1>()) == 4);
}

// A copy holds the same runs and counts, and a new count of the copy does
// not change the source.
void test_copy_holds_the_same_runs() {
    ::foundation::effects::Alloc const alloc{};
    BumpArena arena{4096};
    OpRuns runs{};
    runs.set_count<2>(2);
    runs.set<4>(arena.array<std::int64_t>(alloc, 2, -3));
    OpRuns copy = runs;
    CRUCIBLE_FATAL_INVARIANT(copy.count<2>() == 2 && sum_of(copy.view<4>()) == sum_of(runs.view<4>()));
    copy.set_count<2>(0);
    CRUCIBLE_FATAL_INVARIANT(runs.has_run<4>() && runs.view<4>().size() == 2);
    OpRuns assigned{};
    assigned = runs;
    CRUCIBLE_FATAL_INVARIANT(assigned.has_run<4>() && assigned.view<4>().size() == 2);
}

// A View of a different count ends the process, and the run keeps its
// state.
void test_wrong_count_ends_the_process() {
    ::foundation::effects::Alloc const alloc{};
    BumpArena arena{4096};
    OpRuns runs{};
    runs.set_count<0>(3);
    View<std::uint32_t> const short_run = arena.array<std::uint32_t>(alloc, 2, 0);
    bool const short_run_aborts = ::crucible::test::aborts([&] { runs.set<1>(short_run); });
    CRUCIBLE_FATAL_INVARIANT(short_run_aborts);
    View<std::uint32_t> const long_run = arena.array<std::uint32_t>(alloc, 4, 0);
    bool const long_run_aborts = ::crucible::test::aborts([&] { runs.set<1>(long_run); });
    CRUCIBLE_FATAL_INVARIANT(long_run_aborts);
    bool const empty_run_aborts = ::crucible::test::aborts([&] { runs.set<1>(View<std::uint32_t>{}); });
    CRUCIBLE_FATAL_INVARIANT(empty_run_aborts);
    CRUCIBLE_FATAL_INVARIANT(!runs.has_run<1>() && runs.count<0>() == 3);
}

}  // namespace

int main() {
    test_absent_run_keeps_its_count();
    test_empty_run_has_its_elements();
    test_full_run_gives_its_elements();
    test_new_count_drops_the_runs_of_its_slot();
    test_copy_holds_the_same_runs();
    test_wrong_count_ends_the_process();
    return 0;
}
