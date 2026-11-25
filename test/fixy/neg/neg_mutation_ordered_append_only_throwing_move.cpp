// An ordered log moves each item into it, and its append does not throw.  So the
// door of the log refuses an element whose move can throw.

#include <fixy/Mutation.h>

struct ThrowingMove {
    int value = 0;
    ThrowingMove() noexcept = default;
    ThrowingMove(ThrowingMove&& other) noexcept(false) : value{other.value} {}
};

struct ValueOfMove {
    [[nodiscard]] constexpr int operator()(ThrowingMove const& item) const noexcept { return item.value; }
};

int main() {
    auto log = fixy::mint_ordered_append_only<ThrowingMove, ValueOfMove>();
    return static_cast<int>(log.size());
}
