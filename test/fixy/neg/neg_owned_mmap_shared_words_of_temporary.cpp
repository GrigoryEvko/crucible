// The shared words borrow the mapping.  A temporary mapping unmaps at the
// end of the full expression, so the words of a temporary are refused.

#include <fixy/os/OwnedMmap.h>

#include <cstdint>
#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Shared, RegionBrand>;
}  // namespace

int main() {
    Mapping mapping{};
    return static_cast<int>(
        std::move(mapping).shared_words_at<std::uint64_t, 4>(::fixy::mmap::MappingOffset{0}).is_none());
}
