// An object of an arena keeps a Ref of a second arena of the same family, and the
// second arena is a local object that ends first.  to() gives an arena a fresh brand
// and not its tag, so the Ref does not fit a field that names the tag
// (carrier_brand_t of foundation/core/Ref.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
using Bytes = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;
using Arena = ::foundation::core::BumpArena<Family>;

struct Holder {
    ::foundation::core::Ref<Arena, Family> kept;
};
}  // namespace

int main() {
    Arena long_lived = ::foundation::core::mint_bump_arena<Family>(Bytes{4096});
    Arena short_lived = ::foundation::core::mint_bump_arena<Family>(Bytes{4096});
    Holder const holder{::foundation::core::to(short_lived)};
    static_cast<void>(long_lived.make<Holder>(::foundation::effects::testing::test().alloc, holder));
    return 0;
}
