// A part that moves with a throw could stop the move of a Record after one
// part, so the part gate refuses it.

#include <foundation/core/Record.h>

struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

int main() { return static_cast<int>(sizeof(::foundation::core::Record<ThrowingMove>)); }
