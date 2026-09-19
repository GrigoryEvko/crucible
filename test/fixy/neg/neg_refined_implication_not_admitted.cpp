// The implication relation is the namespace admitted_implications and
// nothing else.  non_negative ⇒ positive is not a member, and an edge
// for it declared in some other namespace is inert, so a weakening
// constrained on the relation fails its constraint.

#include <fixy/Refined.h>

namespace {

namespace elsewhere {
inline constexpr ::foundation::fail_closed::edge<fixy::refined::predicate_t<fixy::non_negative>,
                                                 fixy::refined::predicate_t<fixy::positive>>
    non_negative_implies_positive{};
}  // namespace elsewhere

static_assert(::foundation::fail_closed::Admitted<^^elsewhere, fixy::refined::predicate_t<fixy::non_negative>,
                                                  fixy::refined::predicate_t<fixy::positive>>);

template <auto P, auto Q, class T>
    requires fixy::implies_v<P, Q>
[[nodiscard]] constexpr fixy::Refined<Q, T> weaken(fixy::Refined<P, T>&& r) noexcept {
    return fixy::mint_refined_trusted<Q>(std::move(r).into());
}

}  // namespace

int main() {
    fixy::Refined<fixy::non_negative, int> zero = fixy::mint_refined<fixy::non_negative>(0);
    fixy::Refined<fixy::positive, int> claimed = weaken<fixy::non_negative, fixy::positive>(std::move(zero));
    return claimed.value();
}
