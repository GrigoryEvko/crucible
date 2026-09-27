// release() hands a stream to a caller who closes it elsewhere, so the
// witness parameter is constrained to a leak atom.  An unrelated empty
// struct is not one, and the witness is a reflection query over the atom
// catalog rather than a trait a translation unit could specialize.

#include <fixy/OwnedFile.h>

#include <cstdio>
#include <utility>

namespace {

struct NotALeakAtom final {};

}  // namespace

int main() {
    fixy::OwnedFile handle{};
    std::FILE* const stream = std::move(handle).release(NotALeakAtom{});
    return stream == nullptr ? 0 : 1;
}
