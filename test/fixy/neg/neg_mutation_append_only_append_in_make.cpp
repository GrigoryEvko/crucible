// The make of append_with appends to the log that it builds an element for.  The
// two appends would take one slot.  The rule borrow-match refuses the append,
// because it is a call of a member of the receiver that is not const.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto log = fixy::mint_append_only<int>();
    log.append_with(alloc, [&log, alloc](std::size_t) noexcept {
        log.append(alloc, 2);
        return 1;
    });
    return static_cast<int>(log.size());
}
