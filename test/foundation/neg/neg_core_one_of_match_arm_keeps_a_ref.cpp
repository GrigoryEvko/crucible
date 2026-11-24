// An arm of the borrow match of a OneOf keeps a Ref of its alternative in an
// Option that it captures by reference.  The caller reads the Ref after the
// match, and a write of the OneOf ends the alternative that the Ref names.
// The store check refuses the arm (LentArmStoresNoBorrow of
// foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::OneOf<int, long> const held{7};
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    return held.match(
        [&kept](int const& value) noexcept {
            kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(value)));
            return value;
        },
        [](long const& value) noexcept { return static_cast<int>(value); });
}
