// The binding of a function reference with a target type deduces each
// template parameter that the type names, also the brand of the result of
// the overload of mint_borrowed for a borrowed rvalue range.  Its result type
// names the brand in a context that deduction cannot set, so the target type
// names no template that matches.

#include <fixy/Borrowed.h>
#include <fixy/FixedArray.h>
#include <foundation/Quarantine.h>

#include <span>

namespace {
struct Owner {};
}  // namespace

int main() {
    ::fixy::FixedArray<int, 2> first_range{};
    ::fixy::FixedArray<int, 2> second_range{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture needs a borrowed range of the library, which no base type gives")
    auto first = ::fixy::mint_borrowed<Owner>(std::span<int>{first_range.data(), first_range.size()});
    using HeldBorrow = decltype(first);
    HeldBorrow (&mint)(std::span<int>&&) noexcept = ::fixy::mint_borrowed<Owner>;
    auto second = mint(std::span<int>{second_range.data(), second_range.size()});
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return first.front() + second.front();
}
