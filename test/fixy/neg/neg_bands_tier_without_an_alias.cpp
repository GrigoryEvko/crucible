// every_tier_has_an_alias walks a namespace for alias templates,
// instantiates each at a probe type, and reads the tier back off the
// band.  The seven assertions in Bands.h all pass, so none of them
// shows what the walk does when a tier has no alias.
//
// This namespace mirrors fixy::hot_path and leaves one of the three
// tiers out.  The walk must answer no, or the seven assertions in the
// header prove only that the aliases they found were well formed.
//
// The condition is written as a comparison so the compiler reports the
// value it reduced to.  A bare call would give only the message below,
// which is the fixture's own text and proves nothing on its own.

#include <fixy/Bands.h>

namespace incomplete_hot_path {

template <typename T>
using Hot = fixy::HotPath<fixy::HotPathTier_v::Hot, T>;
template <typename T>
using Warm = fixy::HotPath<fixy::HotPathTier_v::Warm, T>;
// Cold is deliberately absent.

}  // namespace incomplete_hot_path

int main() {
    constexpr bool covers_every_tier =
        fixy::detail::every_tier_has_an_alias<^^incomplete_hot_path, std::meta::dealias(^^fixy::HotPathTier_v)>();
    static_assert(covers_every_tier == true, "a namespace missing one tier alias covers its enum");
    return 0;
}
