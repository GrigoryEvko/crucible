// The body of the frame door with_view of a SmallList captures by const
// reference an object that holds, by value, a part with a mutable Option of
// a View.  C++ const does not go through a mutable member, also in a part of
// the object, so the body keeps the View of the frame there.  The store check
// of the door (StoresNoBorrowOutside of foundation/core/CaptureStore.h) reads
// each mutable member of a const object as writable, so it refuses the body.

#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/SmallList.h>

#include <cstddef>

namespace {

struct Cache {
    mutable ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
};

struct Reader {
    Cache cache{};
};

}  // namespace

int main() {
    auto list = ::foundation::core::mint_small_list<int, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    Reader const reader{};
    list.with_view([&reader](::foundation::core::View<int const> elements) noexcept {
        reader.cache.kept = ::foundation::core::some(elements);
    });
    return reader.cache.kept.is_some() ? 0 : 1;
}
