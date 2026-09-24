// A buffer of aggregates that each hold an exclusive token.  The elements
// start their lifetime over the allocated bytes, and a token has no
// lifetime that starts that way.  The element concept walks each subobject
// and refuses the type.

#include <foundation/AlignedBuffer.h>
#include <foundation/permissions/Permission.h>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct HoldsToken {
    fp::Permission<Region> token;
};
}  // namespace

int main() {
    auto buffer = ::foundation::AlignedBuffer<HoldsToken>::allocate(4);
    return buffer.size() == 4 ? 0 : 1;
}
