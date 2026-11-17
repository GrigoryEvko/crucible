// A refinement witness whose predicate takes another type than the
// payload.
//
// The call operator of the predicate takes a double and no other type, and
// the payload is an int.  So no call of the predicate can check the claim
// on the payload, and the witness refines nothing on this binding.  The
// gate refuses the binding at tier 5.

#include <fixy/Fn.h>

#include <concepts>

struct ratio_is_non_negative final {
    constexpr bool operator()(std::same_as<double> auto ratio) const noexcept { return ratio >= 0.0; }
};

using Refused = ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::cost_constant,
                           ::fixy::atom::refined_with<ratio_is_non_negative>>;

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
