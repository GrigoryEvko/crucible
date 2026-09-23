// A predicate that derives from BoundedAbove<9> and accepts every value
// deduces against the ceiling families through its base.  The relation
// refuses a predicate with a base class.  The premise borrows no ceiling,
// and the constrained weakening fails.

#include <fixy/Refined.h>

namespace {

struct AcceptsEverything : fixy::BoundedAbove<9> {
    constexpr bool operator()(auto) const noexcept { return true; }
};

inline constexpr AcceptsEverything accepts_everything{};

template <auto P, auto Q, class T>
    requires fixy::implies_v<P, Q>
[[nodiscard]] constexpr fixy::Refined<Q, T> weaken(fixy::Refined<P, T>&& refined) noexcept {
    return fixy::mint_refined_trusted<Q>(std::move(refined).into());
}

}  // namespace

int main() {
    fixy::Refined<accepts_everything, int> any = fixy::mint_refined<accepts_everything>(100);
    fixy::Refined<fixy::bounded_above<20>, int> claimed =
        weaken<accepts_everything, fixy::bounded_above<20>>(std::move(any));
    return claimed.value();
}
