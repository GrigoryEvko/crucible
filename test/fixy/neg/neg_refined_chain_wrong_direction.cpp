// A chain runs one way.  in_range<5, 9> reaches bounded_above<20>, and
// no admitted step leads back from a ceiling to a range.  The
// constrained weakening from the ceiling to the range fails.

#include <fixy/Refined.h>

namespace {

static_assert(fixy::implies_v<fixy::in_range<5, 9>, fixy::bounded_above<20>>);

template <auto P, auto Q, class T>
    requires fixy::implies_v<P, Q>
[[nodiscard]] constexpr fixy::Refined<Q, T> weaken(fixy::Refined<P, T>&& refined) noexcept {
    return fixy::mint_refined_trusted<Q>(std::move(refined).into());
}

}  // namespace

int main() {
    fixy::Refined<fixy::bounded_above<20>, int> wide = fixy::mint_refined<fixy::bounded_above<20>>(15);
    fixy::Refined<fixy::in_range<5, 9>, int> claimed = weaken<fixy::bounded_above<20>, fixy::in_range<5, 9>>(
        std::move(wide));
    return claimed.value();
}
