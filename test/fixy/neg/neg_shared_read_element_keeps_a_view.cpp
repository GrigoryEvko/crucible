// The arm of the element door of a SharedRead takes the element by reference and
// captures by reference an Option of a View, which the caller reads after the
// door.  The arm can keep a borrow of the element there, and the borrow would
// outlive the share of the guard.  The door refuses such an arm
// (LentArmStoresNoBorrow of foundation/core/Choice.h).

#include <fixy/SharedRegion.h>
#include <foundation/core/Choice.h>

namespace {
struct Cache final {};
struct ReadBrand final {};

[[maybe_unused]] int read(::fixy::SharedRead<int, Cache, ReadBrand> const& shared) noexcept {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    return shared.with_element(
        0uz, [&kept](int const& value) noexcept { return kept.is_some() ? 0 : value; }, []() noexcept { return 0; });
}
}  // namespace

int main() { return 0; }
