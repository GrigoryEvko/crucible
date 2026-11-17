// A thread-local cell of a value whose copy runs code.  A cell holds bytes,
// and peek() and store() copy the bytes, so a copy constructor would not run.
// The handle refuses a value type that is not trivially copyable.

#include <foundation/ThreadLocalRef.h>

namespace thread_local_ref_copy_fixture {
struct CountsCopies {
    int copies;
    CountsCopies() = default;
    CountsCopies(CountsCopies const& other) noexcept : copies{other.copies + 1} {}
};
struct CopyTag {};
}  // namespace thread_local_ref_copy_fixture

int main() {
    using namespace thread_local_ref_copy_fixture;
    [[maybe_unused]] const ::foundation::ThreadLocalRef<CopyTag, CountsCopies> cell{};
    return 0;
}
