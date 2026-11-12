// A Ref from a push into a temporary FixedList would dangle at the end of the
// full expression, so push takes an lvalue list only.

#include <foundation/core/List.h>

int main() {
    auto pushed = ::foundation::core::FixedList<int, 4>{}.push(1);
    return pushed.is_ok() ? 1 : 0;
}
