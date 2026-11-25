// The visitor of a walk of the bases captures by reference an Option of a View,
// which the caller reads after the walk.  The visitor can keep a borrow of a
// base there.  The walk refuses a visitor that can store a borrow outside it
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>

namespace {
struct Left {
    int value = 1;
};
struct Both : Left {
    int more = 2;
};
}  // namespace

int main() {
    Both const both{};
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    ::foundation::core::for_each_base(both, [&kept](auto const& base) noexcept {
        static_cast<void>(base);
        static_cast<void>(kept.is_some());
    });
    return 0;
}
