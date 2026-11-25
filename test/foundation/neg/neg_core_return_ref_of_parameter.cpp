// A function returns the Ref of an element of a FixedList parameter by value.
// The parameter ends with the frame of the function, so a read of the result
// reads a dead stack frame.  The rule view-escape refuses the return.

#include <foundation/core/List.h>
#include <foundation/core/Ref.h>

#include <utility>

namespace fc = ::foundation::core;

namespace {

auto ref_of_parameter(fc::FixedList<int, 4> list) { return std::move(list.get(0)).expect("room"); }

}  // namespace

// The call does not read the result, so an optimized build gives no warning
// about the dead parameter in main.
int main() {
    static_cast<void>(ref_of_parameter(fc::FixedList<int, 4>{}));
    return 0;
}
