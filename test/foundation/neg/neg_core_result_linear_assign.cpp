// A move assignment drops the part that the target holds, and a linear
// error must not be dropped, so the assignment of a Result of a linear
// error is refused.

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
    ::foundation::core::Result<int, Token> target{1};
    ::foundation::core::Result<int, Token> source{::foundation::core::err(Token{2})};
    target = static_cast<::foundation::core::Result<int, Token>&&>(source);
    return target.is_ok() ? 0 : 1;
}
