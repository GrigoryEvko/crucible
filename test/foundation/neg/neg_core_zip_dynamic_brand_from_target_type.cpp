// The address of try_zip of two walks when one has a dynamic extent, with a
// target type, does not choose the brand of the Refs.  The brand of the
// return type is in a context that deduction does not read, so the brand
// parameter keeps its default, the fresh brand.  A target type with a
// different brand then matches no function.

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

}  // namespace

int main() {
    namespace core = ::foundation::core;
    auto [values, values_key] =
        core::mint_keyed_owner(core::fixed_array_owner<int, 4>, core::FixedArray<int, 4>{1, 2, 3, 4});
    using Dynamic = ResultOf<decltype(&core::View<int const>::walk)>::type;
    using Fixed = ResultOf<decltype(&core::View<int const, 4>::walk)>::type;
    using Pairs = core::Result<core::Walk<core::detail::ZipStage<StageOf<Dynamic>::type, StageOf<Fixed>::type, Held>>,
                               core::LengthMismatch>;
    using Pick = Pairs (Dynamic::*)(Fixed&&) && noexcept;
    return values.with_mut(values_key, [](auto run) noexcept -> int {
        core::View<int const> const dynamic = run.view();
        return (dynamic.walk().*static_cast<Pick>(&Dynamic::try_zip))(run.view().walk()).is_ok() ? 0 : 1;
    });
}
