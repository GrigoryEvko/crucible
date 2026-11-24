// The body of the frame door with_view of a GrowList captures by const
// reference an object with a View of a class that the unit only declares.
// The store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) cannot read the parts of that class, and a
// const object of it can still hold a reference member.  So the check stops
// the build, and the error names the class and the cause.

#include <foundation/core/Grow.h>
#include <foundation/core/Region.h>

namespace {

struct Hidden;

struct Neighbors {
    ::foundation::core::View<Hidden const> hidden{};
};

}  // namespace

int main() {
    auto list = ::foundation::core::mint_grow_list<int>(64);
    Neighbors const neighbors{};
    auto const count = list.with_view([&neighbors](::foundation::core::View<int const> elements) noexcept {
        static_cast<void>(neighbors);
        return elements.size();
    });
    return static_cast<int>(count);
}
