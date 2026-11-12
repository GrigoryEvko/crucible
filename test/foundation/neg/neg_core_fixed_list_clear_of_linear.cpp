// clear() would end each linear element and lose what it proves, so a list
// of a linear element has no clear().

#include <foundation/ByteSeal.h>
#include <foundation/core/List.h>

struct[[= ::foundation::linear::linear{}]] Token final {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    Token() noexcept = default;
    Token(Token&&) noexcept = default;
    Token& operator=(Token&&) noexcept { return *this; }
    Token(Token const&) = delete;
};

int main() {
    ::foundation::core::FixedList<Token, 2> tokens;
    tokens.clear();
    return 0;
}
