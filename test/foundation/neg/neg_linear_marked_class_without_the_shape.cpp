// A class that carries the annotation linear and can be copied.  A copy
// of a linear value is a second owner of one resource, and a trivially
// copyable class is a class that std::bit_cast builds from bytes.  The
// trait refuses the class, so no reader of the trait gets an answer for
// it.

#include <foundation/Linear.h>

namespace {
struct[[= ::foundation::linear::linear{}]] Copyable {
    int value = 0;
};
}  // namespace

static_assert(::foundation::linear::is_linear_v<Copyable>);

int main() { return 0; }
