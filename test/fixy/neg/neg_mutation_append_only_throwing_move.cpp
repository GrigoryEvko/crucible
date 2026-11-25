// append moves its item into the log and does not throw.  So append refuses an
// element whose move can throw.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

struct ThrowingMove {
    int value = 0;
    ThrowingMove() noexcept = default;
    ThrowingMove(ThrowingMove&& other) noexcept(false) : value{other.value} {}
};

int main() {
    auto log = fixy::mint_append_only<ThrowingMove>();
    log.append(::foundation::effects::testing::test().alloc, ThrowingMove{});
    return static_cast<int>(log.size());
}
