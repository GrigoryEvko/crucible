// A file outside Region.h writes an explicit specialization of the typed door
// detail::view_of_object_.  View names the door as a friend template, so the
// body of such a specialization reaches the private constructors of a View
// with any address.  The rule specialization-home refuses the specialization
// in each build, because a function template of the tree gets an explicit
// specialization only in its own file.

#include <foundation/core/Region.h>

template <>
constexpr foundation::core::View<int, 1> foundation::core::detail::view_of_object_<int>(int& object) noexcept {
    return foundation::core::View<int, 1>{&object};
}

int main() { return 0; }
