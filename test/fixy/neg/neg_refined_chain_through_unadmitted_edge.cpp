// A chain uses admitted steps only.  in_range<5, 9> reaches
// bounded_above<9> through the relation.  The edge from
// bounded_above<9> to even is outside admitted_implications, and it is
// inert.  No chain reaches even, and the constrained weakening fails.

#include <fixy/Refined.h>

namespace {

inline constexpr auto even = [](auto x) constexpr noexcept { return x % 2 == 0; };

namespace elsewhere {
inline constexpr ::foundation::fail_closed::edge<fixy::refined::predicate_t<fixy::bounded_above<9>>,
                                                 fixy::refined::predicate_t<even>>
    nine_implies_even{};
}  // namespace elsewhere

static_assert(::foundation::fail_closed::Admitted<^^elsewhere, fixy::refined::predicate_t<fixy::bounded_above<9>>,
                                                  fixy::refined::predicate_t<even>>);
static_assert(fixy::implies_v<fixy::in_range<5, 9>, fixy::bounded_above<9>>);

template <auto P, auto Q, class T>
    requires fixy::implies_v<P, Q>
[[nodiscard]] constexpr fixy::Refined<Q, T> weaken(fixy::Refined<P, T>&& refined) noexcept {
    return fixy::mint_refined_trusted<Q>(std::move(refined).into());
}

}  // namespace

int main() {
    fixy::Refined<fixy::in_range<5, 9>, int> narrow = fixy::mint_refined<fixy::in_range<5, 9>>(7);
    fixy::Refined<even, int> claimed = weaken<fixy::in_range<5, 9>, even>(std::move(narrow));
    return claimed.value();
}
