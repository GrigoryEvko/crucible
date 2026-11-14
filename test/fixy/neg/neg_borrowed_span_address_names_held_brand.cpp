// The binding of a function reference with a target type deduces each
// template parameter that the type names, also the brand of the result of
// the overload of mint_borrowed for a borrowed rvalue range.  Its result type
// names the brand in a context that deduction cannot set, so the target type
// names no template that matches.  The refusal is at the level of the type,
// so the two borrows can name one range.

#include <fixy/Borrowed.h>
#include <foundation/Quarantine.h>

#include <span>

namespace {
struct Owner {};
int range[2] = {1, 2};
}  // namespace

int main() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture needs a borrowed range of the library, which no base type gives")
    auto first = ::fixy::mint_borrowed<Owner>(std::span<int>{range});
    using HeldBorrow = decltype(first);
    HeldBorrow (&mint)(std::span<int>&&) noexcept = ::fixy::mint_borrowed<Owner>;
    auto second = mint(std::span<int>{range});
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return first.front() + second.front();
}
