// Inside a frame body, two copies of the View of the run would each write
// each element.  The frame would then have three writers of one run, and a
// door that reads the brands would see one writer.  The View of a ViewMut
// reads, so it does not convert to a View that writes.  A copy that leaves
// the body through a capture by reference is the case of the store check of
// the door (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/List.h>
#include <foundation/core/Region.h>

#include <utility>

namespace fc = ::foundation::core;

int main() {
    fc::FixedList<int, 4> list{};
    static_cast<void>(list.append(1));
    fc::FixedList<int, 4> back = fc::with_mut(std::move(list), [](auto run) noexcept {
        fc::View<int> first_writer = run.view();
        fc::View<int> second_writer = first_writer;
        for (int& element : first_writer)
            element = 5;
        for (int& element : second_writer)
            element += 1;
    });
    return back.size() == 1 ? 0 : 1;
}
