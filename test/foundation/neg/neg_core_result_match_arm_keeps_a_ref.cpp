// The value arm of the borrow match of a Result keeps a Ref of the value in
// an Option that it captures by reference.  The caller reads the Ref after
// the match, and a write of the Result ends the value that the Ref names.
// The store check refuses the arm (LentArmStoresNoBorrow of
// foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

namespace {
enum class Code : unsigned char {
    closed = 1
};
}  // namespace

int main() {
    ::foundation::core::Result<int, Code> const held{5};
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    return held.match(
        [&kept](int const& value) noexcept {
            kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(value)));
            return value;
        },
        [](Code const&) noexcept { return 0; });
}
