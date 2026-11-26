// The drain of a FixedList gives each element to its body from the list that
// the door holds, and that list ends with the door.  A body that takes the
// element by reference and captures an Option of a View by reference could
// keep a borrow of the element there, and the caller could read it after the
// drain.  The drain refuses such a body (LentArmStoresNoBorrow of
// foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/List.h>
#include <foundation/core/Region.h>

#include <cstdint>
#include <utility>

int main() {
    ::foundation::core::FixedList<std::uint64_t, 4> list{};
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    std::move(list).drain([&kept](std::uint64_t const& element) noexcept {
        static_cast<void>(element);
        static_cast<void>(kept.is_some());
    });
    return 0;
}
