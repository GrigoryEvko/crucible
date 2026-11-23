// A class that states its own end holds a full Option of a token as a data
// member.  The drop door would end the class, and the Option would end with
// its token in the frame of the door, with no consume.  An Option of a token
// states no end of its own, so the deleted twin of the door refuses the class.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Choice.h>

namespace {
struct[[= ::foundation::linear::linear{}]] Token {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr Token() noexcept = default;
    Token(const Token&) = delete("a linear token has one owner");
    constexpr Token(Token&&) noexcept = default;
    constexpr Token& operator=(Token&&) noexcept { return *this; }
    ~Token() = default;
};

struct[[= ::foundation::linear::linear{}]] Holder {
    ::foundation::core::Option<Token> held;
};
}  // namespace

int main() {
    ::foundation::linear::drop(Holder{::foundation::core::Option<Token>::some(Token{})});
    return 0;
}
