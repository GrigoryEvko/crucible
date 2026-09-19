// release() hands a mapping to something that will unmap it elsewhere,
// so the witness parameter is constrained to a leak atom.  An unrelated
// empty struct is not one, and the port made that witness a reflection
// query over the atom catalog rather than a trait a translation unit
// could specialize, so this door cannot be opened from outside.

#include <fixy/OwnedMmap.h>

#include <utility>

namespace {

struct RegionTag {};
struct ProtTag {};
struct ShareTag {};

struct NotALeakAtom final {};

}  // namespace

int main() {
    fixy::OwnedMmap<RegionTag, ProtTag, ShareTag> region{};
    auto [addr, len] = std::move(region).release(NotALeakAtom{});
    return (addr == nullptr && len == 0) ? 0 : 1;
}
