// release() hands a mapping to something that will unmap it elsewhere,
// so the witness parameter is constrained to a leak atom.  An unrelated
// empty struct is not one.  The witness is a reflection query over the
// atom catalog rather than a trait a translation unit could specialize,
// so this door cannot be opened from outside.

#include <fixy/OwnedMmap.h>

#include <utility>

namespace {

struct RegionTag {};

struct NotALeakAtom final {};

}  // namespace

int main() {
    fixy::OwnedMmap<RegionTag, fixy::mmap::prot::ReadOnly, fixy::mmap::share::Private> region{};
    auto [addr, len] = std::move(region).release(NotALeakAtom{});
    return (addr == nullptr && len == 0) ? 0 : 1;
}
