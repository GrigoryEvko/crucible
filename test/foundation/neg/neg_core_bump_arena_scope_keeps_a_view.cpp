// The body of with_bump_arena keeps the View of a run of the arena of the
// scope in an Option that it captures by reference.  The arena ends with the
// scope, so the caller would read a View of freed bytes after the scope.  The
// door refuses a body that can store a borrow outside the scope
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

int main() {
    ::foundation::core::Option<::foundation::core::View<std::uint32_t>> kept{::foundation::core::none};
    ::foundation::core::with_bump_arena(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096}, [&kept](auto& arena) noexcept {
            auto run = arena.template array<std::uint32_t>(::foundation::effects::testing::test().alloc, 4, 7u);
            kept = ::foundation::core::some(run.view());
        });
    return kept.is_some() ? 1 : 0;
}
