// A class outside the base states keeps_own_borrows, the mark of a handle whose
// doors store only borrows of its own brand, such as an epoch.  The store check
// trusts the mark only in a class of the base (foundation/core/TypeParts.h), so
// it reads the members of this class, and the Option of a View in it is a
// borrow slot.  The predicate of find_if captures the class by reference, so
// find_if refuses it.

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/TypeParts.h>

namespace {
struct[[= ::foundation::core::keeps_own_borrows{}]] Stash {
    ::foundation::core::Option<::foundation::core::View<int const>> held{};
};
}  // namespace

int main() {
    ::foundation::core::FixedArray<int, 3> const values{1, 2, 3};
    Stash stash{};
    auto const found = ::foundation::core::find_if(
        values.view(), [&stash](int const& value) noexcept { return value == 2 && stash.held.is_none(); });
    return static_cast<int>(found.is_some());
}
