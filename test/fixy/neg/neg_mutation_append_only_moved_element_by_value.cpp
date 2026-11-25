// A log gives an element by value only when its copy is trivial and it holds no
// borrow.  An element that only moves is read in a frame, so its subscript binds
// to the deleted overload.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

struct MoveOnly {
    int value = 0;
    MoveOnly() noexcept = default;
    MoveOnly(MoveOnly&& other) noexcept : value{other.value} {}
};

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto log = fixy::mint_append_only<MoveOnly>();
    log.emplace(alloc);
    return log[0].value;
}
