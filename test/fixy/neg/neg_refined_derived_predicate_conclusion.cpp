// A predicate that derives from BoundedAbove<20> and accepts no value
// deduces against the ceiling families through its base.  The relation
// refuses a predicate with a base class.  No range implies it, and the
// constrained weakening fails.

#include <fixy/Refined.h>

namespace {

struct AcceptsNothing : fixy::BoundedAbove<20> {
    constexpr bool operator()(auto) const noexcept { return false; }
};

inline constexpr AcceptsNothing accepts_nothing{};

template <auto P, auto Q, class T>
    requires fixy::implies_v<P, Q>
[[nodiscard]] constexpr fixy::Refined<Q, T> weaken(fixy::Refined<P, T>&& refined) noexcept {
    return fixy::mint_refined_trusted<Q>(std::move(refined).into());
}

}  // namespace

int main() {
    fixy::Refined<fixy::in_range<5, 9>, int> narrow = fixy::mint_refined<fixy::in_range<5, 9>>(7);
    fixy::Refined<accepts_nothing, int> claimed = weaken<fixy::in_range<5, 9>, accepts_nothing>(std::move(narrow));
    return claimed.value();
}
