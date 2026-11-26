// A function outside the base states two_names_door, and its body gives a
// linear token and a closure that captures the token to one call.  The kind
// two-names of the rule view-escape trusts the annotation only in a base file,
// so it refuses the call.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>

namespace {

struct[[= ::foundation::linear::linear{}]] Token {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal{};
    int count = 0;
    constexpr Token() noexcept = default;
    constexpr Token(Token&&) noexcept = default;
    constexpr Token& operator=(Token&&) noexcept = default;
    Token(const Token&) = delete;
};

template <class Body>
void write_beside(Token& token, Body& body) noexcept {
    token.count = body() + 1;
}

[[maybe_unused]][[= ::foundation::linear::two_names_door{}]] void door_outside_the_base(Token& token) noexcept {
    auto read = [&token]() noexcept { return token.count; };
    write_beside(token, read);
}

}  // namespace

int main() { return 0; }
