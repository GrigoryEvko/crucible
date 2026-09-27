// release() binds only to an rvalue handle, so a release is one explicit
// move of the handle.  A call on a named handle that it does not move is
// refused, even with a leak atom as the witness.

#include <fixy/OwnedFile.h>

#include <cstdio>

namespace {

struct ClosedElsewhere final {};

}  // namespace

int main() {
    fixy::OwnedFile handle{};
    std::FILE* const stream = handle.release(fixy::atom::leak::resource<ClosedElsewhere>{});
    return stream == nullptr ? 0 : 1;
}
