// An owner names a producer state that holds a borrow.  An owner door could
// then store a borrow of a caller in an end, and the store mark of an end
// would not hold.  A side state holds no borrow (SpscSideState), so the mint
// refuses the owner.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/effects/Effect.h>

namespace {

struct BorrowBrand final {};

struct BorrowState final {
    ::foundation::core::Option<::foundation::core::Ref<int, BorrowBrand>> held{};
};

struct Owner final {
    using producer_state = BorrowState;
    struct consumer_state {};
    Owner() noexcept = default;
    Owner(Owner const&) = delete("pinned");
    Owner(Owner&&) = delete("pinned");
};

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    static_cast<void>(minted);
    return 0;
}
