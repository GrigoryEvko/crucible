// A store of a value that the cell type cannot be assigned from.  store()
// requires that the value type is assignable from the argument.

#include <foundation/ThreadLocalRef.h>

namespace thread_local_ref_store_fixture {
struct Label {
    const char* text = "not a counter";
};
struct CounterTag {};
}  // namespace thread_local_ref_store_fixture

int main() {
    using namespace thread_local_ref_store_fixture;
    const ::foundation::ThreadLocalRef<CounterTag, int> cell{};
    cell.store(Label{});
    return cell.peek();
}
