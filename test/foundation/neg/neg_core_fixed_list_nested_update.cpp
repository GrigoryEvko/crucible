// A body of update of a FixedList writes the same list through its name.  The
// write of the same element would be lost when the door puts its copy back.
// The door carries the annotation borrow_match, so the rule borrow-match
// refuses a call of a member of the list that is not const in the body.

#include <foundation/core/List.h>

int main() {
    ::foundation::core::FixedList<int, 4> list{};
    static_cast<void>(list.append(1).expect("the list has room"));
    list.update(::foundation::core::index<0>(), [&list](int& first) noexcept {
        list.set(::foundation::core::index<0>(), 7);
        first += 1;
    });
    return 0;
}
