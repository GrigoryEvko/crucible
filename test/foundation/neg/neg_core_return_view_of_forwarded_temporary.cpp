// A forwarding parameter deduces a value when the caller gives a temporary,
// so the instance takes the list by an rvalue reference.  The View of that
// list ends with the full expression of the call, and the rule view-escape
// refuses the return in that instance.

#include <foundation/core/List.h>
#include <foundation/core/Region.h>

#include <utility>

namespace fc = ::foundation::core;

namespace {

template <class Whole>
fc::View<int const> view_of_forwarded(Whole&& list) {
    return std::as_const(list).view();
}

}  // namespace

// The call does not read the result, so an optimized build gives no warning
// about the dead temporary in main.
int main() {
    static_cast<void>(view_of_forwarded(fc::FixedList<int, 4>{}));
    return 0;
}
