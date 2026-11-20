// A private mapping holds a copy of the pages for this process alone, so
// no other writer stores its words.  The gate of shared_words_at asks for
// a mapping of the share Shared.

#include <fixy/os/OwnedMmap.h>

#include <cstdint>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Private, RegionBrand>;
}  // namespace

int main() {
    Mapping const mapping{};
    return static_cast<int>(mapping.shared_words_at<std::uint64_t, 4>(::fixy::mmap::MappingOffset{0}).is_none());
}
