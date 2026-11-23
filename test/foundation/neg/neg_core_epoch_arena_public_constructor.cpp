// An epoch arena has no public constructor: only mint_epoch_arena builds it,
// and it takes a member tag only.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch{
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096}};
};

int main() {
    Owner const owner{};
    return static_cast<int>(owner.scratch.retained_bytes());
}
