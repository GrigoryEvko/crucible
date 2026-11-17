// A thread-local cell of a type with no default constructor.  A cell of a
// thread starts as zero bytes, which is the value T{} only of a type that is
// trivially default constructible, so the handle refuses this value type.

#include <foundation/ThreadLocalRef.h>

namespace thread_local_ref_default_fixture {
struct NeedsSeed {
    int value;
    constexpr explicit NeedsSeed(int seed) noexcept : value{seed} {}
};
struct CounterTag {};
}  // namespace thread_local_ref_default_fixture

int main() {
    using namespace thread_local_ref_default_fixture;
    [[maybe_unused]] const ::foundation::ThreadLocalRef<CounterTag, NeedsSeed> cell{};
    return 0;
}
