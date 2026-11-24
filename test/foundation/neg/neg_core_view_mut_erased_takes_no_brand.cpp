// The erased view of a run takes no brand back.  The body of the frame of one
// array names the brand of its run, and a door of that run would take a view
// of that brand.  The erased view of the run of a second array does not
// convert to it, so it enters no door that names the brand of another run.

#include <foundation/core/Array.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 4> first{1, 2, 3, 4};
    ::foundation::core::FixedArray<int, 4> second{5, 6, 7, 8};
    auto const count = first.with_mut([&second](auto first_run) noexcept -> std::size_t {
        using FirstRun = decltype(first_run);
        return second.with_mut([](auto second_run) noexcept -> std::size_t {
            FirstRun forged = ::foundation::core::erase_brand(std::move(second_run));
            return forged.size();
        });
    });
    return static_cast<int>(count);
}
