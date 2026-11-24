// The test of is_ok_and keeps a Ref of the value in an Option that it
// captures by reference.  The store check refuses the test
// (LentArmStoresNoBorrow of foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

namespace {
enum class Code : unsigned char {
    closed = 1
};
}  // namespace

int main() {
    ::foundation::core::Result<int, Code> const held{6};
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    bool const is_even = held.is_ok_and([&kept](int const& value) noexcept {
        kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(value)));
        return value % 2 == 0;
    });
    return static_cast<int>(is_even);
}
