// The body of the frame door with_view of a GrowList captures an object that
// holds a reference to an array of a class that the unit only declares.  The
// store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) reads the element of an array, and it cannot
// read the parts of that class.  So the check stops the build, and the error
// names the class and the cause.  A capture of an array of a complete class
// passes (test/layer/checks/foundation/core/CaptureStore.cpp).

#include <foundation/core/Grow.h>
#include <foundation/core/Region.h>

#include <cstddef>

namespace {

struct Hidden;

template <class Row>
struct HoldsRow {
    Row& row;
};

[[nodiscard]] std::size_t count_beside(HoldsRow<Hidden const[2]> const& holder) {
    auto list = ::foundation::core::mint_grow_list<int>(64);
    return list.with_view([&holder](::foundation::core::View<int const> elements) noexcept {
        static_cast<void>(holder);
        return elements.size();
    });
}

}  // namespace

int main() {
    static_cast<void>(&count_beside);
    return 0;
}
