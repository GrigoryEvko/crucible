// A buffer of exclusive tokens built by value initialization.  A token has
// no default constructor anyone may call, so no route builds one inside a
// buffer: the value-initializing allocation requires a default constructor
// that does not throw.

#include <foundation/AlignedBuffer.h>
#include <foundation/permissions/Permission.h>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto buffer = ::foundation::AlignedBuffer<fp::Permission<Region>>::allocate_value_initialized(4);
    return buffer.size() == 4 ? 0 : 1;
}
