// An explicit specialization of zip of two Views of one fixed extent does
// not choose the brand of the Refs.  The rules of the base refuse such a
// specialization, and this probe writes it in an opt-out region.  The brand
// of the return type is in a context that deduction does not read, so the
// brand parameter keeps its default, the fresh brand.  A declaration with a
// different brand then matches no template.

#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>

#include <cstddef>

namespace {

struct Held final {};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged zip of one fixed extent shows that a declared type sets no brand")
template <>
constexpr foundation::core::detail::ZipRange<int, int, Held>
foundation::core::zip(foundation::core::View<int, 4> first, foundation::core::View<int, 4> second) noexcept {
    return foundation::core::detail::ZipRange<int, int, Held>{first, second};
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    ::foundation::core::FixedArray<int, 4> values{1, 2, 3, 4};
    return static_cast<int>(::foundation::core::zip(values.view(), values.view()).size());
}
