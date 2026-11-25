// The claims of the slots of the kernel cache (crucible/KernelCache.h).
//
//   - One claim of a slot wins and gives its one writer.  Each other claim
//     gives the content hash that the slot holds.
//   - The row and the kernel of a slot come from the one writer, so a reader
//     finds the kernel of an insert under the row of the same insert.
//   - Two threads that insert one content hash under two rows take two slots,
//     and each lookup finds the kernel of its own row.
//   - Two threads that insert one content hash under one row leave one of the
//     two kernels under that row.
//
// The two-thread cases run under the tsan preset with no suppression.

#include "test_kernels.h"

#include <crucible/KernelCache.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

using crucible::ContentHash;
using crucible::KernelCache;
using crucible::RowHash;
using crucible::test::is_found;
using crucible::test::Kernels;
using crucible::test::make_kernel;
using crucible::test::make_kernel_arena;
using Slot = KernelCache::KernelCacheSlot;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;
constexpr ::foundation::effects::cap::Nondet kNondet = ::foundation::effects::testing::test().nondet;

// The content hash of each case.
constexpr std::uint64_t kContent = 0x5EED;

// The caches of the two-thread cases.
constexpr int kRounds = 400;

// Two kernels of one arena.
struct TwoKernels {
    KernelCache::KernelRef first;
    KernelCache::KernelRef second;
};

[[nodiscard]] TwoKernels make_two_kernels(Kernels& arena CRUCIBLE_LIFETIMEBOUND) noexcept {
    auto const first = make_kernel(arena);
    return TwoKernels{.first = first, .second = make_kernel(arena)};
}
void make_two_kernels(Kernels&&) = delete("a temporary arena releases the kernels at the end of the statement");

void one_claim_of_a_slot_wins() {
    Kernels arena = make_kernel_arena();
    TwoKernels const kernels = make_two_kernels(arena);
    Slot slot{};
    // The claim that won publishes the row and the kernel of the slot.
    bool const is_claimed = slot.claim(ContentHash{kContent})
                                .match(
                                    [&kernels](Slot::WriterHandle writer) noexcept {
                                        std::move(writer).publish(RowHash{3}, kernels.first);
                                        return true;
                                    },
                                    [](ContentHash) noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_claimed);
    // Each later claim gives the content hash that the slot holds, for the
    // same content hash and for another one.
    auto const claim_again = [&slot, &kernels](ContentHash content) noexcept {
        return slot.claim(content).match(
            [&kernels](Slot::WriterHandle writer) noexcept {
                std::move(writer).publish(RowHash{4}, kernels.second);
                return ::fixy::Option<ContentHash>{::fixy::none};
            },
            [](ContentHash held) noexcept { return ::fixy::some(held); });
    };
    CRUCIBLE_TEST_ASSERT(kTest, claim_again(ContentHash{kContent}) == ::fixy::some(ContentHash{kContent}));
    CRUCIBLE_TEST_ASSERT(kTest, claim_again(ContentHash{kContent + 1}) == ::fixy::some(ContentHash{kContent}));
    KernelCache::KernelCacheSlotSnapshot const seen = slot.reader().load();
    CRUCIBLE_TEST_ASSERT(kTest, seen.content_hash == kContent && seen.row_hash == 3);
    CRUCIBLE_TEST_ASSERT(kTest, is_found(seen.kernel, kernels.first));
    ::fixy::check::pass(kTest, "  one_claim_of_a_slot_wins: PASSED\n");
}

// Runs two inserts of one content hash on two threads, for many fresh caches.
// Gives the count of the caches whose lookups are_right accepts.
template <class AreRight>
[[nodiscard]] int count_right_rounds(RowHash first_row, RowHash second_row, TwoKernels const& kernels,
                                     AreRight const& are_right) noexcept {
    int right = 0;
    for (int round = 0; round < kRounds; ++round) {
        KernelCache cache{kAlloc, /*capacity=*/16};
        auto insert_first = [&cache, &kernels, first_row] noexcept {
            return cache.insert(kNondet, ContentHash{kContent}, first_row, kernels.first).is_ok();
        };
        auto insert_second = [&cache, &kernels, second_row] noexcept {
            return cache.insert(kNondet, ContentHash{kContent}, second_row, kernels.second).is_ok();
        };
        ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
        auto const [is_first_placed, is_second_placed] =
            ::fixy::thread::run_joined_raw_probe(bg, kTest, "the two inserts share a cache on purpose",
                                                 ::fixy::thread::ThreadOptions{}, std::move(insert_first),
                                                 std::move(insert_second))
                .expect("the thread door starts the two inserts");
        if (is_first_placed && is_second_placed && are_right(cache)) ++right;
    }
    return right;
}

void two_inserts_of_two_rows_keep_their_pairs() {
    Kernels arena = make_kernel_arena();
    TwoKernels const kernels = make_two_kernels(arena);
    auto const find_each = [&kernels](KernelCache const& cache) noexcept {
        return is_found(cache.lookup(kNondet, ContentHash{kContent}, RowHash{1}), kernels.first)
            && is_found(cache.lookup(kNondet, ContentHash{kContent}, RowHash{2}), kernels.second);
    };
    CRUCIBLE_TEST_ASSERT(kTest, count_right_rounds(RowHash{1}, RowHash{2}, kernels, find_each) == kRounds);
    ::fixy::check::pass(kTest, "  two_inserts_of_two_rows_keep_their_pairs: PASSED\n");
}

void two_inserts_of_one_row_leave_one_kernel() {
    Kernels arena = make_kernel_arena();
    TwoKernels const kernels = make_two_kernels(arena);
    auto const find_one = [&kernels](KernelCache const& cache) noexcept {
        KernelCache::FoundKernel const found = cache.lookup(kNondet, ContentHash{kContent}, RowHash{1});
        return is_found(found, kernels.first) || is_found(found, kernels.second);
    };
    CRUCIBLE_TEST_ASSERT(kTest, count_right_rounds(RowHash{1}, RowHash{1}, kernels, find_one) == kRounds);
    ::fixy::check::pass(kTest, "  two_inserts_of_one_row_leave_one_kernel: PASSED\n");
}

}  // namespace

int main() {
    one_claim_of_a_slot_wins();
    two_inserts_of_two_rows_keep_their_pairs();
    two_inserts_of_one_row_leave_one_kernel();
    ::fixy::check::pass(kTest, "test_kernel_cache_claims: PASSED\n");
    return 0;
}
