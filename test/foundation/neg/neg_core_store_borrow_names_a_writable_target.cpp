// Negative-compile test. This file must fail to compile.
//
// The body of with_bump_arena captures by value a borrow whose first type
// argument is const and whose statement names a target that is not const.
// Through the borrow, the body keeps the View of a run of the arena of the
// scope in the target, and the caller would read a View of freed bytes after
// the scope.  The store check reads the target from the statement
// (borrow_target of foundation/core/TypeParts.h), so the door refuses the
// body.

#include <foundation/Transfer.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace borrow_target {

struct Holder {
    ::foundation::core::Option<::foundation::core::View<std::uint32_t>> kept{::foundation::core::none};
};

template <class First, class Target>
class[[= ::foundation::transfer::refers{^^Target}]] Named {
public:
    explicit Named(Target& target) noexcept : target_{target} {}
    [[nodiscard]] Target& get() const noexcept { return target_; }

private:
    Target& target_;
};

}  // namespace borrow_target

int main() {
    borrow_target::Holder holder{};
    borrow_target::Named<borrow_target::Holder const, borrow_target::Holder> const named{holder};
    ::foundation::core::with_bump_arena(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096}, [named](auto& arena) noexcept {
            auto run = arena.template array<std::uint32_t>(::foundation::effects::testing::test().alloc, 4, 7u);
            named.get().kept = ::foundation::core::some(run.view());
        });
    return holder.kept.is_some() ? 1 : 0;
}
