// A move assignment drops the payload that the target holds, and a linear
// payload must not be dropped, so the assignment of an Option of a linear
// payload is refused.

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
    auto target = ::foundation::core::Option<Token>::some(Token{1});
    auto source = ::foundation::core::Option<Token>::some(Token{2});
    target = static_cast<::foundation::core::Option<Token>&&>(source);
    return target.is_some() ? 0 : 1;
}
