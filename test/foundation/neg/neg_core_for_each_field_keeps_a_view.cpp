// The visitor of a walk of the fields captures by reference an Option of a
// View, which the caller reads after the walk.  The visitor can keep a borrow of
// a field there.  The walk refuses a visitor that can store a borrow outside it
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>

namespace {
struct Pair {
    int first = 1;
    int second = 2;
};
}  // namespace

int main() {
    Pair const pair{};
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    ::foundation::core::for_each_field(pair, [&kept](auto const& field) noexcept {
        static_cast<void>(field);
        static_cast<void>(kept.is_some());
    });
    return 0;
}
