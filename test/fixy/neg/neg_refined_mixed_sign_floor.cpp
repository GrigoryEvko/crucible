// A floor of -1 does not imply a floor of 9u.  The floor family
// compares its bounds by value.  The conversion that turns -1 into the
// largest unsigned value never admits the step.

#include <fixy/Refined.h>

namespace {

template <auto P, auto Q, class T>
    requires fixy::implies_v<P, Q>
[[nodiscard]] constexpr fixy::Refined<Q, T> weaken(fixy::Refined<P, T>&& refined) noexcept {
    return fixy::mint_refined_trusted<Q>(std::move(refined).into());
}

}  // namespace

int main() {
    fixy::Refined<fixy::bounded_below<-1>, int> low = fixy::mint_refined<fixy::bounded_below<-1>>(0);
    fixy::Refined<fixy::bounded_below<9u>, int> claimed =
        weaken<fixy::bounded_below<-1>, fixy::bounded_below<9u>>(std::move(low));
    return claimed.value();
}
