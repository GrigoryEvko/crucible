// The inputs of mint_parallel_for (reads() of fixy/concurrent/Spawn.h), run for
// real on one thread for each shard.  Each family of the inputs gives a body
// the run of its elements.  An input that shares a byte with the region ends
// the process before a shard starts, and an input that only touches the region
// passes.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/OwnedRegion.h>
#include <fixy/concurrent/Spawn.h>
#include <foundation/Linear.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/core/Region.h>
#include <foundation/core/Strided.h>

#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include "parallel_loop_rig.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace core = ::foundation::core;
namespace eff = ::foundation::effects;
namespace spawn = ::fixy::spawn;

using ::fixy::HeapArray;
using ::fixy::View;

using Word = std::uint64_t;

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{eff::testing::test()};

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// The tag of the region that the shards write.
struct Written {
    using permission_row = eff::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

// The tag of a region that the shards read.
struct Apart {
    using permission_row = eff::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

// The tag of a region of const elements.
struct ReadOnly {
    using permission_row = eff::Row<>;
};

constexpr std::size_t kCount = 4099;
constexpr std::size_t kShards = 4;

// The text of the end of the process for the input at position 0.
constexpr ::fixy::TextView kSharedFirst{
    "mint_parallel_for: the input 0 of reads() shares a byte with the region that the shards write"};

[[nodiscard]] eff::Alloc test_alloc() noexcept { return eff::testing::test().alloc; }

// The elements offset, offset + 1, and so on.
[[nodiscard]] HeapArray<Word> count_from(Word offset) noexcept {
    return ::fixy::mint_heap_array_with<Word>(test_alloc(), kCount,
                                              [offset](std::size_t position) noexcept { return offset + position; });
}

// The same elements in an inline array.
[[nodiscard]] ::fixy::FixedArray<Word, kCount> fixed_from(Word offset) noexcept {
    return ::fixy::build_fixed_array<Word, kCount>([offset](auto run) noexcept {
        Word next = offset;
        for (Word& element : run) {
            element = next;
            ++next;
        }
    });
}

// The element of a run at a position that the caller knows to be inside it.
[[nodiscard]] Word element_of(View<Word const> run, std::size_t position) noexcept {
    return *run.get(position).expect("a test reads an element below the count of its View");
}

// The sum of the elements of a run, and of a strided view.
[[nodiscard]] Word sum_of(View<Word const> run) noexcept {
    Word sum = 0;
    for (Word const element : run) {
        sum += element;
    }
    return sum;
}

template <std::size_t Rank>
[[nodiscard]] Word sum_of(core::StridedView<Word const, Rank> const& view) noexcept {
    Word sum = 0;
    core::walk([&sum](Word const& element) noexcept { sum += element; }, view).expect("a walk of one view has a shape");
    return sum;
}

// True when the element at each position is first + step * position.
[[nodiscard]] bool is_arithmetic(View<Word const> run, Word first, Word step) noexcept {
    Word expected = first;
    for (Word const element : run) {
        if (element != expected) return false;
        expected += step;
    }
    return true;
}

// Runs the body over the shards of the region with the inputs, on one thread
// for each shard.
template <class Region, class Body, class... Inputs>
[[nodiscard]] Region loop_over(Region region, Body body, Inputs const&... inputs) noexcept {
    BgCtx const ctx{eff::testing::bg()};
    return spawn::mint_parallel_for<spawn::BodyCoupling::WaitOnEachOther, kShards>(
        ctx, ::fixy::concurrent::WorkBudget{.read_bytes = 64}, std::move(region), spawn::reads(inputs...), body);
}

// ── The families of the inputs ──────────────────────────────────────────

// The body writes the sum of the elements of five inputs at its position.
struct SumsFiveInputs {
    template <class Shard, class First, class Second, class Third, class Fourth, class Fifth>
    void operator()(Shard shard, spawn::ShardPlace<kShards> place, First first, Second second, Third third,
                    Fourth fourth, Fifth fifth) const noexcept {
        static_assert(std::is_same_v<First, View<Word const>> && std::is_same_v<Second, View<Word const>>);
        static_assert(std::is_same_v<Third, View<Word const, kCount>>);
        static_assert(std::is_same_v<Fourth, View<Word const>> && std::is_same_v<Fifth, View<Word const>>);
        std::size_t position = place.first();
        for (Word& element : shard) {
            element = element_of(first, position) + element_of(second, position) + element_of(third, position)
                    + element_of(fourth, position) + element_of(fifth, position);
            ++position;
        }
    }
};

// A View, an owner on the heap, an inline owner, a unique view and a region of
// another tag each give their run.  The five offsets sum to 11111, and each
// input adds its position, so the element at the position p is 11111 + 5 p.
[[nodiscard]] bool lends_each_input_with_a_view() {
    HeapArray<Word> const viewed = count_from(1);
    HeapArray<Word> const owner = count_from(10);
    ::fixy::FixedArray<Word, kCount> const fixed = fixed_from(100);
    HeapArray<Word> unique = count_from(1000);
    HeapArray<Word> apart = count_from(10000);
    HeapArray<Word> written = count_from(0);
    View<Word const> const view{viewed.view()};
    auto const over_written = [&owner, &fixed, &written, view](auto const& lent, auto const& read) noexcept {
        written = ::fixy::with_region<Written>(
            std::move(written), [&owner, &fixed, &lent, &read, view](auto region) noexcept {
                return loop_over(std::move(region), SumsFiveInputs{}, view, owner, fixed, lent, read);
            });
    };
    unique = ::fixy::with_mut(std::move(unique), [&apart, &over_written](auto lent) noexcept {
        apart = ::fixy::with_region<Apart>(std::move(apart), [&over_written, &lent](auto read) noexcept {
            over_written(lent, read);
            return read;
        });
    });
    return is_arithmetic(View<Word const>{written.view()}, 11111, 5);
}

// The body writes the sum of the elements of one input.
struct SumsOneInput {
    template <class Shard, class Input>
    void operator()(Shard shard, spawn::ShardPlace<kShards>, Input input) const noexcept {
        static_assert(std::is_same_v<Input, View<Word const>>);
        Word const sum = sum_of(input);
        for (Word& element : shard) {
            element = sum;
        }
    }
};

// A masked array gives its run: 8 elements of 5.
[[nodiscard]] bool lends_a_masked_array() {
    ::fixy::MaskedArray<Word> masked = ::fixy::mint_masked_array<Word>(test_alloc(), 8);
    masked = ::fixy::with_mut(std::move(masked), [](auto run) noexcept { ::fixy::fill(std::move(run), Word{5}); });
    HeapArray<Word> written = count_from(0);
    written = ::fixy::with_region<Written>(std::move(written), [&masked](auto region) noexcept {
        return loop_over(std::move(region), SumsOneInput{}, masked);
    });
    return is_arithmetic(View<Word const>{written.view()}, 40, 0);
}

// The body writes the sum of two strided inputs.
struct SumsTwoStridedInputs {
    template <class Shard, class First, class Second>
    void operator()(Shard shard, spawn::ShardPlace<kShards>, First first, Second second) const noexcept {
        static_assert(std::is_same_v<First, core::StridedView<Word const, 1>>);
        static_assert(std::is_same_v<Second, core::StridedView<Word const, 1>>);
        Word const sum = sum_of(first) + sum_of(second);
        for (Word& element : shard) {
            element = sum;
        }
    }
};

// A strided view and a unique strided view each give a strided view of const
// elements: the positions 0, 3, 6 and so on to 297, with the sum 14850.
[[nodiscard]] bool lends_each_strided_input() {
    HeapArray<Word> const viewed = count_from(0);
    HeapArray<Word> unique = count_from(0);
    HeapArray<Word> written = count_from(0);
    ::fixy::FixedArray<std::size_t, 1> const sizes{100};
    ::fixy::FixedArray<std::size_t, 1> const strides{3};
    auto const strided = ::fixy::mint_strided_view(View<Word const>{viewed.view()}, sizes.view(), strides.view(), 0)
                             .expect("the view fits the run");
    unique = ::fixy::with_mut(std::move(unique), [&sizes, &strides, &written, strided](auto lent) noexcept {
        auto const own = ::fixy::mint_strided_view_mut(std::move(lent), sizes.view(), strides.view(), 0)
                             .expect("the view fits the run");
        written = ::fixy::with_region<Written>(std::move(written), [&own, strided](auto region) noexcept {
            return loop_over(std::move(region), SumsTwoStridedInputs{}, strided, own);
        });
    });
    return is_arithmetic(View<Word const>{written.view()}, 2 * 14850, 0);
}

// The body copies the first input into its shard.
struct CopiesTheFirstInput {
    template <class Shard, class First, class... Rest>
    void operator()(Shard shard, spawn::ShardPlace<kShards> place, First first, Rest... rest) const noexcept {
        std::size_t position = place.first();
        for (Word& element : shard) {
            element = element_of(first, position) + (Word{0} + ... + rest.size());
            ++position;
        }
    }
};

// An input beside the region passes: the region is the first half of a run,
// and the input is the second half, whose first byte follows the last byte of
// the region.  An input with no element passes too.
[[nodiscard]] bool admits_an_input_that_touches_the_region() {
    HeapArray<Word> run = count_from(0);
    run = ::fixy::with_mut(std::move(run), [](auto lent) noexcept {
        auto [head, tail] = core::split_at(std::move(lent), kCount / 2).expect("the cut fits the run");
        static_cast<void>(::fixy::with_region<Written>(std::move(head), [&tail](auto region) noexcept {
            return loop_over(std::move(region), CopiesTheFirstInput{}, tail, View<Word const>{});
        }));
    });
    View<Word const> const all{run.view()};
    View<Word const> const head = all.window(0, kCount / 2).expect("the half fits");
    View<Word const> const tail = all.window(kCount / 2, kCount - kCount / 2).expect("the half fits");
    return is_arithmetic(head, kCount / 2, 1) && is_arithmetic(tail, kCount / 2, 1);
}

// The arena of a region of const elements over a run that the test holds.
class HeldRun {
    View<Word const> run_{};

public:
    explicit HeldRun(View<Word const> run) noexcept : run_{run} {}

    template <typename T>
        requires std::is_same_v<T, Word const>
    [[nodiscard]] View<Word const> alloc_array(eff::Alloc /*alloc*/, std::size_t count) noexcept {
        CRUCIBLE_FATAL_INVARIANT(count == run_.size());
        return run_;
    }
};

// The shards of a region of const elements write no byte, so an input can be
// the run of the region.  Each shard counts the elements of its run that equal
// the element of the input at their position.
[[nodiscard]] bool admits_the_run_of_a_region_of_const_elements() {
    HeapArray<Word> const run = count_from(7);
    View<Word const> const view{run.view()};
    HeldRun arena{view};
    ::fixy::Atomic<Word> equal{0};
    auto region = ::fixy::mint_owned_region<Word const>(test_alloc(), arena, kCount,
                                                        ::foundation::permissions::mint_permission_root<ReadOnly>());
    auto const count_equal = [&equal](auto shard, auto place, auto same) noexcept {
        std::size_t position = place.first();
        Word count = 0;
        for (Word const element : shard) {
            count += element == element_of(same, position) ? Word{1} : Word{0};
            ++position;
        }
        static_cast<void>(equal.fetch_add_acq_rel(count));
    };
    ::foundation::linear::drop(loop_over(std::move(region), count_equal, view));
    return equal.load_acquire() == kCount;
}

// A region of owners passes when the loop reads no compared input: each shard
// writes the objects of its own Boxes, and an input that is a region of another
// tag names none of them.
[[nodiscard]] bool admits_a_region_of_boxes() {
    auto const alloc = test_alloc();
    HeapArray<::fixy::Box<Word>> boxes = ::fixy::mint_heap_array_with<::fixy::Box<Word>>(
        alloc, kCount, [alloc](std::size_t) noexcept { return ::fixy::mint_box<Word>(alloc, Word{0}); });
    HeapArray<Word> apart = count_from(3);
    auto const over_boxes = [&boxes](auto const& read) noexcept {
        boxes = ::fixy::with_region<Written>(std::move(boxes), [&read](auto region) noexcept {
            return loop_over(
                std::move(region),
                [](auto shard, auto place, auto run) noexcept {
                    std::size_t position = place.first();
                    for (::fixy::Box<Word>& box : shard) {
                        *box = element_of(run, position);
                        ++position;
                    }
                },
                read);
        });
    };
    apart = ::fixy::with_region<Apart>(std::move(apart), [&over_boxes](auto read) noexcept {
        over_boxes(read);
        return read;
    });
    Word expected = 3;
    for (::fixy::Box<Word> const& box : boxes) {
        if (*box != expected) return false;
        ++expected;
    }
    return true;
}

// ── The inputs that share a byte with the region ────────────────────────

// The body reads each element of the input and writes each element of its
// shard.  With no compare, a shard reads an element that another shard writes.
struct ReadsAndWrites {
    template <class Shard, class Input>
    void operator()(Shard shard, spawn::ShardPlace<kShards>, Input input) const noexcept {
        Word const sum = sum_of(input);
        for (Word& element : shard) {
            element = sum;
        }
    }
};

// The loop over the region with one input.
template <class Region, class Input>
[[nodiscard]] Region read_beside_the_shards(Region region, Input const& input) noexcept {
    return loop_over(std::move(region), ReadsAndWrites{}, input);
}

// The plugin refuses a borrow of an owner in the closure of a door that the
// owner moved into, when the borrow and the move are in one function.  That
// rule is the answer at compile time.  The tests of an owner below are probes
// of the run-time compare of reads(), the backstop for a borrow that the rules
// do not see.

// The View of the owner that became the region.
void read_the_owner_of_the_region() {
    HeapArray<Word> items = count_from(0);
    View<Word const> const view{items.view()};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the run-time compare of reads(), which the compile-time borrow rules refuse")
    items = ::fixy::with_region<Written>(
        std::move(items), [view](auto region) noexcept { return read_beside_the_shards(std::move(region), view); });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// The last element of the owner that became the region: the shared bytes are
// the last ones of the two runs.
void read_the_last_element_of_the_region() {
    HeapArray<Word> items = count_from(0);
    View<Word const> const last = View<Word const>{items.view()}.window(kCount - 1, 1).expect("the run has it");
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the run-time compare of reads(), which the compile-time borrow rules refuse")
    items = ::fixy::with_region<Written>(
        std::move(items), [last](auto region) noexcept { return read_beside_the_shards(std::move(region), last); });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// The borrow of the region from before the region moves into the loop.
void read_a_borrow_of_the_region() {
    HeapArray<Word> items = count_from(0);
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the run-time compare of reads(), which the compile-time borrow rules refuse")
    items = ::fixy::with_region<Written>(std::move(items), [](auto region) noexcept {
        View<Word const> const borrow = ::fixy::mint_borrowed(region);
        return read_beside_the_shards(std::move(region), borrow);
    });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// The keyed owner whose frame lends the run of the region.
void read_the_keyed_owner_of_the_region() {
    auto [array, array_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<Word, kCount>);
    array.with_mut(array_key, [&owner = std::as_const(array)](auto lent) noexcept {
        static_cast<void>(::fixy::with_region<Written>(std::move(lent), [&owner](auto region) noexcept {
            return read_beside_the_shards(std::move(region), owner);
        }));
    });
}

// A strided view of every second element of the region.
void read_a_strided_view_of_the_region() {
    HeapArray<Word> items = count_from(0);
    ::fixy::FixedArray<std::size_t, 1> const sizes{kCount / 2};
    ::fixy::FixedArray<std::size_t, 1> const strides{2};
    auto const strided = ::fixy::mint_strided_view(View<Word const>{items.view()}, sizes.view(), strides.view(), 1)
                             .expect("the view fits the run");
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the run-time compare of reads(), which the compile-time borrow rules refuse")
    items = ::fixy::with_region<Written>(std::move(items), [&strided](auto region) noexcept {
        return read_beside_the_shards(std::move(region), strided);
    });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// The old name of the ViewMut that moved into the region: the move leaves the
// old name with no element, so the loop reads none and each shard writes the
// sum 0.  Two parameters that name one ViewMut hide the use from the rule
// use-after-move.  The kind two-names of the rule view-escape refuses the call
// that gives one unique view to the two parameters, so the call stands in a
// PROBE region.
template <class Lent>
void read_the_old_name(Lent& lent, Lent const& old_name) noexcept {
    static_cast<void>(::fixy::with_region<Written>(std::move(lent), [&old_name](auto region) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, old_name.size() == 0);
        return read_beside_the_shards(std::move(region), old_name);
    }));
}

[[nodiscard]] bool reads_nothing_through_the_old_name() {
    HeapArray<Word> items = count_from(1);
    items = ::fixy::with_mut(std::move(items), [](auto lent) noexcept {
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
        read_the_old_name(lent, lent);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    });
    bool is_each_zero = true;
    for (Word const element : items.view()) {
        is_each_zero = is_each_zero && element == 0;
    }
    return is_each_zero;
}

void test_inputs_that_share_the_region() {
    ::fixy::check::expect_dies(kTest, read_the_owner_of_the_region, kSharedFirst);
    ::fixy::check::expect_dies(kTest, read_the_last_element_of_the_region, kSharedFirst);
    ::fixy::check::expect_dies(kTest, read_a_borrow_of_the_region, kSharedFirst);
    ::fixy::check::expect_dies(kTest, read_the_keyed_owner_of_the_region, kSharedFirst);
    ::fixy::check::expect_dies(kTest, read_a_strided_view_of_the_region, kSharedFirst);
    CRUCIBLE_TEST_ASSERT(kTest, reads_nothing_through_the_old_name());
}

// ── The parts that the fixtures share ───────────────────────────────────

// The loop of the fixtures compiles and runs with a body that holds nothing, so
// a fixture fails for its own reason.  The body writes the sum of the 64 ones of
// the input.
[[nodiscard]] bool runs_the_loop_of_the_fixtures() {
    HeapArray<Word> const input = ::parallel_loop_rig::make_items();
    HeapArray<Word> items = ::parallel_loop_rig::make_items();
    using RigWritten = ::parallel_loop_rig::Written;
    items = ::fixy::with_region<RigWritten>(std::move(items), [&input](auto region) noexcept -> decltype(region) {
        return ::parallel_loop_rig::loop_over(
            std::move(region),
            [](auto shard, auto, auto run) noexcept {
                for (Word& element : shard) {
                    element = sum_of(run);
                }
            },
            input);
    });
    return is_arithmetic(View<Word const>{items.view()}, ::parallel_loop_rig::kCount, 0);
}

}  // namespace

int main() {
    test_inputs_that_share_the_region();
    CRUCIBLE_TEST_ASSERT(kTest, runs_the_loop_of_the_fixtures());
    CRUCIBLE_TEST_ASSERT(kTest, lends_each_input_with_a_view());
    CRUCIBLE_TEST_ASSERT(kTest, lends_a_masked_array());
    CRUCIBLE_TEST_ASSERT(kTest, lends_each_strided_input());
    CRUCIBLE_TEST_ASSERT(kTest, admits_an_input_that_touches_the_region());
    CRUCIBLE_TEST_ASSERT(kTest, admits_the_run_of_a_region_of_const_elements());
    CRUCIBLE_TEST_ASSERT(kTest, admits_a_region_of_boxes());
    ::fixy::check::pass(kTest, "test_concurrent_parallel_reads: all tests passed\n");
    return 0;
}
