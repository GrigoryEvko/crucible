// The test of is_none_or keeps a Ref of the payload in an Option that it
// captures by reference, for the reason of is_some_and.  The store check
// refuses the test (LentArmStoresNoBorrow of foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::Option<int> const held = ::foundation::core::Option<int>::some(4);
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    bool const is_even = held.is_none_or([&kept](int const& value) noexcept {
        kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(value)));
        return value % 2 == 0;
    });
    return static_cast<int>(is_even);
}
