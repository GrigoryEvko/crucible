// A body of the read door of a SharedRead captures by reference an Option of a
// View, which the caller reads after the door.  The body can keep the View of
// the elements there, and the View would outlive the share of the guard.  The
// door refuses a body that can store a borrow outside it (StoresNoBorrowOutside
// of foundation/core/CaptureStore.h).

#include <fixy/SharedRegion.h>
#include <foundation/core/Choice.h>

#include <cstddef>

namespace {
struct Cache final {};
struct ReadBrand final {};

[[maybe_unused]] std::size_t read(::fixy::SharedRead<int, Cache, ReadBrand> const& shared) noexcept {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    return shared.with_view(
        [&kept](::foundation::core::View<int const> run) noexcept { return run.size() + (kept.is_some() ? 1 : 0); });
}
}  // namespace

int main() { return 0; }
