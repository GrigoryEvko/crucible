// A Ref<T const> reads its object only.  Nothing converts it to a Ref<T>,
// which writes the object.

#include <foundation/core/Ref.h>

int main() {
    int number = 1;
    ::foundation::core::Ref<int const> const fixed = ::foundation::core::Ref<int const>::to(number);
    ::foundation::core::Ref<int> const writer{fixed};
    *writer = 2;
    return number;
}
