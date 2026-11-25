// The function of then of a Computation that the caller gives up takes the
// payload by an rvalue reference and captures by reference an Option of a View,
// which the caller reads after then.  The function can keep a borrow of the
// payload there.  then refuses a function that takes the payload by reference
// and can store a borrow outside the call (LentArmStoresNoBorrow of
// foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Computation.h>

#include <utility>

namespace fe = ::foundation::effects;

int main() {
    fe::Computation<fe::Row<>, int> computation = fe::Computation<fe::Row<>, int>::mint_computation(1);
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto chained = std::move(computation).then([&kept](int&& value) noexcept {
        return fe::Computation<fe::Row<>, int>::mint_computation(kept.is_some() ? 0 : value);
    });
    static_cast<void>(chained);
    return 0;
}
