// A second borrow of a borrowed range must not carry the brand of a borrow
// that the caller holds.  The overload of mint_borrowed for an rvalue range
// whose elements outlive it takes its brand from its call site, and an
// explicit template argument after the owner lands in a pack that must be
// empty.  The refusal is at the level of the type, so the two borrows can
// name one range.

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
    using HeldBrand = decltype(first)::brand_type;
    auto second = ::fixy::mint_borrowed<Owner, std::span<int>, HeldBrand>(std::span<int>{range});
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return first.front() + second.front();
}
