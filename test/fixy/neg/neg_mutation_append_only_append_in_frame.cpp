// The body of a frame of a log appends to the log that it reads.  The append can
// move the storage that the View of the frame borrows.  The rule borrow-match
// refuses the append, because it is a call of a member of the receiver that is
// not const.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto log = fixy::mint_append_only<int>();
    log.append(alloc, 1);
    log.with_view([&log, alloc](::foundation::core::View<int const> held) noexcept {
        for (int const item : held) {
            log.append(alloc, item);
        }
    });
    return static_cast<int>(log.size());
}
