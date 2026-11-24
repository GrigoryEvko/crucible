// Each read of a SharedView is an atomic load, so no door of it gives a plain
// reference to an element: a read through such a reference would be a plain
// load of memory that another writer stores.

#include <fixy/os/OwnedMmap.h>

#include <cstdint>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Shared, RegionBrand>;
}  // namespace

int main() {
    Mapping const mapping{};
    for (::fixy::mmap::SharedView<std::uint64_t const, 1> const words : mapping.view_at<std::uint64_t const, 1>(0)) {
        std::uint64_t const& word = words.at(::foundation::core::index<0>());
        return static_cast<int>(word);
    }
    return 0;
}
