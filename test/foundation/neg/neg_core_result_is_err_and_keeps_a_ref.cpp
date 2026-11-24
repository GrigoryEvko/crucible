// The test of is_err_and keeps a Ref of the error in an Option that it
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
    ::foundation::core::Result<int, Code> const held{::foundation::core::err(Code::closed)};
    ::foundation::core::Option<::foundation::core::Ref<Code const>> kept{::foundation::core::none};
    bool const is_closed = held.is_err_and([&kept](Code const& code) noexcept {
        kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(code)));
        return code == Code::closed;
    });
    return static_cast<int>(is_closed);
}
