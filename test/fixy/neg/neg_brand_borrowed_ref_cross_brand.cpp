// Two mints of a reference borrow are two brands, so a callee that
// asks for two borrows of one minting refuses borrows minted at two
// sites by deduction.

#include <fixy/Borrowed.h>

namespace {
template <class Brand>
constexpr void same_minting(::fixy::BorrowedRef<int, Brand>, ::fixy::BorrowedRef<int, Brand>) noexcept {}
}  // namespace

int main() {
    int value = 1;
    auto first = ::fixy::mint_borrowed_ref(value);
    auto second = ::fixy::mint_borrowed_ref(value);
    same_minting(first, second);
    return 0;
}
