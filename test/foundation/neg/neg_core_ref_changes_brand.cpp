// A brand names one borrow.  A Ref of one brand is never a Ref of a second
// fresh brand, so a callee that asks for one brand on two Refs gets two
// Refs of one borrow.

#include <foundation/core/Ref.h>

template <class T, class Brand>
Brand brand_of(::foundation::core::Ref<T, Brand>);

int main() {
    int first = 1;
    int second = 2;
    auto const first_ref = ::foundation::core::to(first);
    auto const second_ref = ::foundation::core::to(second);
    ::foundation::core::Ref<int, decltype(brand_of(second_ref))> const swapped{first_ref};
    return *swapped;
}
