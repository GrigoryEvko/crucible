// A growth of a log moves each element, and a move that throws could lose one.
// So the door of the log refuses an element whose move can throw.

#include <fixy/Mutation.h>

struct ThrowingMove {
    int value = 0;
    ThrowingMove() noexcept = default;
    ThrowingMove(ThrowingMove&& other) noexcept(false) : value{other.value} {}
};

int main() {
    auto log = fixy::mint_append_only<ThrowingMove>();
    return static_cast<int>(log.size());
}
