// A function returns the writing View of a local FixedList.  The elements
// live in the frame of the function, so a write through the result writes a
// dead stack frame.  The rule view-escape refuses the return.

#include <foundation/core/List.h>
#include <foundation/core/Region.h>

namespace fc = ::foundation::core;

namespace {

fc::View<int> view_of_local_list_mut() {
    fc::FixedList<int, 4> list{};
    static_cast<void>(list.append(4));
    return list.view();
}

}  // namespace

int main() { return view_of_local_list_mut().size() == 1 ? 0 : 1; }
