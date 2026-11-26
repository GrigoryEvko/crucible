// An arm of an rvalue match gives a Box of a View.  A Box owns its object, so
// the borrow walk reads its type argument, and a View is a borrow.  So the
// result holds a borrow, and the match refuses it (HoldsNoBorrow of
// foundation/core/Choice.h), as it refuses the View alone.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>

#include <utility>

::foundation::core::Box<::foundation::core::View<int const>> box_of_a_view() noexcept;

int main() {
    ::foundation::core::Option<int> held{::foundation::core::none};
    auto const kept =
        std::move(held).match([](int) noexcept { return box_of_a_view(); }, []() noexcept { return box_of_a_view(); });
    static_cast<void>(kept);
    return 0;
}
