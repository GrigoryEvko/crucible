// The body of the frame door with_view of a SmallList captures by const
// reference a closure whose [this] points to a holder of an Option of a View.
// C++ const does not go through a pointer, so the body calls the closure, and
// the closure keeps the View of the frame in the holder.  The store check of
// the door (StoresNoBorrowOutside of foundation/core/CaptureStore.h) reads
// the target of each pointer to an object that is not const as writable,
// also in a const object, so it refuses the body.

#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/SmallList.h>

#include <cstddef>

namespace {

struct Holder {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};

    [[nodiscard]] auto writer() noexcept {
        return [this](::foundation::core::View<int const> elements) noexcept {
            kept = ::foundation::core::some(elements);
        };
    }
};

}  // namespace

int main() {
    auto list = ::foundation::core::mint_small_list<int, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    Holder holder{};
    auto const writer = holder.writer();
    list.with_view([&writer](::foundation::core::View<int const> elements) noexcept { writer(elements); });
    return holder.kept.is_some() ? 0 : 1;
}
