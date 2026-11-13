// A file outside Region.h writes an explicit specialization of the template
// detail::view_over_.  View names view_over_ as no friend, so the body
// reaches no private member of a View.  A specialization still gives each
// caller of view_over_<int> a different body, such as one that skips the
// checks of the raw parts.  The rule specialization-home refuses the
// specialization in each build, because a function template of the tree
// gets an explicit specialization only in its own file.  The body reaches no
// private member, so the fixture reads only the rule.

#include <foundation/core/Region.h>

template <>
constexpr foundation::core::View<int> foundation::core::detail::view_over_<int>(int*, std::size_t) noexcept {
    for (;;) {}
}

int main() { return 0; }
