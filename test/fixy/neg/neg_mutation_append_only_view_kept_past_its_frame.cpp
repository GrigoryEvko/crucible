// The body of a frame of a log writes its View into a variable of the caller.
// The next append can move the storage that the View borrows.  The store check
// of the door refuses the body, and the deleted overload gives the reason.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto log = fixy::mint_append_only<int>();
    log.append(alloc, 1);
    ::foundation::core::View<int const> kept{};
    log.with_view([&kept](::foundation::core::View<int const> held) noexcept { kept = held; });
    log.append(alloc, 2);
    return static_cast<int>(kept.size());
}
