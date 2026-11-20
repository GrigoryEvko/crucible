// The run-time path of the size classes of foundation/alloc/Layout.h against
// the slots of the classes.
//
// For each size from 1 byte to 2 MiB, and for each alignment from 1 byte to
// 4 KiB, the class of class_for at run time is the smallest class whose slot
// holds the size and is a whole count of the alignment.  The reference walks
// the 64 slots in order.  The check file of the header holds the same facts
// at each bound of a class, at compile time.

#include <foundation/alloc/Layout.h>

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

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_each_size_has_its_class(test);
    test_each_alignment_has_its_class(test);
    return 0;
}
