// A function returns the read-only View of a local FixedList.  The elements
// live in the frame of the function, so a read of the result reads a dead
// stack frame.  The rule view-escape refuses the return.

#include <foundation/core/List.h>
#include <foundation/core/Region.h>

#include <utility>

namespace fc = ::foundation::core;

namespace {

fc::View<int const> view_of_local_list() {
    fc::FixedList<int, 4> list{};
    static_cast<void>(list.append(3));
    return std::as_const(list).view();
}

}  // namespace

int main() { return view_of_local_list().size() == 1 ? 0 : 1; }
