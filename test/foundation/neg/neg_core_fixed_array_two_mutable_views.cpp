// The caller takes two Views of one FixedArray and writes the same element
// through both.  The two Views would name the same elements, so no proof that
// two runs do not overlap would hold for them.  view() gives a View of the
// const elements, and a write takes with_element or the member with_mut.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{};
    ::foundation::core::View<int, 4> const first = array.view(), second = array.view();
    first.at(::foundation::core::index<0>()) = 1;
    second.at(::foundation::core::index<0>()) = 2;
    return array.at(::foundation::core::index<0>());
}
