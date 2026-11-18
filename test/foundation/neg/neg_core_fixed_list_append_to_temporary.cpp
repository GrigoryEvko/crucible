// A Ref from an append into a temporary FixedList would dangle at the end of
// the full expression, so append takes an lvalue list only.

#include <foundation/core/List.h>

int main() {
    auto appended = ::foundation::core::FixedList<int, 4>{}.append(1);
    return appended.is_ok() ? 1 : 0;
}
