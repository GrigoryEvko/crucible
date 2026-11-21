// replace gives the old payload back as an Option.  An Option of a linear
// payload is linear, so a statement that drops the result of replace drops
// the old payload.  The door is [[nodiscard]], so the statement is refused.

#include <foundation/Linear.h>
#include <foundation/core/Choice.h>

struct[[= ::foundation::linear::linear{}]] Token {
    int handle = 0;
    explicit Token(int value) noexcept : handle{value} {}
    Token(Token&&) noexcept = default;
    Token(Token const&) = delete;
    Token& operator=(Token&& other) noexcept {
        handle = other.handle;
        return *this;
    }
};

int main() {
    auto held = ::foundation::core::Option<Token>::some(Token{1});
    held.replace(Token{2});
    Token const last = static_cast<::foundation::core::Option<Token>&&>(held).expect("the Option holds a token");
    return last.handle == 2 ? 0 : 1;
}
