// The move of an Option moves its payload.  A payload whose move can throw
// would leave a half-moved Option, so the payload gate refuses it.

#include <foundation/core/Choice.h>

struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

int main() { return ::foundation::core::Option<ThrowingMove>{}.is_some() ? 1 : 0; }
