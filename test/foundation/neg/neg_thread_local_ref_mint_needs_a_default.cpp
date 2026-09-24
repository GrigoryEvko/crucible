// A thread-local cell of a type with no default constructor.  The cell is
// built on the first access of each thread, with no argument to build it
// from, so the handle requires a default-constructible value.

#include <foundation/ThreadLocalRef.h>

namespace {
struct NeedsSeed {
    int value;
    constexpr explicit NeedsSeed(int seed) noexcept : value{seed} {}
};
struct CounterTag {};
}  // namespace

int main() {
    auto cell = ::foundation::mint_thread_local_ref<CounterTag, NeedsSeed>();
    return cell.peek().value;
}
