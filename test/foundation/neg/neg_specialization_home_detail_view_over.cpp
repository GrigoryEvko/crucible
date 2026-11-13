// A file outside Region.h writes an explicit specialization of the template
// detail::view_over_.  While View befriends view_over_, the body of the
// specialization reaches the private constructor of View and can give a View
// twice as long as its buffer, so a loop over it reads past the buffer.  The
// rule specialization-home refuses the specialization in each build, because
// a function template of the tree gets an explicit specialization only in its
// own file.  The body reaches no private member, so the fixture reads only
// the rule.

#include <foundation/core/Region.h>

template <>
constexpr foundation::core::View<int> foundation::core::detail::view_over_<int>(int*, std::size_t) noexcept {
    for (;;) {}
}

int main() { return 0; }
