// A thread-local cell of a value of 16 bytes.  Each cell of the table of a
// thread holds thread_local_cell_bytes bytes, 8, so the handle refuses a
// value that does not fit a cell.

#include <foundation/ThreadLocalRef.h>

namespace thread_local_ref_size_fixture {
struct SixteenBytes {
    unsigned long long low;
    unsigned long long high;
};
struct PairTag {};
}  // namespace thread_local_ref_size_fixture

int main() {
    using namespace thread_local_ref_size_fixture;
    [[maybe_unused]] const ::foundation::ThreadLocalRef<PairTag, SixteenBytes> cell{};
    return 0;
}
