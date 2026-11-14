// A second borrow must not carry the brand of a borrow that the caller
// holds.  mint_borrowed takes its brand from its call site, and an explicit
// template argument after the owner lands in a pack that must be empty, so
// no caller spells a brand that it holds.  The refusal is at the level of
// the type, so the two borrows can name one range.

#include <fixy/Borrowed.h>

namespace {
struct Owner {};
int range[2] = {1, 2};
}  // namespace

int main() {
    auto first = ::fixy::mint_borrowed<Owner>(range);
    using HeldBrand = decltype(first)::brand_type;
    auto second = ::fixy::mint_borrowed<Owner, int (&)[2], HeldBrand>(range);
    return first.front() + second.front();
}
