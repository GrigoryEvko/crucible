// A body of a frame door copies the RefMut that get_mut gives, so two names
// would write one element.  A RefMut is the one writer of its element.

#include <foundation/core/List.h>
#include <foundation/core/Region.h>

#include <utility>

namespace fc = ::foundation::core;

int main() {
    fc::FixedList<int, 4> list{};
    static_cast<void>(list.append(1));
    fc::FixedList<int, 4> back = fc::with_mut(std::move(list), [](auto run) noexcept {
        auto first_writer = std::move(run.get_mut(0)).expect("one element");
        auto second_writer = first_writer;
        *first_writer = 5;
        *second_writer += 1;
    });
    return back.size() == 1 ? 0 : 1;
}
