// A log never changes an element.  The View of its frame names const elements,
// so a body of the frame writes none.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto log = fixy::mint_append_only<int>();
    log.append(alloc, 1);
    log.with_view([](::foundation::core::View<int const> held) noexcept { *held.get(0).expect("one element") = 2; });
    return log[0];
}
