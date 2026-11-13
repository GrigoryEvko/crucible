// A file outside Region.h writes an explicit specialization of first_of_,
// the door to the pointer of a View, with a body that gives a different
// pointer.  first_of_ is a friend that View defines in its class, and it is
// no template, so the compiler refuses the specialization.

#include <foundation/core/Region.h>

int forged = 0;

template <>
constexpr int* foundation::core::first_of_(foundation::core::detail::ViewKey const&,
                                           foundation::core::View<int>) noexcept {
    return &forged;
}

int main() { return 0; }
