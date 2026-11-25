// The inputs of the parallel forms of fixy/concurrent/ParallelTree.h that share
// a byte with what the shards write (the inputs of the shards).  Each form ends
// the process before a shard starts.  The forms with inputs of their own runs
// are in test_concurrent_parallel_tree.cpp and its neighbors.

#include <fixy/Core.h>
#include <fixy/concurrent/ParallelTree.h>
#include <foundation/AlignedBytes.h>
#include <foundation/Quarantine.h>
#include <foundation/alloc/Layout.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Side.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "parallel_tree_test.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace spawn = ::fixy::spawn;
namespace tree = ::crucible::test::parallel_tree;

using ::foundation::core::HeapArray;
using ::foundation::core::View;

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

constexpr std::size_t kThreeGrains = 3 * ::fixy::concurrent::parallel_tree_grain;

// The tag of a region of slots over the run of the elements.
struct SharedSlots {
    using permission_row = tree::eff::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

// The text of the end of the process for the input at position 0 and at
// position 1.
constexpr ::fixy::TextView kFirstInput{
    "mint_parallel_for: the input 0 of reads() shares a byte with the region that the shards write"};
constexpr ::fixy::TextView kSecondInput{
    "mint_parallel_for: the input 1 of reads() shares a byte with the region that the shards write"};

// The elements are a run of three grains, and the partials are its first three
// elements.  With no compare of the two, shard 0 reads the elements 1 and 2
// while the shards 1 and 2 write them.
void sum_into_elements() {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    HeapArray<float> run = ::foundation::core::mint_heap_array(tree::test_alloc(), kThreeGrains, 1.0f);
    run = ::fixy::with_mut(std::move(run), [&ctx](auto out) noexcept {
        View<float const> const elements{out.view()};
        auto [slots, rest] = ::foundation::core::split_at(std::move(out), 3).expect("the cut fits the run");
        static_cast<void>(rest);
        static_cast<void>(::fixy::with_region<SharedSlots>(std::move(slots), [&ctx, elements](auto partials) noexcept {
            auto parts = ::fixy::concurrent::sum_by_tree_in_parallel<spawn::BodyCoupling::WaitOnEachOther, 3>(
                ctx, elements, std::move(partials));
            return std::move(parts.partials);
        }));
    });
}

// The same elements and partials for the reduce with the combine of a caller.
void reduce_into_elements() {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    HeapArray<std::uint64_t> run =
        ::foundation::core::mint_heap_array(tree::test_alloc(), kThreeGrains, std::uint64_t{1});
    run = ::fixy::with_mut(std::move(run), [&ctx](auto out) noexcept {
        View<std::uint64_t const> const elements{out.view()};
        auto [slots, rest] = ::foundation::core::split_at(std::move(out), 3).expect("the cut fits the run");
        static_cast<void>(rest);
        static_cast<void>(::fixy::with_region<SharedSlots>(std::move(slots), [&ctx, elements](auto partials) noexcept {
            auto parts = ::fixy::concurrent::reduce_by_tree_in_parallel<spawn::BodyCoupling::WaitOnEachOther, 3>(
                ctx, elements, std::move(partials),
                [](std::uint64_t const& left, std::uint64_t const& right) noexcept -> std::uint64_t {
                    return left ^ right;
                });
            static_cast<void>(parts.value);
            return std::move(parts.partials);
        }));
    });
}

// The input of a scan is a run of three grains, and the carries are its first
// three elements.  With no compare of the two, the first phase writes the sum
// of each grain over an element of the input, so the scan gives a wrong value
// on each count of threads, and shard 0 reads what the shards 1 and 2 write.
void scan_carries_in_input() {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    HeapArray<float> source = ::foundation::core::mint_heap_array(tree::test_alloc(), kThreeGrains, 1.0f);
    HeapArray<float> scanned = ::foundation::core::mint_heap_array(tree::test_alloc(), kThreeGrains, 0.0f);
    scanned = ::foundation::core::with_mut(std::move(scanned), [&ctx, &source](auto out) noexcept {
        source = ::foundation::core::with_mut(std::move(source), [&ctx, &out](auto run) noexcept {
            View<float const> const input{run.view()};
            auto [slots, rest] = ::foundation::core::split_at(std::move(run), 3).expect("the cut fits the run");
            static_cast<void>(rest);
            static_cast<void>(
                ::fixy::with_region<SharedSlots>(std::move(slots), [&ctx, &out, input](auto carries) noexcept {
                    return ::fixy::concurrent::scan_sums_in_parallel<spawn::BodyCoupling::WaitOnEachOther, 3>(
                        ctx, ::foundation::core::ScanKind::Inclusive, input, std::move(out), std::move(carries));
                }));
        });
    });
}

// The sums are the first two elements of a run of four, and the elements are
// the whole run.  With no compare of the two, shard 0 reads the elements 0
// and 1 for segment 0, and shard 1 writes the sum of segment 1 into element 1.
void sum_segments_into_elements() {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    HeapArray<std::size_t> const offsets = ::foundation::core::mint_heap_array_with<std::size_t>(
        tree::test_alloc(), 3, [](std::size_t index) noexcept { return 2 * index; });
    HeapArray<float> run = ::foundation::core::mint_heap_array(tree::test_alloc(), 4, 1.0f);
    run = ::foundation::core::with_mut(std::move(run), [&ctx, &offsets](auto out) noexcept {
        View<float const> const elements{out.view()};
        auto [sums, rest] = ::foundation::core::split_at(std::move(out), 2).expect("the cut fits the run");
        static_cast<void>(rest);
        ::fixy::concurrent::sum_segments_by_tree_in_parallel<spawn::BodyCoupling::WaitOnEachOther, 2>(
            ctx, elements, View<std::size_t const>{offsets.view()}, std::move(sums));
    });
}

// The offsets are three words over the bytes of a block, and the sums are two
// doubles in a window of the same block: sum s lies in the bytes of the offset
// s + 1.  With no compare of the bytes, shard 0 writes the offset that shard 1
// reads as the start of its segment, although the two runs have different
// element types.  The plugin refuses the offsets in the closure of the frame
// that the block moved into, so the sum is a probe of the run-time compare.
void sum_segments_into_offsets() {
    using ::foundation::core::ByteCount;
    using Start = ::foundation::core::side::WindowStart;
    using Length = ::foundation::core::side::WindowLength;
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    HeapArray<double> const run = ::foundation::core::mint_heap_array(tree::test_alloc(), 4, 1.0);
    View<double const> const elements{run.view()};
    ::fixy::FixedArray<::foundation::ByteWindow, 1> const words{
        ::foundation::ByteWindow{::foundation::core::side_as<Start>(ByteCount{0}),
                                 ::foundation::core::side_as<Length>(ByteCount{3 * sizeof(std::size_t)})}};
    ::fixy::FixedArray<::foundation::ByteWindow, 1> const doubles{
        ::foundation::ByteWindow{::foundation::core::side_as<Start>(ByteCount{sizeof(std::size_t)}),
                                 ::foundation::core::side_as<Length>(ByteCount{2 * sizeof(double)})}};
    ::foundation::AlignedBytes block = ::foundation::mint_aligned_bytes_zeroed(
        tree::test_alloc(),
        ::foundation::alloc::mint_layout(ByteCount{64}, ::foundation::alloc::Alignment{64}).expect("a layout"));
    auto [written, back] =
        ::foundation::with_mut_windows<std::size_t>(std::move(block), words, [](auto offsets) noexcept {
            std::size_t position = 0;
            for (std::size_t& offset : offsets) {
                offset = position;
                position += 2;
            }
        });
    CRUCIBLE_TEST_ASSERT(kTest, written.is_ok());
    View<std::size_t const> const offsets{::foundation::lifetime::start_as_array<std::size_t>(
        std::as_const(back).view().window(0, 3 * sizeof(std::size_t)).expect("the words fit the block"))};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the run-time compare of reads(), which the compile-time borrow rules refuse")
    auto [summed, last] =
        ::foundation::with_mut_windows<double>(std::move(back), doubles, [&ctx, elements, offsets](auto sums) noexcept {
            ::fixy::concurrent::sum_segments_by_tree_in_parallel<spawn::BodyCoupling::WaitOnEachOther, 2>(
                ctx, elements, offsets, std::move(sums));
        });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    static_cast<void>(summed);
    static_cast<void>(last);
}

}  // namespace

int main() {
    ::fixy::check::expect_dies(kTest, sum_into_elements, kFirstInput);
    ::fixy::check::expect_dies(kTest, reduce_into_elements, kFirstInput);
    ::fixy::check::expect_dies(kTest, scan_carries_in_input, kFirstInput);
    ::fixy::check::expect_dies(kTest, sum_segments_into_elements, kFirstInput);
    ::fixy::check::expect_dies(kTest, sum_segments_into_offsets, kSecondInput);
    ::fixy::check::pass(kTest, "test_concurrent_parallel_tree_inputs: all tests passed\n");
    return 0;
}
