// The function of map of a const Computation takes the payload by reference and
// captures by reference an Option of a View, which the caller reads after map.
// The function can keep a borrow of the payload there.  map refuses a function
// that takes the payload by reference and can store a borrow outside the call
// (LentArmStoresNoBorrow of foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Computation.h>

namespace fe = ::foundation::effects;

int main() {
    fe::Computation<fe::Row<>, int> const computation = fe::Computation<fe::Row<>, int>::mint_computation(1);
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto mapped = computation.map([&kept](int const& value) noexcept { return kept.is_some() ? 0 : value; });
    static_cast<void>(mapped);
    return 0;
}
