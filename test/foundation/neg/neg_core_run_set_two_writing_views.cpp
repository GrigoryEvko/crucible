// Two calls of view<I>() of one set would give two writing Views of one run,
// and the two writers would alias one element.  A copy of the set refers to
// the same run too.  So the View of a set gives only const elements, and each
// write through it does not compile.

#include <foundation/core/RunSet.h>

#include <cstdint>

int main() {
    ::foundation::core::RunSet<std::uint16_t, ::foundation::core::RunOf<int, 0>> set{};
    auto first = set.view<0>();
    auto second = set.view<0>();
    *second.get(0).expect("the run holds an element") = 2;
    return static_cast<int>(first.size());
}
