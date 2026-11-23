// The compile-time checks of fixy/concurrent/SpscEnds.h.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/core/Frame.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Effect.h>

namespace fixy::concurrent::detail::spsc_ends_checks {

// An owner with two empty states, and the brands of two splits of it.
struct CheckOwner final {
    struct producer_state {};
    struct consumer_state {};
    CheckOwner() noexcept = default;
    CheckOwner(CheckOwner const&) = delete("an owner of a split is pinned");
    CheckOwner(CheckOwner&&) = delete("an owner of a split is pinned");
};
struct CheckBrand final {};
struct OtherBrand final {};

using ProducerBrand = SpscSideBrand<CheckBrand, SpscSide::producer>;
using ConsumerBrand = SpscSideBrand<CheckBrand, SpscSide::consumer>;

// A root door mints one key of each brand that its body names
// (foundation/core/RootKeys.h).  A body that borrows a split and names a key
// of one of its sides would then hold a second key of a live side, beside the
// key of the mint.  The walk of the borrow refuses such a body for each side,
// so the refusal does not rest on the Sendable test of a thread door.  A body
// that borrows a split of another brand passes, so the walk reads the split.
struct BorrowsTheSplit {
    SpscSplit<CheckOwner, CheckBrand>& split;
    void operator()(ConsumerKey<CheckBrand>& /*key*/) noexcept {}
};
struct BorrowsAnotherSplit {
    SpscSplit<CheckOwner, OtherBrand>& split;
    void operator()(ConsumerKey<CheckBrand>& /*key*/) noexcept {}
};
static_assert(::foundation::core::NamesRootKeys<BorrowsTheSplit, 0>);
static_assert(!::foundation::core::BorrowsNoKeyOwner<BorrowsTheSplit, ConsumerBrand>);
static_assert(!::foundation::core::BorrowsNoKeyOwner<BorrowsTheSplit, ProducerBrand>);
static_assert(::foundation::core::BorrowsNoKeyOwner<BorrowsAnotherSplit, ConsumerBrand>);
static_assert(::foundation::core::BorrowsNoKeyOwner<BorrowsAnotherSplit, ProducerBrand>);

// The same refusal for the fresh brand of mint_spsc_split.  GCC 16 lists no
// member alias of a specialization whose argument is a fresh brand, so the
// statement of the side brand is a base (states_owner_brand of
// foundation/core/Frame.h), and this check reads the split of a real mint.
// No code calls the function: its checks hold where it stands.
struct FreshSplitCheck {
    static void check(::foundation::effects::Alloc allocation) noexcept {
        [[maybe_unused]] auto [split, producer_key, consumer_key] = mint_spsc_split<CheckOwner>(allocation);
        using Brand = typename decltype(split)::brand_type;
        auto const borrows_the_split = [&split](ConsumerKey<Brand>& /*key*/) noexcept { static_cast<void>(split); };
        static_assert(::foundation::core::NamesRootKeys<decltype(borrows_the_split), 0>);
        static_assert(!::foundation::core::BorrowsNoKeyOwner<decltype(borrows_the_split),
                                                             SpscSideBrand<Brand, SpscSide::consumer>>);
        static_assert(!::foundation::core::BorrowsNoKeyOwner<decltype(borrows_the_split),
                                                             SpscSideBrand<Brand, SpscSide::producer>>);
    }
};

}  // namespace fixy::concurrent::detail::spsc_ends_checks
