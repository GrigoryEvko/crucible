// A Ref<T const, Brand> reads its object only.  Nothing converts it to a
// Ref<T, Brand>, which writes the object.

#include <foundation/core/Ref.h>

template <class T, class Brand>
Brand brand_of(::foundation::core::Ref<T, Brand>);

int main() {
    int number = 1;
    int const& read = number;
    auto const fixed = ::foundation::core::to(read);
    ::foundation::core::Ref<int, decltype(brand_of(fixed))> const writer{fixed};
    *writer = 2;
    return number;
}
