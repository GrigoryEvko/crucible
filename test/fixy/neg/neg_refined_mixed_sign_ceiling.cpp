// A ceiling of 9u does not imply a ceiling of -1.  The ceiling family
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
    fixy::Refined<fixy::bounded_above<9u>, unsigned> nine = fixy::mint_refined<fixy::bounded_above<9u>>(5u);
    fixy::Refined<fixy::bounded_above<-1>, unsigned> claimed =
        weaken<fixy::bounded_above<9u>, fixy::bounded_above<-1>>(std::move(nine));
    return static_cast<int>(claimed.value());
}
