// Two Views of different fixed extents have different counts, so zip of the
// two is refused at compile time.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> const left{1, 2, 3, 4};
    ::foundation::core::FixedArray<int, 5> const right{1, 2, 3, 4, 5};
    int total = 0;
    for (auto const [first, second] : ::foundation::core::zip(left.view(), right.view())) {
        total += *first * *second;
    }
    return total;
}
