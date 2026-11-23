// The Epoch of an epoch arena has no public constructor: only with_epoch
// builds it, in its own frame, so no code opens an epoch outside the door.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};

int main() {
    Owner owner{};
    ::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>> epoch{owner.scratch};
    static_cast<void>(epoch);
    return 0;
}
