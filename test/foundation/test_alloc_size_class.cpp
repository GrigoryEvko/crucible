// The run-time path of the size classes of foundation/alloc/Layout.h against
// the slots of the classes.
//
// For each size from 1 byte to 2 MiB, and for each alignment from 1 byte to
// 4 KiB, the class of class_for at run time is the smallest class whose slot
// holds the size and is a whole count of the alignment.  The reference walks
// the 64 slots in order.  The check file of the header holds the same facts
// at each bound of a class, at compile time.
//
// The slot of an offset of foundation/alloc/Metadata.h gives the slot of a
// division at each offset of a small slab and at each bound of each class.

#include <foundation/alloc/Layout.h>
#include <foundation/alloc/Metadata.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace alloc = ::foundation::alloc;

using ::fixy::TestRunnerCtx;
using ::foundation::core::ByteCount;

// The smallest class whose slot holds bytes and is a whole count of the
// alignment, from a walk of the slots, or 0 for none.  Complexity: O(64).
[[nodiscard]] std::uint8_t reference_class(std::size_t bytes, std::size_t alignment) noexcept {
    for (std::uint8_t value = 1; value <= alloc::kSizeClassCount; ++value) {
        std::size_t const slot = alloc::slot_bytes(alloc::SizeClass{value}).raw();
        if (slot < bytes) continue;
        if (slot % alignment != 0) continue;
        return value;
    }
    return 0;
}

// The class of class_for for a layout of bytes at the alignment, or 0 for
// none.  The bytes come from the loop of the caller, so the class comes from
// the run-time path.
[[nodiscard, gnu::noinline]] std::uint8_t run_time_class(std::size_t bytes, std::size_t alignment) noexcept {
    alloc::Layout const layout =
        alloc::mint_layout(ByteCount{bytes}, alloc::Alignment{alignment}).expect("the test gives a layout");
    return static_cast<std::uint8_t>(alloc::class_for(layout).value_or(alloc::SizeClass{0}));
}

// Each size to 2 MiB at the alignment of malloc.  Complexity: O(2^21 * 64).
void test_each_size_has_its_class(TestRunnerCtx const& test) {
    std::size_t mismatches = 0;
    for (std::size_t bytes = 1; bytes <= alloc::kLargestSlotBytes; ++bytes) {
        if (run_time_class(bytes, 16) != reference_class(bytes, 16)) ++mismatches;
    }
    CRUCIBLE_TEST_ASSERT(test, mismatches == 0);
    CRUCIBLE_TEST_ASSERT(test, run_time_class(alloc::kLargestSlotBytes + 1, 16) == 0);
}

// The sizes around each bound of a class, at each alignment from 1 byte to
// 4 KiB.  Complexity: O(13 * 64 * 3 * 64).
void test_each_alignment_has_its_class(TestRunnerCtx const& test) {
    std::size_t mismatches = 0;
    for (std::size_t alignment = 1; alignment <= 4096; alignment *= 2) {
        for (std::uint8_t value = 1; value <= alloc::kSizeClassCount; ++value) {
            std::size_t const slot = alloc::slot_bytes(alloc::SizeClass{value}).raw();
            // The byte below the slot, the slot, and the byte above it.  Each
            // slot is at least 16 bytes, so the byte below is above zero.
            for (std::size_t step = 0; step < 3; ++step) {
                std::size_t const bytes = slot + step - 1;
                if (bytes > alloc::kLargestSlotBytes) continue;
                if (run_time_class(bytes, alignment) != reference_class(bytes, alignment)) ++mismatches;
            }
        }
    }
    CRUCIBLE_TEST_ASSERT(test, mismatches == 0);
}

// The value of a slot that names no slot.
constexpr std::size_t kNoSlot = ~std::size_t{0};

// The slot of slot_at, or kNoSlot for none.  The offset comes from the loop
// of the caller, so the slot comes from the run-time path.
[[nodiscard, gnu::noinline]] std::size_t run_time_slot(std::uint8_t value, std::size_t offset) noexcept {
    ::foundation::core::Option<alloc::SlabSlotIndex> const slot =
        alloc::slot_at(alloc::SizeClass{value}, ByteCount{offset});
    if (slot.is_none()) return kNoSlot;
    return ::foundation::core::Option<alloc::SlabSlotIndex>{slot}
        .value_or(alloc::SlabSlotIndex{::foundation::core::StaticIndex<0>{}})
        .raw();
}

// The slot by a division, the oracle outside the reciprocal under test.
[[nodiscard]] std::size_t reference_slot(std::uint8_t value, std::size_t offset) noexcept {
    std::size_t const slot = alloc::slot_bytes(alloc::SizeClass{value}).raw();
    if (offset >= alloc::kSlabChunkBytes) return kNoSlot;
    if (offset % slot != 0) return kNoSlot;
    if (offset / slot >= alloc::slab_slots(alloc::SizeClass{value})) return kNoSlot;
    return offset / slot;
}

// THE SLOT OF AN OFFSET of foundation/alloc/Metadata.h: each offset of the
// slab of each small class, and for each class each bound of a slot, the
// byte below it and the byte above it, up to the first bound past the slots.
// Complexity: O(32 * 2^16 + 64 * 4097 * 3).
void test_each_offset_has_its_slot(TestRunnerCtx const& test) {
    std::size_t mismatches = 0;
    for (std::uint8_t value = 1; value <= alloc::kLastSmallClass; ++value) {
        for (std::size_t offset = 0; offset < alloc::kSlabUnitBytes; ++offset) {
            if (run_time_slot(value, offset) != reference_slot(value, offset)) ++mismatches;
        }
    }
    for (std::uint8_t value = 1; value <= alloc::kSizeClassCount; ++value) {
        std::size_t const slot = alloc::slot_bytes(alloc::SizeClass{value}).raw();
        std::size_t const slots = alloc::slab_slots(alloc::SizeClass{value});
        for (std::size_t index = 0; index <= slots; ++index) {
            std::size_t const bound = index * slot;
            if (bound != 0 && run_time_slot(value, bound - 1) != reference_slot(value, bound - 1)) ++mismatches;
            if (run_time_slot(value, bound) != reference_slot(value, bound)) ++mismatches;
            if (run_time_slot(value, bound + 1) != reference_slot(value, bound + 1)) ++mismatches;
        }
    }
    CRUCIBLE_TEST_ASSERT(test, mismatches == 0);
    CRUCIBLE_TEST_ASSERT(test, run_time_slot(1, 0) == 0);
    CRUCIBLE_TEST_ASSERT(test, run_time_slot(1, alloc::kSlabUnitBytes - 16) == 4095);
    CRUCIBLE_TEST_ASSERT(test, run_time_slot(9, 160) == 1);
    CRUCIBLE_TEST_ASSERT(test, run_time_slot(9, 161) == kNoSlot);
    CRUCIBLE_TEST_ASSERT(test, run_time_slot(alloc::kSizeClassCount, 0) == 0);
    CRUCIBLE_TEST_ASSERT(test, run_time_slot(alloc::kSizeClassCount, alloc::kSlabChunkBytes) == kNoSlot);
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_each_size_has_its_class(test);
    test_each_alignment_has_its_class(test);
    test_each_offset_has_its_slot(test);
    return 0;
}
