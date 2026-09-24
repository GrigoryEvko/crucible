// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Lazy<T>::get_or_init builds its T from what the initializer returns,
// so the initializer must return a T or a type that converts to T
// without a cast.  The result below converts to int only through an
// explicit conversion, so the requires-clause of get_or_init refuses the
// initializer.  Without that check, the placement new of the T is a
// direct initialization, which uses the explicit conversion, and the
// Lazy<int> would take the value in silence.
//
// Expected diagnostic: get_or_init has no viable candidate, and the note
// names the is_invocable_r_v<T, F> check.

#include <fixy/handle/Once.h>

namespace h = fixy::handle;

namespace {
struct ExplicitCode {
    explicit constexpr operator int() const noexcept { return 7; }
};
}  // namespace

int main() {
    h::Lazy<int> lazy{};
    return lazy.get_or_init([]() noexcept { return ExplicitCode{}; });
}
