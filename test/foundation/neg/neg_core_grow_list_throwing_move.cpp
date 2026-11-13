// A growth moves each element, so mint_grow_list refuses an element whose
// move can throw: the growth could lose an element.

#include <foundation/core/Grow.h>

struct ThrowingMove {
    int value = 0;
    ThrowingMove() noexcept = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

int main() {
    auto list = ::foundation::core::mint_grow_list<ThrowingMove>(4);
    return static_cast<int>(list.size());
}
