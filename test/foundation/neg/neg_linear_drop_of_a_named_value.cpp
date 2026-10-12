// The drop door takes a linear value that the caller gives up.  A named
// value stays with the caller, so a drop of it would leave a second name
// for the value.  The deleted twin of the door gives the reason.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>

namespace {
struct[[= ::foundation::linear::linear{}]] Token {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr Token() noexcept = default;
    Token(const Token&) = delete("a linear token has one owner");
    constexpr Token(Token&&) noexcept = default;
    constexpr Token& operator=(Token&&) noexcept { return *this; }
    ~Token() = default;
};
}  // namespace

int main() {
    Token token;
    ::foundation::linear::drop(token);
    return 0;
}
