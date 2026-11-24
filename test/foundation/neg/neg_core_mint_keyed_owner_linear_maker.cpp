// A maker holds a linear token as an empty member, so the maker is empty.  The
// mint calls the maker with no consume, and the token would end with no door.
// The deleted twin of the mint refuses each maker that holds a linear value.

#include <foundation/ByteSeal.h>
#include <foundation/core/Array.h>

struct[[= ::foundation::linear::linear{}]] Token final {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    Token() noexcept = default;
    Token(Token&&) noexcept = default;
    Token& operator=(Token&&) noexcept { return *this; }
    Token(Token const&) = delete;
};

struct TokenArrayOwner final {
    [[no_unique_address]] Token token{};

    template <::foundation::core::IsKeyBrand Brand>
    [[nodiscard]] constexpr ::foundation::core::FixedArray<int, 4, Brand>
    operator()(::foundation::core::MintedBrand<Brand> /*proof*/) const noexcept {
        return ::foundation::core::FixedArray<int, 4, Brand>{};
    }
};

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(TokenArrayOwner{});
    static_cast<void>(key);
    return array.at(::foundation::core::index<0>());
}
