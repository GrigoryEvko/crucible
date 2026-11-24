// Inside a frame body, two copies of the View of the run would each write
// each element, and one copy would leave the body through a capture.  The
// frame would then have three writers of one run, and a door that reads the
// brands would see one writer.  The View of a ViewMut reads, so it does not
// convert to a View that writes.

#include <foundation/core/List.h>
#include <foundation/core/Region.h>

#include <utility>

namespace fc = ::foundation::core;

int main() {
    fc::FixedList<int, 4> list{};
    static_cast<void>(list.append(1));
    fc::View<int> escaped{};
    fc::FixedList<int, 4> back = fc::with_mut(std::move(list), [&escaped](auto run) noexcept {
        fc::View<int> first_writer = run.view();
        fc::View<int> second_writer = first_writer;
        for (int& element : first_writer)
            element = 5;
        for (int& element : second_writer)
            element += 1;
        escaped = second_writer;
    });
    return back.size() == 1 && escaped.size() == 1 ? 0 : 1;
}
