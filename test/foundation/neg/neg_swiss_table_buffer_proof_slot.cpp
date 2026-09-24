// A table whose slot holds an exclusive token.  The slots start their
// lifetime over the allocated bytes, and a token has no lifetime that
// starts that way, so each slot would name a token that was never built.
// The slot concept walks each subobject of the slot type and refuses it.

#include <foundation/SwissTableBuffer.h>
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
    auto table = ::foundation::SwissTableBuffer<HoldsToken>::allocate(16);
    return table.capacity() == 16 ? 0 : 1;
}
