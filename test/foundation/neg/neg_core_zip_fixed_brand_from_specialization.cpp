// An explicit specialization of zip of two walks of one fixed extent does
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

// The result type of a member function that takes no argument.
template <class Fn>
struct ResultOf;
template <class R, class C>
struct ResultOf<R (C::*)() const noexcept> {
    using type = R;
};

// The stage of a walk.
template <class W>
struct StageOf;
template <class S>
struct StageOf<::foundation::core::Walk<S>> {
    using type = S;
};

// The name is not Fixed: the parameter list of the specialization below is in
// the scope of foundation::core, where Fixed names the fixed text of a report.
using FixedWalk = ResultOf<decltype(&::foundation::core::View<int, 4>::walk)>::type;
using Root = StageOf<FixedWalk>::type;

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged zip of one fixed extent shows that a declared type sets no brand")
template <>
template <>
constexpr foundation::core::Walk<foundation::core::detail::ZipStage<Root, Root, Held>>
FixedWalk::zip<>(FixedWalk&& second) && noexcept {
    static_cast<void>(second);
    foundation::core::fatal("a forged zip gives no walk");
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    ::foundation::core::FixedArray<int, 4> values{1, 2, 3, 4};
    return static_cast<int>(values.view().walk().zip(values.view().walk()).size());
}
