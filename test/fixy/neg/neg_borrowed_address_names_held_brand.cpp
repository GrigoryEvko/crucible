// The binding of a function reference with a target type deduces each
// template parameter that the type names, also the brand of the result.  A
// caller would then mint a borrow of a second range with a brand that it
// holds.  The result type of mint_borrowed names its brand in a context that
// deduction cannot set, so the target type names no template that matches.

#include <fixy/Borrowed.h>
#include <fixy/FixedArray.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    ::fixy::FixedArray<int, 2> first_range{};
    ::fixy::FixedArray<int, 2> second_range{};
    auto first = ::fixy::mint_borrowed<Owner>(first_range);
    using HeldBorrow = decltype(first);
    HeldBorrow (&mint)(::fixy::FixedArray<int, 2>&) noexcept = ::fixy::mint_borrowed<Owner>;
    auto second = mint(second_range);
    return first.front() + second.front();
}
