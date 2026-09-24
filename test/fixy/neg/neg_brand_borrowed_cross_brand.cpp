// Two mints of a borrow are two brands, so a callee that asks for two
// borrows of one minting refuses borrows minted at two sites by
// deduction.

#include <fixy/Borrowed.h>

namespace {
struct Owner {};

template <class Brand>
constexpr void same_minting(::fixy::Borrowed<int, Owner, Brand>, ::fixy::Borrowed<int, Owner, Brand>) noexcept {}
}  // namespace

int main() {
    int storage[3] = {1, 2, 3};
    auto first = ::fixy::mint_borrowed<Owner>(storage);
    auto second = ::fixy::mint_borrowed<Owner>(storage);
    same_minting(first, second);
    return 0;
}
