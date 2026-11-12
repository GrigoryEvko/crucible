// A View of a temporary FixedList would point into an object that the full
// expression ends, so view() of an rvalue is refused.

#include <foundation/core/List.h>

int main() {
    auto elements = ::foundation::core::FixedList<int, 4>{}.view();
    return elements.size() == 0 ? 1 : 0;
}
