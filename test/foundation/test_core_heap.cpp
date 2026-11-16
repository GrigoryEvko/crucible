// Tests of the refusals of the allocation door of the Ref family
// (foundation/core/Heap.h).
//
// The owners of the base, such as Box, give the door only a size and an
// alignment that their types give, so no owner reaches these refusals.  The
// test calls the door itself.  test_core_box tests the storage that the door
// gives, through Box.

#include <foundation/core/Heap.h>

#include <foundation/Platform.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace {

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;
using ::foundation::core::detail::heap_allocate_;

// A request of zero bytes, and an alignment that is not a power of two, end
// the process.  The door gives no storage on these paths.  Each request runs
// in a child through expect_dies of the Check family.
void test_bad_requests_abort(TestRunnerCtx const& test) {
    expect_dies(
        test, [] noexcept { static_cast<void>(heap_allocate_(0, 8)); },
        ::foundation::core::TextView{
            "foundation: fatal: the allocation door got a request of 0 bytes at the alignment 8"});
    expect_dies(
        test, [] noexcept { static_cast<void>(heap_allocate_(8, 0)); },
        ::foundation::core::TextView{
            "foundation: fatal: the allocation door got a request of 8 bytes at the alignment 0"});
    expect_dies(
        test, [] noexcept { static_cast<void>(heap_allocate_(8, 24)); },
        ::foundation::core::TextView{
            "foundation: fatal: the allocation door got a request of 8 bytes at the alignment 24"});
    expect_dies(
        test, [] noexcept { static_cast<void>(heap_allocate_(64, 48)); },
        ::foundation::core::TextView{
            "foundation: fatal: the allocation door got a request of 64 bytes at the alignment 48"});
}

// The allocator refuses a request that no address space holds, and the door
// ends the process with no null pointer for its caller.  A sanitizer
// allocator reports such a request itself and does not give the null
// pointer, so only a build with no sanitizer runs this test.
//
// The size comes from a function that the optimizer does not see into, so
// -Walloc-size-larger-than does not refuse a constant at the call.
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
[[gnu::noipa]] std::size_t too_large_request() noexcept { return ~std::size_t{0} - 4096; }
#endif

void test_exhausted_memory_aborts([[maybe_unused]] TestRunnerCtx const& test) {
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    // too_large_request() is 2^64 - 1 - 4096.
    expect_dies(
        test, [] noexcept { static_cast<void>(heap_allocate_(too_large_request(), 8)); },
        ::foundation::core::TextView{
            "foundation: fatal: the allocator gave no storage for 18446744073709547519 bytes at the "
            "alignment 8"});
    expect_dies(
        test, [] noexcept { static_cast<void>(heap_allocate_(too_large_request(), 64)); },
        ::foundation::core::TextView{
            "foundation: fatal: the allocator gave no storage for 18446744073709547519 bytes at the "
            "alignment 64"});
#endif
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_bad_requests_abort(test);
    test_exhausted_memory_aborts(test);
    return 0;
}
