// A borrow of one borrowed range must not carry the brand of a borrow of a
// different range.  The overload of mint_borrowed for an rvalue range whose
// elements outlive it takes its brand from its call site, and an explicit
// template argument after the owner lands in a pack that must be empty.

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
    using HeldBrand = decltype(first)::brand_type;
    auto second = ::fixy::mint_borrowed<Owner, std::span<int>, HeldBrand>(
        std::span<int>{second_range.data(), second_range.size()});
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return first.front() + second.front();
}
