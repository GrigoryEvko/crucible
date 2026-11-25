// An explicit specialization of try_zip of two walks when one has a dynamic
// extent does not choose the brand of the Refs.  The rules of the base refuse
// such a specialization, and this probe writes it in an opt-out region.  The
// brand of the return type is in a context that deduction does not read, so
// the brand parameter keeps its default, the fresh brand.  A declaration with
// a different brand then matches no template.

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

using Dynamic = ResultOf<decltype(&::foundation::core::View<int const>::walk)>::type;
// The name is not Fixed: the parameter list of the specialization below is in
// the scope of foundation::core, where Fixed names the fixed text of a report.
using FixedWalk = ResultOf<decltype(&::foundation::core::View<int const, 4>::walk)>::type;

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged zip of a dynamic extent shows that a declared type sets no brand")
template <>
template <>
constexpr foundation::core::Result<
    foundation::core::Walk<foundation::core::detail::ZipStage<StageOf<Dynamic>::type, StageOf<FixedWalk>::type, Held>>,
    foundation::core::LengthMismatch>
Dynamic::try_zip<>(FixedWalk&& second) && noexcept {
    static_cast<void>(second);
    foundation::core::fatal("a forged try_zip gives no walk");
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    return values.with_mut(values_key, [](auto run) noexcept -> int {
        ::foundation::core::View<int const> const dynamic = run.view();
        return dynamic.walk().try_zip(run.view().walk()).is_ok() ? 0 : 1;
    });
}
