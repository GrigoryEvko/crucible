// An arm of the borrow match of an Option captures by reference a Ref of the
// Option itself, and writes the Option through it.  The write ends the
// payload that the arm borrows.  The store check of the borrow match refuses
// an arm that reaches a borrow that it can write (LentArmStoresNoBorrow of
// foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::Option<int> held = ::foundation::core::Option<int>::some(3);
    ::foundation::core::Ref<::foundation::core::Option<int>> target =
        ::foundation::core::erase_brand(::foundation::core::to(held));
    return held.match(
        [&target](int const& value) noexcept {
            *target = ::foundation::core::none;
            return value;
        },
        [] noexcept { return 0; });
}
