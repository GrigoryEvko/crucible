// A borrow of one object must not carry the brand of a borrow of a different
// object.  mint_borrowed_ref takes its brand from its call site, and an
// explicit template argument lands in a pack that must be empty, so no
// caller spells a brand that it holds.

#include <fixy/Borrowed.h>

int main() {
    int first_object = 1;
    int second_object = 2;
    auto first = ::fixy::mint_borrowed_ref(first_object);
    using HeldBrand = decltype(first)::brand_type;
    auto second = ::fixy::mint_borrowed_ref<int, HeldBrand>(second_object);
    return first.get() + second.get();
}
