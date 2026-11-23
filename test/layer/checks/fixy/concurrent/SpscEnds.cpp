// The compile-time checks of fixy/concurrent/SpscEnds.h.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/Transfer.h>
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

// A split stays on one thread: two threads cannot write it at the same time,
// also when its owner holds no state that two threads cannot write.  The
// Box of the owner and the state of each end are plain, and a loan key
// proves no thread.
static_assert(::foundation::transfer::Synchronized<CheckOwner>);
static_assert(!::foundation::transfer::Synchronized<SpscSplit<CheckOwner, CheckBrand>>);

// The held split.  A proof is an empty class that no code builds with no
// argument and that does not copy.  A held brand names one proof for each
// side, and the two proofs are two types.
class ProducerProof final {
    ProducerProof() noexcept = default;

public:
    ProducerProof(ProducerProof const&) = delete("a proof of a side lives for the call that made it");
    ~ProducerProof() = default;
};
class ConsumerProof final {
    ConsumerProof() noexcept = default;

public:
    ConsumerProof(ConsumerProof const&) = delete("a proof of a side lives for the call that made it");
    ~ConsumerProof() = default;
};
struct HeldBrand final {
    using producer_proof = ProducerProof;
    using consumer_proof = ConsumerProof;
};
struct OneProofBrand final {
    using producer_proof = ProducerProof;
    using consumer_proof = ProducerProof;
};
static_assert(SpscSideProof<ProducerProof> && !SpscSideProof<CheckBrand> && !SpscSideProof<int>);
static_assert(SpscHeldBrand<HeldBrand> && !SpscHeldBrand<CheckBrand> && !SpscHeldBrand<OneProofBrand>);

// A held split opens each side with the proof of that side, and with no key
// and no proof of the other side.  A split of a brand that is not held opens
// with no proof.
struct WritesNothing final {
    void operator()(ProducerEnd<CheckOwner>& /*end*/) const noexcept {}
};
using HeldSplit = SpscSplit<CheckOwner, HeldBrand>;
template <class Split, class Opener>
constexpr bool opens_producer =
    requires(Split& split, Opener& opener) { split.with_producer(opener, WritesNothing{}); };
static_assert(opens_producer<HeldSplit, ProducerProof const>);
static_assert(!opens_producer<HeldSplit, ConsumerProof const>);
static_assert(!opens_producer<HeldSplit, ProducerKey<HeldBrand>>);
static_assert(opens_producer<SpscSplit<CheckOwner, CheckBrand>, ProducerKey<CheckBrand>>);
static_assert(!opens_producer<SpscSplit<CheckOwner, CheckBrand>, SpscNoSideProof const>);

// The same refusal for the fresh brand of mint_spsc_split.  GCC 16 lists no
// member alias of a specialization whose argument is a fresh brand, so the
// statement of the side brand is a base (states_owner_brand of
// foundation/core/Frame.h), and this check reads the split of a real mint.
// No code calls the function: its checks hold where it stands.  The class has
// internal linkage, because its closure holds a reference to a split of a
// fresh brand, and a walk unit that includes this file would otherwise give
// a class of external linkage a member of internal linkage.
namespace {
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
}  // namespace

}  // namespace fixy::concurrent::detail::spsc_ends_checks
