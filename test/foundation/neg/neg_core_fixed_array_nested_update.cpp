// A body of update of a FixedArray writes the same array through its name.
// The write of the same element would be lost when the door puts its copy
// back.  The door carries the annotation borrow_match, so the rule
// borrow-match refuses a call of a member of the array that is not const in
// the body.  The test of the array shows the loss through a helper, which the
// rule does not see.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{};
    array.update(::foundation::core::index<0>(), [&array](int& first) noexcept {
        array.set(::foundation::core::index<0>(), 7);
        first += 1;
    });
    return array.at(::foundation::core::index<0>());
}
