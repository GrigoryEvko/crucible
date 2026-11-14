// The binding of a function reference with a target type deduces each
// template parameter that the type names, also the brand of the result.  A
// caller would then mint a second borrow with a brand that it holds.  The
// result type of mint_borrowed names its brand in a context that deduction
// cannot set, so the target type names no template that matches.  The
// refusal is at the level of the type, so the two borrows can name one
// range.

#include <fixy/Borrowed.h>

namespace {
struct Owner {};
int range[2] = {1, 2};
}  // namespace

int main() {
    auto first = ::fixy::mint_borrowed<Owner>(range);
    using HeldBorrow = decltype(first);
    HeldBorrow (&mint)(int (&)[2]) noexcept = ::fixy::mint_borrowed<Owner>;
    auto second = mint(range);
    return first.front() + second.front();
}
