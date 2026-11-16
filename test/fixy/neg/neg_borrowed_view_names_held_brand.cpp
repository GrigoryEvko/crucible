// A caller cannot give a borrow of a base view the brand of a borrow that
// it holds.  The pack Refused of the mint takes each explicit template
// argument after the owner, and the constraint refuses it, so the brand of
// the second borrow is fresh or the call does not compile.

#include <fixy/Borrowed.h>
#include <fixy/Core.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    ::fixy::FixedArray<int, 2> array{};
    auto first = ::fixy::mint_borrowed<Owner>(array.view());
    using HeldBrand = decltype(first)::brand_type;
    auto second = ::fixy::mint_borrowed<Owner, ::fixy::View<int, 2>, HeldBrand>(array.view());
    return first.front() + second.front();
}
