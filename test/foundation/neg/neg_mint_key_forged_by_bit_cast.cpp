// The mint key built from a byte.  The key is the gate of every mint,
// so a forged key mints any Permission.  The constructors of the key are
// user-provided, so the key is not trivially copyable, and std::bit_cast
// has no candidate.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <bit>
#include <memory>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto key = std::bit_cast<fp::perm_mint_key>(char{0});
    (void)key;
    return 0;
}
