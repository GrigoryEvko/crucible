// A body that no door called captures a full Option of a token by value.  The
// door end_unrun_body would end the body, and the Option would end with its
// token in the frame of the door, with no consume.  An Option of a token
// states no end of its own, so the deleted twin of the door refuses the body.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Choice.h>

#include <utility>

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
    auto body = [held = ::foundation::core::Option<Token>::some(Token{})]() noexcept { return held.is_some(); };
    ::foundation::linear::end_unrun_body(std::move(body), ::foundation::linear::UnrunReason::refusal_test);
    return 0;
}
