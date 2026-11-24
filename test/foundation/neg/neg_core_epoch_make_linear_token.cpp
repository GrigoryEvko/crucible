// make of an epoch builds a linear end in the epoch.  The epoch ends no
// object, so the end would end with the epoch, with no consume.  make
// refuses a type that holds a linear value (CanMakeInArena).

#include <foundation/Linear.h>
#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace {
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};
}  // namespace

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};

int main() {
    Owner owner{};
    using ScratchEpoch = ::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>;
    return owner.scratch.with_epoch([](ScratchEpoch& epoch) noexcept -> int {
        auto const end = epoch.make<End>(::foundation::effects::testing::test().alloc, End{7});
        return end->handle;
    });
}
