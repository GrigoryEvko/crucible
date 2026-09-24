// A store of a value that the cell type cannot be assigned from.  store()
// requires that the value type is assignable from the argument.

#include <foundation/ThreadLocalRef.h>

namespace {
struct Counter {
    int value = 0;
};
struct Label {
    const char* text = "not a counter";
};
struct CounterTag {};
}  // namespace

int main() {
    const auto cell = ::foundation::mint_thread_local_ref<CounterTag, Counter>();
    cell.store(Label{});
    return cell.peek().value;
}
