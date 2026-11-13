// A borrow of one range must not carry the brand of a borrow of a different
// range.  mint_borrowed takes its brand from its call site, and an explicit
// template argument after the owner lands in a pack that must be empty, so
// no caller spells a brand that it holds.

#include <fixy/Borrowed.h>
#include <fixy/FixedArray.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    ::fixy::FixedArray<int, 2> first_range{};
    ::fixy::FixedArray<int, 2> second_range{};
    auto first = ::fixy::mint_borrowed<Owner>(first_range);
    using HeldBrand = decltype(first)::brand_type;
    auto second = ::fixy::mint_borrowed<Owner, ::fixy::FixedArray<int, 2>&, HeldBrand>(second_range);
    return first.front() + second.front();
}
