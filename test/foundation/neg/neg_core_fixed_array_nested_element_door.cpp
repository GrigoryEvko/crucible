// A body of the element door with_element of a FixedArray calls the door of
// the same array again, for the same element.  The two bodies would hold two
// references to one element, and each could write it beside the other.  The
// door carries the annotation borrow_match, so the rule borrow-match refuses
// a call of a member of the array that is not const in the body.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{};
    array.with_element(::foundation::core::index<0>(), [&array](int& first) noexcept {
        array.with_element(::foundation::core::index<0>(), [&first](int& second) noexcept { second = first + 1; });
        first = 2;
    });
    return array.at(::foundation::core::index<0>());
}
