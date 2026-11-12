// Tests of the refusals of the allocation door of the Ref family
// (foundation/core/Heap.h).
//
// The owners of the base, such as Box, give the door only a size and an
// alignment that their types give, so no owner reaches these refusals.  The
// test calls the door itself.  test_core_box tests the storage that the door
// gives, through Box.

#include <foundation/core/Heap.h>

#include <foundation/Platform.h>

#include "abort_probe.h"

#include <cstddef>

namespace {

using ::foundation::core::detail::heap_allocate_;

// A request of zero bytes, and an alignment that is not a power of two, end
// the process.  The door gives no storage on these paths.
void test_bad_requests_abort() {
    bool const zero_bytes_aborts = ::foundation::test::aborts([] { static_cast<void>(heap_allocate_(0, 8)); });
    bool const zero_alignment_aborts = ::foundation::test::aborts([] { static_cast<void>(heap_allocate_(8, 0)); });
    bool const odd_alignment_aborts = ::foundation::test::aborts([] { static_cast<void>(heap_allocate_(8, 24)); });
    bool const large_odd_alignment_aborts =
        ::foundation::test::aborts([] { static_cast<void>(heap_allocate_(64, 48)); });
    CRUCIBLE_FATAL_INVARIANT(zero_bytes_aborts && zero_alignment_aborts);
    CRUCIBLE_FATAL_INVARIANT(odd_alignment_aborts && large_odd_alignment_aborts);
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

void test_exhausted_memory_aborts() {
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    bool const plain_aborts =
        ::foundation::test::aborts([] { static_cast<void>(heap_allocate_(too_large_request(), 8)); });
    bool const aligned_aborts =
        ::foundation::test::aborts([] { static_cast<void>(heap_allocate_(too_large_request(), 64)); });
    CRUCIBLE_FATAL_INVARIANT(plain_aborts && aligned_aborts);
#endif
}

}  // namespace

int main() {
    test_bad_requests_abort();
    test_exhausted_memory_aborts();
    return 0;
}
