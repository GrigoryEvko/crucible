// Two Views of one list with written elements would be two writers of one
// element.  view() gives const elements, also on a list that is not const.

#include <foundation/core/List.h>

namespace fc = ::foundation::core;

int main() {
    fc::FixedList<int, 2> numbers{};
    static_cast<void>(numbers.append(1).expect("the list has room"));
    *numbers.view().get(0).expect("the list holds an element") = 2;
    return 0;
}
