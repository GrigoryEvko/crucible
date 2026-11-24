// The compile-time checks of fixy/concurrent/SpscEnds.h.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Owner.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace fixy::concurrent::detail::spsc_ends_checks {

// An owner with two empty states, an owner with a plain count, and the brands
// of two splits.
struct CheckOwner final {
    struct producer_state {};
    struct consumer_state {};
    CheckOwner() noexcept = default;
    CheckOwner(CheckOwner const&) = delete("an owner of a split is pinned");
    CheckOwner(CheckOwner&&) = delete("an owner of a split is pinned");
};
struct PlainOwner final {
    struct producer_state {};
    struct consumer_state {};
    PlainOwner() noexcept = default;
    PlainOwner(PlainOwner const&) = delete("an owner of a split is pinned");
    PlainOwner(PlainOwner&&) = delete("an owner of a split is pinned");
    std::uint64_t count = 0;
};
struct CheckBrand final {};
struct OtherBrand final {};

using CheckSplit = SpscSplit<CheckOwner, CheckBrand>;
using OtherSplit = SpscSplit<CheckOwner, OtherBrand>;

// A side token is linear, carries authority, does not copy, moves, and no code
// builds one with no argument.  The tokens of the two sides are two types, and
// the tokens of two brands are two types.
static_assert(::foundation::linear::is_linear_type(^^ProducerToken<CheckOwner, CheckBrand>));
static_assert(!std::is_copy_constructible_v<ProducerToken<CheckOwner, CheckBrand>>);
static_assert(std::is_nothrow_move_constructible_v<ProducerToken<CheckOwner, CheckBrand>>);
static_assert(!std::is_default_constructible_v<ProducerToken<CheckOwner, CheckBrand>>);
static_assert(!std::is_same_v<ProducerToken<CheckOwner, CheckBrand>, ConsumerToken<CheckOwner, CheckBrand>>);
static_assert(!std::is_same_v<ProducerToken<CheckOwner, CheckBrand>, ProducerToken<CheckOwner, OtherBrand>>);

// Two threads can write a split whose owner two threads write (the statement
// of the split), and no thread shares a split of an owner with a plain part.
static_assert(::foundation::transfer::Synchronized<CheckOwner>);
static_assert(::foundation::transfer::Synchronized<CheckSplit>);
static_assert(!::foundation::transfer::Synchronized<PlainOwner>);
static_assert(!::foundation::transfer::Synchronized<SpscSplit<PlainOwner, CheckBrand>>);

// The store mark of an end rests on three facts.  The end states the mark,
// it holds two parts only, the const Ref of its owner and its state, and a
// side state holds no borrow.  The members come as a constant array, so the
// loop reads no container.
template <std::meta::info Type>
inline constexpr std::meta::info data_members_of_ = std::meta::reflect_constant_array(
    std::meta::nonstatic_data_members_of(Type, std::meta::access_context::unchecked()));

template <std::meta::info End, std::meta::info Ref, std::meta::info State>
[[nodiscard]] consteval bool holds_const_ref_and_state() {
    std::size_t count = 0;
    bool is_expected = true;
    for (std::meta::info const member : [:data_members_of_<End>:]) {
        std::meta::info const type = std::meta::dealias(std::meta::type_of(member));
        if (count == 0) {
            is_expected = is_expected && std::meta::is_const_type(type)
                       && std::meta::dealias(std::meta::remove_const(type)) == Ref;
        } else {
            is_expected = is_expected && type == State;
        }
        ++count;
    }
    return is_expected && count == 2;
}

static_assert(::foundation::linear::carries_annotation(std::meta::dealias(^^ProducerEnd<PlainOwner>),
                                                       ^^::foundation::core::owns_its_storage));
static_assert(holds_const_ref_and_state<^^ProducerEnd<PlainOwner>,
                                        ^^::foundation::core::Ref<PlainOwner, SpscSplitBrand<PlainOwner>>,
                                        ^^PlainOwner::producer_state>());
static_assert(holds_const_ref_and_state<^^ConsumerEnd<PlainOwner>,
                                        ^^::foundation::core::Ref<PlainOwner, SpscSplitBrand<PlainOwner>>,
                                        ^^PlainOwner::consumer_state>());

// A state that holds a borrow is no side state, so an owner with such a
// state does not split.  The borrows name a brand of their own.
struct BorrowBrand final {};
struct BorrowState final {
    ::foundation::core::Option<::foundation::core::Ref<int, BorrowBrand>> held{};
};
struct OwnerOfABorrowState final {
    using producer_state = BorrowState;
    struct consumer_state {};
    OwnerOfABorrowState() noexcept = default;
    OwnerOfABorrowState(OwnerOfABorrowState const&) = delete("an owner of a split is pinned");
    OwnerOfABorrowState(OwnerOfABorrowState&&) = delete("an owner of a split is pinned");
};
static_assert(!SpscSideState<BorrowState> && !SpscSplitOwner<OwnerOfABorrowState>);

// So a body of an inner frame captures an end, or a split, by a reference
// that is not const.  The walk still reads the owner type, so an end of an
// owner that holds a borrow refuses the body.
struct BorrowOwner final {
    struct producer_state {};
    struct consumer_state {};
    BorrowOwner() noexcept = default;
    BorrowOwner(BorrowOwner const&) = delete("an owner of a split is pinned");
    BorrowOwner(BorrowOwner&&) = delete("an owner of a split is pinned");
    ::foundation::core::Option<::foundation::core::Ref<int, BorrowBrand>> held{};
};
template <class Captured>
struct CapturesByReference final {
    Captured& captured;
    void operator()() const noexcept {}
};
static_assert(::foundation::core::StoresNoBorrowOutside<CapturesByReference<ProducerEnd<PlainOwner>>>);
static_assert(::foundation::core::StoresNoBorrowOutside<CapturesByReference<SpscSplit<PlainOwner, CheckBrand>>>);
static_assert(!::foundation::core::StoresNoBorrowOutside<CapturesByReference<ProducerEnd<BorrowOwner>>>);

// The producer token of a split opens its producer side.  The consumer token
// and the token of another brand open nothing there.
struct WritesNothing final {
    void operator()(ProducerEnd<CheckOwner>& /*end*/) const noexcept {}
};
template <class Split, class Opener>
constexpr bool opens_producer =
    requires(Split& split, Opener& opener) { with_producer(split, opener, WritesNothing{}); };
static_assert(opens_producer<CheckSplit, ProducerToken<CheckOwner, CheckBrand>>);
static_assert(!opens_producer<CheckSplit, ConsumerToken<CheckOwner, CheckBrand>>);
static_assert(!opens_producer<CheckSplit, ProducerToken<CheckOwner, OtherBrand>>);
static_assert(!opens_producer<OtherSplit, ProducerToken<CheckOwner, CheckBrand>>);

// The gate of a frame door refuses a body that reaches the token of its side,
// and it admits a body that reaches the token of the other side.
struct ReachesItsToken {
    ProducerToken<CheckOwner, CheckBrand>& token;
    void operator()(ProducerEnd<CheckOwner>& /*end*/) noexcept {}
};
struct ReachesTheOtherToken {
    ConsumerToken<CheckOwner, CheckBrand>& token;
    void operator()(ProducerEnd<CheckOwner>& /*end*/) noexcept {}
};
static_assert(!ReachesNoSideToken<ReachesItsToken, ProducerToken<CheckOwner, CheckBrand>>);
static_assert(ReachesNoSideToken<ReachesTheOtherToken, ProducerToken<CheckOwner, CheckBrand>>);

// An owner gives the first state of each end with two static members, and
// the mint builds each end of each other owner with the state that its type
// gives.
struct StartOwner final {
    struct producer_state {
        std::uint64_t written = 0;
    };
    struct consumer_state {
        std::uint64_t read = 0;
    };
    StartOwner() noexcept = default;
    StartOwner(StartOwner const&) = delete("an owner of a split is pinned");
    StartOwner(StartOwner&&) = delete("an owner of a split is pinned");
    [[nodiscard]] static producer_state first_producer_state(StartOwner const& /*owner*/) noexcept { return {}; }
    [[nodiscard]] static consumer_state first_consumer_state(StartOwner const& /*owner*/) noexcept { return {}; }
};
static_assert(SpscGivesFirstStates<StartOwner> && !SpscGivesFirstStates<CheckOwner>);

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

// A side of a held split opens with the proof of its side, and with no other
// proof.  The type of a side names no brand.  A held split states no transfer
// fact, because its proofs belong to its holder.
using HeldSplit = SpscHeldSplit<CheckOwner, HeldBrand>;
using HeldProducerSide = HeldSplit::producer_side_type;
static_assert(std::is_same_v<HeldProducerSide, SpscHeldSide<CheckOwner, SpscSide::producer, ProducerProof>>);
template <class Side, class Opener>
constexpr bool opens_side = requires(Side& side, Opener& opener) { side.with_end(opener, WritesNothing{}); };
static_assert(opens_side<HeldProducerSide, ProducerProof const>);
static_assert(!opens_side<HeldProducerSide, ConsumerProof const>);
static_assert(!::foundation::transfer::Synchronized<HeldSplit>);

// The door of two held sides takes two sides of two types, such as the two
// sides of one held split, and refuses one side type given two times.
static_assert(SpscTwoSideTypes<HeldSplit::producer_side_type, HeldSplit::consumer_side_type>);
static_assert(!SpscTwoSideTypes<HeldProducerSide, HeldProducerSide>);

// The door of a side refuses a body that reaches a side of its proof through a
// borrow, also through the held split that holds it.  A body that reaches a
// held split of other proofs passes.
struct OtherProducerProof final {
    OtherProducerProof() = delete("a proof of another holder");
    OtherProducerProof(OtherProducerProof const&) = delete("a proof of another holder");
};
struct OtherConsumerProof final {
    OtherConsumerProof() = delete("a proof of another holder");
    OtherConsumerProof(OtherConsumerProof const&) = delete("a proof of another holder");
};
struct OtherHeldBrand final {
    using producer_proof = OtherProducerProof;
    using consumer_proof = OtherConsumerProof;
};
struct ReachesTheSide {
    HeldProducerSide& side;
};
struct ReachesTheSplit {
    HeldSplit& split;
};
struct ReachesAnotherSplit {
    SpscHeldSplit<CheckOwner, OtherHeldBrand>& split;
};
static_assert(SpscReachesHeldSide<ReachesTheSide, ProducerProof>);
static_assert(SpscReachesHeldSide<ReachesTheSplit, ProducerProof>);
static_assert(!SpscReachesHeldSide<ReachesAnotherSplit, ProducerProof>);

// The residual of a held side: a proof is not tied to one holder.  One proof
// of the producer side opens the producer side of two held splits of one
// brand, so the type does not refuse the proof of one holder at the side of a
// second holder.  The door of the side states this route.
template <class Proof>
constexpr bool opens_two_holders = requires(HeldSplit& first, HeldSplit& second, Proof const& proof) {
    first.producer_side().with_end(proof, WritesNothing{});
    second.producer_side().with_end(proof, WritesNothing{});
};
static_assert(opens_two_holders<ProducerProof>);

}  // namespace fixy::concurrent::detail::spsc_ends_checks
