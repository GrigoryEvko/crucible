// Tests of the allocation door of the Ref family (foundation/core/Heap.h)
// and of the Layout that it takes (foundation/alloc/Layout.h).
//
// The door takes one Layout, so no call gives it zero bytes or an alignment
// that is not a power of two.  The run-time doors of Layout refuse such a
// request with an error, and the process goes on.  The owners of the base,
// such as Box, give the door only layouts of their types.  test_core_box
// tests the storage that the door gives, through Box.

#include <foundation/core/Heap.h>

#include <foundation/Platform.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace alloc = ::foundation::alloc;

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;
using ::foundation::core::ByteCount;
using ::foundation::core::ElementCount;
// The two calls of the allocation door in this test.  Only the owners of the
// base call the door, and each call here runs in a child that the door ends,
// so no storage comes back.  A build with a sanitizer runs no call of
// take_storage (test_exhausted_memory_aborts).
[[maybe_unused]] void take_storage(alloc::Layout const& layout) noexcept {
    static_cast<void>(::foundation::core::detail::heap_allocate_(layout));
}

void take_zeroed_storage(alloc::Layout const& layout) noexcept {
    static_cast<void>(::foundation::core::detail::heap_allocate_zeroed_(layout));
}

// The error of a refused request.  A request that the door takes gives the
// error zero_bytes, which no refusal below expects.
[[nodiscard]] alloc::LayoutError error_of(alloc::LayoutResult&& result) noexcept {
    return static_cast<alloc::LayoutResult&&>(result).err().value_or(alloc::LayoutError::zero_bytes);
}

// A request of zero bytes, an alignment that is not a power of two, and
// bytes past the largest object give an error, and the process goes on.
void test_bad_requests_give_errors(TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, error_of(alloc::mint_layout(ByteCount{0}, alloc::Alignment{8}))
                                   == alloc::LayoutError::zero_bytes);
    CRUCIBLE_TEST_ASSERT(test, error_of(alloc::mint_layout(ByteCount{8}, alloc::Alignment{0}))
                                   == alloc::LayoutError::alignment_not_power_of_two);
    CRUCIBLE_TEST_ASSERT(test, error_of(alloc::mint_layout(ByteCount{8}, alloc::Alignment{24}))
                                   == alloc::LayoutError::alignment_not_power_of_two);
    CRUCIBLE_TEST_ASSERT(test, error_of(alloc::mint_layout(ByteCount{~std::size_t{0} - 4096}, alloc::Alignment{8}))
                                   == alloc::LayoutError::too_large);
    CRUCIBLE_TEST_ASSERT(test, error_of(alloc::Layout::of_array<std::uint64_t>(ElementCount{0}))
                                   == alloc::LayoutError::zero_bytes);
    CRUCIBLE_TEST_ASSERT(test, error_of(alloc::Layout::of_array<std::uint64_t>(ElementCount{~std::size_t{0} / 4}))
                                   == alloc::LayoutError::too_large);
    CRUCIBLE_TEST_ASSERT(test, alloc::mint_alignment(ByteCount{48}).err().value_or(alloc::LayoutError::zero_bytes)
                                   == alloc::LayoutError::alignment_not_power_of_two);
}

// The zeroed door takes at most the alignment of calloc, 16 bytes, and a
// larger alignment ends the process in a child through expect_dies.
void test_zeroed_door_refuses_a_large_alignment(TestRunnerCtx const& test) {
    expect_dies(
        test,
        [] noexcept {
            alloc::Layout const line = alloc::mint_layout(ByteCount{64}, alloc::Alignment{64}).expect("a line layout");
            take_zeroed_storage(line);
        },
        ::foundation::core::TextView{
            "foundation: fatal: the zeroed allocation door got a request of 64 bytes at the alignment 64"});
}

// The allocator refuses a request that no address space holds, and the door
// ends the process with no null pointer for its caller.  A sanitizer
// allocator reports such a request itself and does not give the null
// pointer, so only a build with no sanitizer runs this test.
//
// The size comes from a function that the optimizer does not see into, so
// -Walloc-size-larger-than does not refuse a constant at the call.
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
[[gnu::noipa]] std::size_t too_large_request() noexcept { return std::size_t{1} << 62; }
#endif

void test_exhausted_memory_aborts([[maybe_unused]] TestRunnerCtx const& test) {
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    // too_large_request() is 2^62, a layout that no host holds.
    expect_dies(
        test,
        [] noexcept {
            take_storage(
                alloc::mint_layout(ByteCount{too_large_request()}, alloc::Alignment{8}).expect("a layout of 2^62"));
        },
        ::foundation::core::TextView{
            "foundation: fatal: the allocator gave no storage for 4611686018427387904 bytes at the alignment 8"});
    expect_dies(
        test,
        [] noexcept {
            take_storage(
                alloc::mint_layout(ByteCount{too_large_request()}, alloc::Alignment{64}).expect("a layout of 2^62"));
        },
        ::foundation::core::TextView{
            "foundation: fatal: the allocator gave no storage for 4611686018427387904 bytes at the alignment 64"});
#endif
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_bad_requests_give_errors(test);
    test_zeroed_door_refuses_a_large_alignment(test);
    test_exhausted_memory_aborts(test);
    return 0;
}
