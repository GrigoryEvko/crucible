#pragma once

// Session-type protocol DSL.  A protocol is a type built from Send,
// Recv, Select, Offer, Loop, Continue and End.  Two endpoints of one
// channel agree when their protocols are duals.
//
// Select is INTERNAL choice: this endpoint picks the branch and tells
// the peer which one.  Offer is EXTERNAL choice: the peer picks and
// this endpoint dispatches on the label it receives.  Duality swaps
// the two, so one side's Select always faces the other side's Offer,
// and one side's Send always faces the other side's Recv.
//
// Recursion is isorecursive, not equirecursive.  A handle is never
// positioned at a Loop.  The factory unrolls one iteration and
// positions the handle at the loop body, carrying the Loop itself as
// the LoopCtx.  Continue resolves against that context, so it binds to
// the nearest enclosing Loop, and a nested Loop shadows the outer one
// for the duration of its body.  Equirecursion would remove the
// unrolling step but requires type equality up to unfolding, which
// C++ template matching cannot express.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/EpochLattice.h>
#include <crucible/algebra/lattices/GenerationLattice.h>
#include <crucible/algebra/lattices/_VendorLattice.h>
#include <crucible/safety/Pinned.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <meta>
#include <optional>
#include <source_location>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

using ::crucible::algebra::lattices::VendorBackend;
using ::crucible::algebra::lattices::VendorLattice;
using ::crucible::algebra::lattices::Epoch;
using ::crucible::algebra::lattices::EpochLattice;
using ::crucible::algebra::lattices::Generation;
using ::crucible::algebra::lattices::GenerationLattice;

template <typename T, typename Rest>
struct Send {
    using message_type = T;
    using next = Rest;
};

template <typename T, typename Rest>
struct Recv {
    using message_type = T;
    using next = Rest;
};

// Internal choice: THIS endpoint picks one of Branches...
template <typename... Branches>
struct Select {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
};

// Names the role that signals a choice, as the FIRST template argument
// of an Offer:
//
//   Offer<Sender<Alice>, Recv<Msg, End>, Recv<Crash<Alice>, Recovery>>
//
// A two-party Offer needs no annotation because "the peer" is
// unambiguous.  A multiparty local protocol can hold several Offers
// whose signalling roles differ, and crash analysis must know which
// role signals an Offer to decide whether that Offer needs a crash
// branch for a given peer.  Without the annotation such analysis has
// to assume every peer can signal every Offer and over-rejects.
template <typename Role>
struct Sender {
    using role_type = Role;
};

// Sender of an unannotated Offer.  Not a user-facing role — it exists
// so trait specializations can tell annotated from unannotated Offers
// at the type level, and it matches any peer in crash analysis.
struct AnonymousPeer {};

// External choice: the PEER picks one of Branches...
template <typename... Branches>
struct Offer {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
    using sender = AnonymousPeer;
};

// `branch_count` and `branches_tuple` count the real branches only.
// The Sender tag is not a branch.
template <typename Role, typename... Branches>
struct Offer<Sender<Role>, Branches...> {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
    using sender = Role;
};

template <typename OfferType>
struct offer_sender {
    using type = typename OfferType::sender;
};

template <typename OfferType>
using offer_sender_t = typename offer_sender<OfferType>::type;

template <typename Body>
struct Loop {
    using body = Body;
};

struct Continue {};

struct End {};

// Declares the vendor an upper-layer session runs against.  The
// wrapper is transparent to every structural protocol operation but
// visible to mint and subtyping admission checks.
template <VendorBackend V, typename Proto>
struct VendorPinned : Proto {
    using protocol = Proto;
    static constexpr VendorBackend vendor_backend = V;
};

// Carries a compile-time claim about the peer's current epoch and
// generation alongside the LoopCtx.  It holds admission facts for
// combinator construction only.  The runtime epoch and generation
// values live elsewhere.
template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, typename InnerLoopCtx = void>
struct EpochCtx {
    using inner_loop_ctx = InnerLoopCtx;
    static constexpr std::uint64_t current_epoch = CurrentEpoch;
    static constexpr std::uint64_t current_generation = CurrentGeneration;
};

template <typename LoopCtx>
struct session_loop_ctx_traits {
    using inner_loop_ctx = LoopCtx;
    static constexpr bool explicit_epoch = false;
    static constexpr std::uint64_t current_epoch = 0;
    static constexpr std::uint64_t current_generation = 0;
};

template <>
struct session_loop_ctx_traits<void> {
    using inner_loop_ctx = void;
    static constexpr bool explicit_epoch = false;
    static constexpr std::uint64_t current_epoch = 0;
    static constexpr std::uint64_t current_generation = 0;
};

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, typename InnerLoopCtx>
struct session_loop_ctx_traits<EpochCtx<CurrentEpoch, CurrentGeneration, InnerLoopCtx>> {
    using inner_loop_ctx = typename session_loop_ctx_traits<InnerLoopCtx>::inner_loop_ctx;
    static constexpr bool explicit_epoch = true;
    static constexpr std::uint64_t current_epoch = CurrentEpoch;
    static constexpr std::uint64_t current_generation = CurrentGeneration;
};

template <typename LoopCtx>
using session_loop_ctx_inner_t = typename session_loop_ctx_traits<LoopCtx>::inner_loop_ctx;

template <typename LoopCtx>
inline constexpr bool session_loop_ctx_has_explicit_epoch_v = session_loop_ctx_traits<LoopCtx>::explicit_epoch;

template <typename LoopCtx>
inline constexpr std::uint64_t session_loop_ctx_epoch_v = session_loop_ctx_traits<LoopCtx>::current_epoch;

template <typename LoopCtx>
inline constexpr std::uint64_t session_loop_ctx_generation_v = session_loop_ctx_traits<LoopCtx>::current_generation;

template <typename LoopCtx, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
inline constexpr bool session_epoch_threshold_valid_v =
    EpochLattice::leq(Epoch{0}, Epoch{MinEpoch}) && GenerationLattice::leq(Generation{0}, Generation{MinGeneration});

template <typename LoopCtx, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
inline constexpr bool session_loop_ctx_epoch_satisfies_v =
    session_epoch_threshold_valid_v<LoopCtx, MinEpoch, MinGeneration> && session_loop_ctx_has_explicit_epoch_v<LoopCtx>
    && EpochLattice::leq(Epoch{MinEpoch}, Epoch{session_loop_ctx_epoch_v<LoopCtx>})
    && GenerationLattice::leq(Generation{MinGeneration}, Generation{session_loop_ctx_generation_v<LoopCtx>});

template <typename LoopCtx, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
inline constexpr bool session_loop_ctx_epoch_matches_v =
    session_loop_ctx_epoch_satisfies_v<LoopCtx, MinEpoch, MinGeneration>
    && EpochLattice::leq(Epoch{session_loop_ctx_epoch_v<LoopCtx>}, Epoch{MinEpoch})
    && GenerationLattice::leq(Generation{session_loop_ctx_generation_v<LoopCtx>}, Generation{MinGeneration});

template <typename LoopCtx, typename NewInnerLoopCtx>
struct session_loop_ctx_rebind_inner {
    using type = NewInnerLoopCtx;
};

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, typename InnerLoopCtx, typename NewInnerLoopCtx>
struct session_loop_ctx_rebind_inner<EpochCtx<CurrentEpoch, CurrentGeneration, InnerLoopCtx>, NewInnerLoopCtx> {
    using type = EpochCtx<CurrentEpoch, CurrentGeneration,
                          typename session_loop_ctx_rebind_inner<InnerLoopCtx, NewInnerLoopCtx>::type>;
};

template <typename LoopCtx, typename NewInnerLoopCtx>
using session_loop_ctx_rebind_inner_t = typename session_loop_ctx_rebind_inner<LoopCtx, NewInnerLoopCtx>::type;

template <typename P>
struct is_send : std::false_type {};
template <typename T, typename R>
struct is_send<Send<T, R>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_send<VendorPinned<V, P>> : is_send<P> {};

template <typename P>
struct is_recv : std::false_type {};
template <typename T, typename R>
struct is_recv<Recv<T, R>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_recv<VendorPinned<V, P>> : is_recv<P> {};

template <typename P>
struct is_select : std::false_type {};
template <typename... Bs>
struct is_select<Select<Bs...>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_select<VendorPinned<V, P>> : is_select<P> {};

template <typename P>
struct is_offer : std::false_type {};
template <typename... Bs>
struct is_offer<Offer<Bs...>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_offer<VendorPinned<V, P>> : is_offer<P> {};

template <typename P>
struct is_loop : std::false_type {};
template <typename B>
struct is_loop<Loop<B>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_loop<VendorPinned<V, P>> : is_loop<P> {};

template <typename P>
struct is_end : std::bool_constant<std::is_same_v<P, End>> {};
template <VendorBackend V, typename P>
struct is_end<VendorPinned<V, P>> : is_end<P> {};

template <typename P>
struct is_continue : std::bool_constant<std::is_same_v<P, Continue>> {};
template <VendorBackend V, typename P>
struct is_continue<VendorPinned<V, P>> : is_continue<P> {};

template <typename P>
struct is_vendor_pinned : std::false_type {
    using protocol = P;
    static constexpr VendorBackend vendor_backend = VendorBackend::Portable;
};
template <VendorBackend V, typename P>
struct is_vendor_pinned<VendorPinned<V, P>> : std::true_type {
    using protocol = P;
    static constexpr VendorBackend vendor_backend = V;
};

template <typename P>
inline constexpr bool is_send_v = is_send<P>::value;
template <typename P>
inline constexpr bool is_recv_v = is_recv<P>::value;
template <typename P>
inline constexpr bool is_select_v = is_select<P>::value;
template <typename P>
inline constexpr bool is_offer_v = is_offer<P>::value;
template <typename P>
inline constexpr bool is_loop_v = is_loop<P>::value;
template <typename P>
inline constexpr bool is_end_v = is_end<P>::value;
template <typename P>
inline constexpr bool is_continue_v = is_continue<P>::value;
template <typename P>
inline constexpr bool is_vendor_pinned_v = is_vendor_pinned<P>::value;
template <typename P>
inline constexpr VendorBackend protocol_vendor_v = is_vendor_pinned<P>::vendor_backend;
template <typename P>
using protocol_inner_t = typename is_vendor_pinned<P>::protocol;

// A Select or Offer with zero branches is a legitimate TYPE operand:
// under branch covariance an empty Select is a subtype of every larger
// Select, so subtyping reasoning admits it as the minimum element.  It
// is not a runnable protocol.  An empty Select has no branch to pick,
// and an empty Offer has no label the peer can send, so a handle
// positioned there is stuck.  The trait therefore exists so that
// handle construction can reject empty choices while subtyping keeps
// accepting them.
//
// The walk is recursive because a handle reaches every position in the
// tree eventually.  Rejecting only a top-level empty choice would let
// the misuse surface at the eventual dead-end operation instead of at
// construction, after construction already promised the whole protocol
// was certified.
//
// Sibling headers add specializations for the combinators they define,
// adjacent to those definitions.

template <typename P>
struct is_empty_choice : std::false_type {};

template <>
struct is_empty_choice<Select<>> : std::true_type {};
template <>
struct is_empty_choice<Offer<>> : std::true_type {};

template <typename Role>
struct is_empty_choice<Offer<Sender<Role>>> : std::true_type {};

template <typename T, typename K>
struct is_empty_choice<Send<T, K>> : is_empty_choice<K> {};

template <typename T, typename K>
struct is_empty_choice<Recv<T, K>> : is_empty_choice<K> {};

template <typename Body>
struct is_empty_choice<Loop<Body>> : is_empty_choice<Body> {};

// The zero-branch cases are covered by the explicit specializations
// above, so these partial specializations match only when there is at
// least one branch.
template <typename First, typename... Rest>
struct is_empty_choice<Select<First, Rest...>>
    : std::bool_constant<(is_empty_choice<First>::value || ... || is_empty_choice<Rest>::value)> {};

template <typename First, typename... Rest>
struct is_empty_choice<Offer<First, Rest...>>
    : std::bool_constant<(is_empty_choice<First>::value || ... || is_empty_choice<Rest>::value)> {};

template <typename Role, typename First, typename... Rest>
struct is_empty_choice<Offer<Sender<Role>, First, Rest...>>
    : std::bool_constant<(is_empty_choice<First>::value || ... || is_empty_choice<Rest>::value)> {};

template <VendorBackend V, typename P>
struct is_empty_choice<VendorPinned<V, P>> : is_empty_choice<P> {};

template <typename P>
inline constexpr bool is_empty_choice_v = is_empty_choice<P>::value;

// A protocol head is any position a handle can occupy.  The test is
// negative rather than an enumeration so that a combinator added by a
// sibling header is admitted without editing this file.  Loop is the
// sole non-head because it has no SessionHandle specialization: the
// factory unrolls it and positions the handle at the body.
template <typename P>
inline constexpr bool is_head_v = !is_loop_v<P>;

template <typename P>
struct dual_of;

template <>
struct dual_of<End> {
    using type = End;
};
template <>
struct dual_of<Continue> {
    using type = Continue;
};

template <typename T, typename R>
struct dual_of<Send<T, R>> {
    using type = Recv<T, typename dual_of<R>::type>;
};

template <typename T, typename R>
struct dual_of<Recv<T, R>> {
    using type = Send<T, typename dual_of<R>::type>;
};

template <typename... Bs>
struct dual_of<Select<Bs...>> {
    using type = Offer<typename dual_of<Bs>::type...>;
};

template <typename... Bs>
struct dual_of<Offer<Bs...>> {
    using type = Select<typename dual_of<Bs>::type...>;
};

// The dual drops the sender tag and yields a plain Select.  From the
// dual endpoint's view the sender of the original Offer is the local
// role, so the annotation carries no meaning on an internal choice.
// The round trip therefore cannot restore the tag and duality is not
// involutive on this shape.  Select has no mirror annotation naming
// its recipient, which is what a symmetric encoding would need.
template <typename Role, typename... Bs>
struct dual_of<Offer<Sender<Role>, Bs...>> {
    using type = Select<typename dual_of<Bs>::type...>;
};

template <typename B>
struct dual_of<Loop<B>> {
    using type = Loop<typename dual_of<B>::type>;
};

template <VendorBackend V, typename P>
struct dual_of<VendorPinned<V, P>> {
    using type = VendorPinned<V, typename dual_of<P>::type>;
};

template <typename P>
using dual_of_t = typename dual_of<P>::type;

// True when P satisfies dual(dual(P)) == P.  Generic code that needs
// the involution as a precondition, such as a type-level rewrite that
// must commute with dual_of, gates on this trait rather than on
// well-formedness.  The trait is deliberately conservative and reports
// false for a whole protocol when any subterm is non-involutive: a
// generic transformation cannot apply to only part of a tree.

template <typename P>
struct is_dual_involutive : std::true_type {};

template <typename T, typename R>
struct is_dual_involutive<Send<T, R>> : is_dual_involutive<R> {};

template <typename T, typename R>
struct is_dual_involutive<Recv<T, R>> : is_dual_involutive<R> {};

template <typename... Bs>
struct is_dual_involutive<Select<Bs...>> : std::bool_constant<(is_dual_involutive<Bs>::value && ...)> {};

template <typename... Bs>
struct is_dual_involutive<Offer<Bs...>> : std::bool_constant<(is_dual_involutive<Bs>::value && ...)> {};

template <typename Role, typename... Bs>
struct is_dual_involutive<Offer<Sender<Role>, Bs...>> : std::false_type {};

template <typename B>
struct is_dual_involutive<Loop<B>> : is_dual_involutive<B> {};

template <VendorBackend V, typename P>
struct is_dual_involutive<VendorPinned<V, P>> : is_dual_involutive<P> {};

template <typename P>
inline constexpr bool is_dual_involutive_v = is_dual_involutive<P>::value;

// Two endpoints of one channel must be duals, otherwise the
// deadlock-freedom guarantee does not hold.
//
// The disjunction is load-bearing.  A channel pair has no primary
// side, so the test must be insensitive to argument order, and the
// simpler `dual_of_t<P1> == P2` is not.  On a sender-annotated Offer
// the forward direction strips the tag while the reverse direction
// cannot restore it:
//
//   using OfferS = Offer<Sender<Role>, B1, B2>;
//   using SelP   = Select<dual_of_t<B1>, dual_of_t<B2>>;
//   dual_of_t<OfferS> == SelP            // holds
//   dual_of_t<SelP>   == Offer<B1, B2>   // not OfferS
//
// so the two orientations disagree on exactly that shape.  Everywhere
// else duality is involutive and either operand alone suffices.

template <typename P1, typename P2>
inline constexpr bool is_dual_v = std::is_same_v<dual_of_t<P1>, P2> || std::is_same_v<P1, dual_of_t<P2>>;

template <typename P1, typename P2>
consteval void ensure_dual() noexcept {
    static_assert(is_dual_v<P1, P2>, "crucible::session::diagnostic [Dual_Mismatch]: "
                                     "ensure_dual<P1, P2>(): the two endpoint protocols are NOT "
                                     "structural duals.  One side's Send must pair with the other "
                                     "side's Recv; one side's Select must pair with the other "
                                     "side's Offer; Loop pairs with Loop, End with End, Continue "
                                     "with Continue.  Inspect dual_of_t<P1> against P2 (they must "
                                     "be the same type).  Without this duality the framework's "
                                     "deadlock-freedom guarantee does NOT hold — runtime hangs and "
                                     "transport-byte misinterpretation become possible.  Common "
                                     "causes: (a) one side missing a Send/Recv step the other side "
                                     "has, (b) Select/Offer mismatch (one endpoint internal-choice, "
                                     "other endpoint should be external-choice), (c) branch types "
                                     "of a Select/Offer pair don't dualize element-wise.");
}

// Sequential composition replaces every End in P with Q.  Continue
// survives unchanged because it marks a loop-back, not a protocol end,
// and it resolves against the LoopCtx while stepping rather than here.

template <typename P, typename Q>
struct compose;

template <typename Q>
struct compose<End, Q> {
    using type = Q;
};

template <typename Q>
struct compose<Continue, Q> {
    using type = Continue;
};

template <typename T, typename R, typename Q>
struct compose<Send<T, R>, Q> {
    using type = Send<T, typename compose<R, Q>::type>;
};

template <typename T, typename R, typename Q>
struct compose<Recv<T, R>, Q> {
    using type = Recv<T, typename compose<R, Q>::type>;
};

template <typename... Bs, typename Q>
struct compose<Select<Bs...>, Q> {
    using type = Select<typename compose<Bs, Q>::type...>;
};

template <typename... Bs, typename Q>
struct compose<Offer<Bs...>, Q> {
    using type = Offer<typename compose<Bs, Q>::type...>;
};

// Without this specialization the unannotated form would treat the
// Sender tag as a branch and compose Q into it.
template <typename Role, typename... Bs, typename Q>
struct compose<Offer<Sender<Role>, Bs...>, Q> {
    using type = Offer<Sender<Role>, typename compose<Bs, Q>::type...>;
};

template <typename B, typename Q>
struct compose<Loop<B>, Q> {
    using type = Loop<typename compose<B, Q>::type>;
};

template <VendorBackend V, typename P, typename Q>
struct compose<VendorPinned<V, P>, Q> {
    using type = VendorPinned<V, typename compose<P, Q>::type>;
};

template <typename P, typename Q>
using compose_t = typename compose<P, Q>::type;

// Branch-asymmetric composition.  It walks P's spine to the first
// Select or Offer and composes Q into branch I alone, leaving the
// other branches untouched.  Send, Recv and Loop nodes above the
// choice are passed through.  Uniform composition, which appends Q to
// every branch, is what compose_t already does.

namespace detail {
template <std::size_t BranchIndex, typename Q, typename... Bs>
struct rewrite_branches {
    template <std::size_t I>
    using at = std::conditional_t<I == BranchIndex, compose_t<std::tuple_element_t<I, std::tuple<Bs...>>, Q>,
                                  std::tuple_element_t<I, std::tuple<Bs...>>>;

    template <typename Idxs>
    struct unpack;

    template <std::size_t... Is>
    struct unpack<std::index_sequence<Is...>> {
        using as_select = Select<at<Is>...>;
        using as_offer = Offer<at<Is>...>;
    };
};
}  // namespace detail

template <typename P, std::size_t I, typename Q>
struct compose_at_branch;

template <typename... Bs, std::size_t I, typename Q>
struct compose_at_branch<Select<Bs...>, I, Q> {
    static_assert(I < sizeof...(Bs), "crucible::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
                                     "compose_at_branch_t<Select<Bs...>, I, Q>: branch index I is "
                                     "out of range for the reached Select.  The Select has fewer "
                                     "branches than the index requested; verify I < sizeof...(Bs) "
                                     "at the call site (decltype-expose `branch_count` if needed).");
    using type =
        typename detail::rewrite_branches<I, Q,
                                          Bs...>::template unpack<std::make_index_sequence<sizeof...(Bs)>>::as_select;
};

template <typename... Bs, std::size_t I, typename Q>
struct compose_at_branch<Offer<Bs...>, I, Q> {
    static_assert(I < sizeof...(Bs), "crucible::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
                                     "compose_at_branch_t<Offer<Bs...>, I, Q>: branch index I is "
                                     "out of range for the reached Offer.  The Offer has fewer "
                                     "branches than the index requested.");
    using type =
        typename detail::rewrite_branches<I, Q,
                                          Bs...>::template unpack<std::make_index_sequence<sizeof...(Bs)>>::as_offer;
};

// I ranges over the real branches, so the rewrite runs on Bs... alone
// and the sender tag is put back at position 0.
template <typename Role, typename... Bs, std::size_t I, typename Q>
struct compose_at_branch<Offer<Sender<Role>, Bs...>, I, Q> {
    static_assert(I < sizeof...(Bs), "crucible::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
                                     "compose_at_branch_t<Offer<Sender<Role>, Bs...>, I, Q>: branch "
                                     "index I is out of range for the reached sender-annotated "
                                     "Offer.  Branches are counted WITHOUT the Sender<Role> tag — "
                                     "an Offer<Sender<R>, B0, B1> has 2 branches, not 3.");

    template <std::size_t J>
    using at = std::conditional_t<J == I, compose_t<std::tuple_element_t<J, std::tuple<Bs...>>, Q>,
                                  std::tuple_element_t<J, std::tuple<Bs...>>>;

    template <typename Idxs>
    struct build;
    template <std::size_t... Js>
    struct build<std::index_sequence<Js...>> {
        using type = Offer<Sender<Role>, at<Js>...>;
    };

    using type = typename build<std::make_index_sequence<sizeof...(Bs)>>::type;
};

template <typename T, typename R, std::size_t I, typename Q>
struct compose_at_branch<Send<T, R>, I, Q> {
    using type = Send<T, typename compose_at_branch<R, I, Q>::type>;
};

template <typename T, typename R, std::size_t I, typename Q>
struct compose_at_branch<Recv<T, R>, I, Q> {
    using type = Recv<T, typename compose_at_branch<R, I, Q>::type>;
};

template <typename B, std::size_t I, typename Q>
struct compose_at_branch<Loop<B>, I, Q> {
    using type = Loop<typename compose_at_branch<B, I, Q>::type>;
};

template <VendorBackend V, typename P, std::size_t I, typename Q>
struct compose_at_branch<VendorPinned<V, P>, I, Q> {
    using type = VendorPinned<V, typename compose_at_branch<P, I, Q>::type>;
};

// Reaching a non-choice terminal means the spine holds no choice to
// compose at.
template <std::size_t I, typename Q>
struct compose_at_branch<End, I, Q> {
    static_assert(sizeof(Q) == 0, "crucible::session::diagnostic [Branch_Compose_No_Choice]: "
                                  "compose_at_branch_t<P, I, Q>: walked P's spine to End without "
                                  "encountering a Select<Bs...> or Offer<Bs...> to compose at.  "
                                  "Branch-asymmetric composition requires P to contain a choice "
                                  "combinator at some position reachable from the head via "
                                  "Send/Recv/Loop pass-through.  If you intended UNIFORM "
                                  "composition (every End → Q), use the existing compose_t<P, Q> "
                                  "instead.");
};

template <std::size_t I, typename Q>
struct compose_at_branch<Continue, I, Q> {
    static_assert(sizeof(Q) == 0, "crucible::session::diagnostic [Branch_Compose_No_Choice]: "
                                  "compose_at_branch_t<P, I, Q>: walked P's spine to Continue "
                                  "without encountering a Select or Offer.  Continue is a loop-"
                                  "back marker, not a choice point; compose_at_branch is meant "
                                  "for choice combinators.");
};

template <typename P, std::size_t I, typename Q>
using compose_at_branch_t = typename compose_at_branch<P, I, Q>::type;

// A protocol is well-formed when every Continue has an enclosing Loop
// and no Loop body is itself a terminal state.  A Continue with no
// Loop above it is stuck, since there is no state to loop back to.  A
// Loop over a terminal body can never reach Continue, so it is the
// terminal state wearing a loop's shape.

// The definition of is_terminal_state follows is_well_formed, but the
// Loop body check queries it, so it is declared here.  A sibling
// header adds terminal combinators of its own.  A translation unit
// that wants those rejected as loop bodies has to see that header, or
// the primary template answers and the loop is admitted.
template <typename P>
struct is_terminal_state;

template <typename P, typename LoopCtx = void>
struct is_well_formed;

template <typename LoopCtx>
struct is_well_formed<End, LoopCtx> : std::true_type {};

template <typename LoopCtx>
struct is_well_formed<Continue, LoopCtx> : std::bool_constant<!std::is_void_v<session_loop_ctx_inner_t<LoopCtx>>> {};

template <typename T, typename R, typename LoopCtx>
struct is_well_formed<Send<T, R>, LoopCtx> : is_well_formed<R, LoopCtx> {};

template <typename T, typename R, typename LoopCtx>
struct is_well_formed<Recv<T, R>, LoopCtx> : is_well_formed<R, LoopCtx> {};

template <typename... Bs, typename LoopCtx>
struct is_well_formed<Select<Bs...>, LoopCtx> : std::bool_constant<(is_well_formed<Bs, LoopCtx>::value && ...)> {};

template <typename... Bs, typename LoopCtx>
struct is_well_formed<Offer<Bs...>, LoopCtx> : std::bool_constant<(is_well_formed<Bs, LoopCtx>::value && ...)> {};

// The Sender tag is an annotation, not a runnable combinator, so only
// the real branches are checked.
template <typename Role, typename... Bs, typename LoopCtx>
struct is_well_formed<Offer<Sender<Role>, Bs...>, LoopCtx>
    : std::bool_constant<(is_well_formed<Bs, LoopCtx>::value && ...)> {};

template <typename B, typename LoopCtx>
struct is_well_formed<Loop<B>, LoopCtx>
    // Loop<B> becomes the new inner LoopCtx while B is checked, and
    // any outer context-axis wrapper is preserved around it.
    //
    // A terminal state is rejected as the WHOLE body but stays legal
    // as a branch inside the body, because Loop<Select<Send<int,
    // Continue>, End>> can still reach Continue.
    : std::bool_constant<!is_terminal_state<B>::value
                         && is_well_formed<B, session_loop_ctx_rebind_inner_t<LoopCtx, Loop<B>>>::value> {};

template <VendorBackend V, typename P, typename LoopCtx>
struct is_well_formed<VendorPinned<V, P>, LoopCtx> : is_well_formed<P, LoopCtx> {};

template <typename P>
inline constexpr bool is_well_formed_v = is_well_formed<P>::value;

// Marks the protocol positions at which a handle may be destroyed
// without being consumed.  A header that adds a terminal combinator
// specializes this next to that combinator's definition.

template <typename P>
struct is_terminal_state : std::bool_constant<std::is_same_v<P, End>> {};

template <VendorBackend V, typename P>
struct is_terminal_state<VendorPinned<V, P>> : is_terminal_state<P> {};

template <typename P>
inline constexpr bool is_terminal_state_v = is_terminal_state<P>::value;

// Holds the abandonment-check flag in debug and nothing at all in
// release.  The release shape is an empty class so that
// SessionHandleBase's [[no_unique_address]] member leaves the base
// empty and the derived handle costs no more than its Resource.
//
// The two shapes give SessionHandle a different sizeof per build mode.
// That is safe only because a binary is built in one mode throughout.
// Linking debug and release objects together produces silent layout
// mismatches.

namespace detail {

#ifdef NDEBUG

class consumed_tracker {
public:
    constexpr consumed_tracker() noexcept = default;
    // The destructor check does not run in release, so the location is
    // dropped.  The signature stays for parity with the debug shape,
    // letting derived constructors pass a location unconditionally.
    constexpr explicit consumed_tracker(std::source_location) noexcept {}
    constexpr void mark() noexcept {}
    constexpr bool was_marked() const noexcept { return true; }
    constexpr void move_from(consumed_tracker&) noexcept {}
    constexpr std::source_location construction_loc() const noexcept { return std::source_location{}; }
};
static_assert(std::is_empty_v<consumed_tracker>, "Release-mode consumed_tracker must be std::is_empty_v for EBO.");

#else

class consumed_tracker {
    bool flag_ = false;
    // The handle's construction site.  Default-constructs to an
    // unknown source for a handle minted without an explicit location.
    std::source_location loc_{};

public:
    constexpr consumed_tracker() noexcept = default;
    constexpr explicit consumed_tracker(std::source_location loc) noexcept : loc_{loc} {}
    constexpr void mark() noexcept { flag_ = true; }
    constexpr bool was_marked() const noexcept { return flag_; }
    constexpr std::source_location construction_loc() const noexcept { return loc_; }

    // Self-move must leave the tracker untouched.  Without the guard,
    // `h = std::move(h)` would copy flag_ onto itself and then set it
    // true, marking a live handle consumed and disabling the
    // destructor's abandonment check for it.
    constexpr void move_from(consumed_tracker& other) noexcept {
        if (this == &other) [[unlikely]]
            return;
        flag_ = other.flag_;
        loc_ = other.loc_;
        other.flag_ = true;
    }
};

#endif

// Renders T by parsing __PRETTY_FUNCTION__.  This depends on GCC's
// `[with T = ...; std::string_view = ...]` spelling of the with-clause
// and is not portable.  The returned view points into the function
// template's constant data and lives as long as the program.

template <typename T>
constexpr std::string_view pretty_function_raw_() noexcept {
    return std::string_view{__PRETTY_FUNCTION__};
}

template <typename T>
constexpr std::string_view type_name() noexcept {
    constexpr std::string_view raw = pretty_function_raw_<T>();
    constexpr std::string_view marker = "T = ";

    constexpr auto pos = raw.find(marker);
    if (pos == std::string_view::npos) [[unlikely]] {
        // Returning the whole string keeps some information in the
        // diagnostic when the with-clause spelling does not match.
        return raw;
    }
    constexpr auto start = pos + marker.size();

    // The rendering ends at the first ';', which separates with-clause
    // entries, or at the closing ']' when the clause has one entry.
    constexpr auto semi = raw.find(';', start);
    constexpr auto bracket = raw.find(']', start);
    constexpr auto end =
        (semi != std::string_view::npos && (bracket == std::string_view::npos || semi < bracket)) ? semi : bracket;

    if (end == std::string_view::npos) [[unlikely]]
        return raw.substr(start);
    return raw.substr(start, end - start);
}

// Names the wrapper class alone, without its template arguments.
// Several distinct wrappers share one SessionHandleBase specialization
// for a given Proto, so a diagnostic that prints only Proto cannot say
// which wrapper produced it.
//
// `Derived` may be void, which is the SessionHandleBase default, so
// callers guard the call.
template <typename Derived>
consteval std::string_view wrapper_class_name() noexcept {
    constexpr auto info = ^^Derived;
    if constexpr (std::meta::has_template_arguments(info)) {
        return std::meta::identifier_of(std::meta::template_of(info));
    } else {
        return std::meta::identifier_of(info);
    }
}

// Names the consumer method that advances the current protocol head,
// for the destructor's abandonment message.  Combinators defined in
// sibling headers are covered by the fall-through arm rather than by
// their own arm, because specializing per combinator from those
// headers would make the include graph circular.
template <typename Proto>
consteval std::string_view next_method_hint() noexcept {
    if constexpr (is_send_v<Proto>) {
        return "send(value, transport_callable)";
    } else if constexpr (is_recv_v<Proto>) {
        return "recv() or recv(callback)";
    } else if constexpr (is_select_v<Proto>) {
        return "pick<branch_index>(transport_callable)";
    } else if constexpr (is_offer_v<Proto>) {
        return "branch(visitor) or pick<branch_index>(transport_callable)";
    } else if constexpr (is_loop_v<Proto>) {
        // Unreachable: a Loop is unrolled before a handle is built.
        return "the loop body's appropriate consumer method "
               "(framework should have unrolled Loop<...>)";
    } else {
        return "the protocol head's appropriate consumer method "
               "(close / send / recv / pick / branch / delegate / accept / "
               "base / rollback)";
    }
}

}  // namespace detail

// Each tag names one sanctioned reason for marking a handle consumed
// without advancing its protocol.  The tag makes each class of
// abandonment separately greppable, which a bare `.detach()` would
// not.  Detection is by inheritance, so a caller adds its own tag by
// deriving from tag_base and the concept admits it.

namespace detach_reason {

struct tag_base {};

// The protocol loops forever with no close branch.  Termination is
// implicit: the transport closes the channel underneath the session
// abstraction.
struct InfiniteLoopProtocol : tag_base {};

// The transport observed the peer's session disappear out of band, so
// no further protocol step can complete.
struct TransportClosedOutOfBand : tag_base {};

// A test walked a partial protocol path and abandoned it deliberately.
struct TestInstrumentation : tag_base {};

// The local side cancelled mid-protocol, for example on a stop token.
// The peer may still be alive, which is what separates this from
// TransportClosedOutOfBand.
struct AsyncCancellation : tag_base {};

// The object that bounded the handle's lifetime is being destroyed.
// This is a last resort and signals that the handle's lifetime did not
// match the protocol's termination point.
struct OwnerLifetimeBoundEarlyExit : tag_base {};

}  // namespace detach_reason

template <typename T>
concept DetachReason = std::is_base_of_v<detach_reason::tag_base, T> && !std::is_same_v<T, detach_reason::tag_base>;

// Carries the lifetime contract for every handle specialization: a
// handle destroyed at a non-terminal protocol state without having
// been consumed is a dropped protocol, and in debug that aborts.
// Moving marks the SOURCE consumed, so a moved-from handle does not
// fire the check.
//
// Inheritance is public so that `.detach()` is callable on a derived
// handle without a per-class using-declaration.
//
// `Derived` names the wrapper class that inherits this base, so the
// abandonment diagnostic can say which wrapper aborted when several
// wrappers share one Proto.  It defaults to void, and a wrapper that
// leaves it void gets a less precise diagnostic.
template <typename Proto, typename Derived = void>
class SessionHandleBase {
    [[no_unique_address]] detail::consumed_tracker tracker_;

protected:
    // Call before returning from a consumer method.  After it, the
    // destructor check sees the tracker marked and skips the abort.
    constexpr void mark_consumed_() noexcept { tracker_.mark(); }

    constexpr bool is_consumed_() const noexcept { return tracker_.was_marked(); }

    // A sibling handle instantiation can mark another handle consumed,
    // which is how a handle shipped to a peer is retired.
    template <typename OtherProto, typename OtherDerived>
    friend class SessionHandleBase;
    template <typename P, typename R, typename L>
    friend class SessionHandle;

public:
    constexpr SessionHandleBase() noexcept = default;

    // Derived constructors default their own location parameter to
    // std::source_location::current(), so the site captured here is
    // the caller's, not the framework's.
    constexpr explicit SessionHandleBase(std::source_location loc) noexcept : tracker_{loc} {}

    // Returns a view into the program's constant data, safe to store
    // for the program's lifetime.
    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept { return detail::type_name<Proto>(); }

    // A wrapper that did not pass itself as Derived is reported as
    // "SessionHandle", so the destructor diagnostic stays stable.
    [[nodiscard]] static constexpr std::string_view wrapper_name() noexcept {
        if constexpr (!std::is_void_v<Derived>) {
            return detail::wrapper_class_name<Derived>();
        } else {
            return "SessionHandle";
        }
    }

    [[nodiscard]] static constexpr std::string_view next_method_hint() noexcept {
        return detail::next_method_hint<Proto>();
    }

    [[nodiscard]] static constexpr std::string_view full_handle_type_name() noexcept {
        if constexpr (!std::is_void_v<Derived>) {
            return detail::type_name<Derived>();
        } else {
            return std::string_view{};
        }
    }

    // Marks the handle consumed without advancing the protocol.  The
    // destructor check still fires for every handle that does not
    // detach, so accidental abandonment stays caught.
    template <typename Reason>
        requires DetachReason<Reason>
    constexpr void detach(Reason /*reason_tag*/) && noexcept {
        tracker_.mark();
    }

    // The deleted overload outranks the templated one for a zero-arg
    // call, so the diagnostic is this string rather than a
    // compiler-version-specific "no matching function" message.
    void detach() && = delete("[DetachReason_Required] SessionHandle::detach() requires a typed "
                              "reason tag from detach_reason::*.  Pass one of "
                              "detach_reason::InfiniteLoopProtocol{} (Loop<X> with no close "
                              "branch), TransportClosedOutOfBand{} (peer crash), "
                              "TestInstrumentation{} (test code only), AsyncCancellation{} "
                              "(jthread stop_token), or OwnerLifetimeBoundEarlyExit{} "
                              "(bridge/wrapper destructor).  The tag names the audit class so "
                              "each class of abandonment stays separately greppable.");

    SessionHandleBase(const SessionHandleBase&) =
        delete("SessionHandle is linear — protocol progress is consumed, not copied.");
    SessionHandleBase& operator=(const SessionHandleBase&) =
        delete("SessionHandle is linear — protocol progress is consumed, not copied.");

    // The move marks the source consumed so its destructor check skips
    // the abort, and the moved-into handle inherits the source's state.
    //
    // Only move-assignment can self-alias, since the language forbids
    // naming `h` inside its own initializer.  The standard leaves a
    // self-moved object valid but unspecified.  This contract is
    // stronger: self-move must leave the consumed state untouched, or
    // the abandonment check stops firing for a handle that really was
    // leaked.  The guard is duplicated in consumed_tracker::move_from
    // so the invariant survives a caller reaching the tracker directly.
    constexpr SessionHandleBase(SessionHandleBase&& other) noexcept { tracker_.move_from(other.tracker_); }

    constexpr SessionHandleBase& operator=(SessionHandleBase&& other) noexcept {
        if (this == &other) [[unlikely]]
            return *this;
        tracker_.move_from(other.tracker_);
        return *this;
    }

    ~SessionHandleBase() {
#ifndef NDEBUG
        if (!tracker_.was_marked() && !is_terminal_state_v<Proto>) {
            constexpr auto pname = detail::type_name<Proto>();
            constexpr auto hint = detail::next_method_hint<Proto>();
            const auto loc = tracker_.construction_loc();
            const char* loc_file = loc.file_name();
            const char* loc_func = loc.function_name();
            const auto loc_line = loc.line();
            const auto loc_col = loc.column();
            // file_name() returns "" for a default-constructed
            // location, which is what a handle minted without an
            // explicit location carries.
            const bool have_loc = loc_file != nullptr && loc_file[0] != '\0';

            if constexpr (!std::is_void_v<Derived>) {
                constexpr auto wname = detail::wrapper_class_name<Derived>();
                constexpr auto fname = detail::type_name<Derived>();
                std::fprintf(stderr,
                             "\n"
                             "═════════════════════════════════════════════════════════════════════\n"
                             "crucible::safety::proto: ABANDONMENT DETECTED (non-terminal handle)\n"
                             "═════════════════════════════════════════════════════════════════════\n"
                             "  Wrapper class:    %.*s\n"
                             "  Full handle type: %.*s\n"
                             "  Protocol head:    %.*s\n",
                             static_cast<int>(wname.size()), wname.data(), static_cast<int>(fname.size()), fname.data(),
                             static_cast<int>(pname.size()), pname.data());
            } else {
                std::fprintf(stderr,
                             "\n"
                             "═════════════════════════════════════════════════════════════════════\n"
                             "crucible::safety::proto: ABANDONMENT DETECTED (non-terminal handle)\n"
                             "═════════════════════════════════════════════════════════════════════\n"
                             "  Wrapper class:    SessionHandle (Derived not provided to base)\n"
                             "  Protocol head:    %.*s\n",
                             static_cast<int>(pname.size()), pname.data());
            }
            if (have_loc) {
                std::fprintf(stderr,
                             "  Construction at:  %s:%u:%u\n"
                             "  In function:      %s\n",
                             loc_file, static_cast<unsigned>(loc_line), static_cast<unsigned>(loc_col), loc_func);
            } else {
                std::fprintf(stderr, "  Construction at:  <unknown — handle minted without "
                                     "source_location capture>\n");
            }
            std::fprintf(stderr,
                         "  Expected action:  call .%.*s\n"
                         "\n"
                         "The handle was destroyed via its destructor without being\n"
                         "consumed via a &&-qualified consumer method.  Either:\n"
                         "  1. Consume the handle by calling its appropriate consumer\n"
                         "     method (close / send / recv / pick / branch / delegate /\n"
                         "     accept / base / rollback), OR\n"
                         "  2. Advance the protocol to a terminal state (End or Stop), OR\n"
                         "  3. Explicitly abandon via std::move(handle).detach(reason),\n"
                         "     where `reason` is one of:\n"
                         "       * detach_reason::InfiniteLoopProtocol\n"
                         "           — Loop<X> with no close branch; transport-level close.\n"
                         "       * detach_reason::TransportClosedOutOfBand\n"
                         "           — peer crash detected (CNTP RETRY_EXC, SWIM dead, fd close).\n"
                         "       * detach_reason::TestInstrumentation\n"
                         "           — test code intentionally drops at a known-safe point.\n"
                         "       * detach_reason::AsyncCancellation\n"
                         "           — std::jthread stop_token fired mid-protocol.\n"
                         "       * detach_reason::OwnerLifetimeBoundEarlyExit\n"
                         "           — bridge/wrapper destructor; last-resort abandonment.\n"
                         "\n"
                         "The framework cannot recover the protocol; aborting.\n"
                         "═════════════════════════════════════════════════════════════════════\n",
                         static_cast<int>(hint.size()), hint.data());
            std::abort();
        }
#endif
    }
};

// The primary template is deliberately undefined.  Only a head
// specialization can be instantiated, so naming a Loop here is a
// compile error: the factory unrolls a Loop before building a handle.

template <typename Proto, typename Resource, typename LoopCtx = void>
class SessionHandle;

namespace detail {

// The sole authorized constructor of a bare handle.  Every handle
// specialization's value constructor is private and befriends only
// this factory and the handle family, so a direct
// `SessionHandle<Proto, Res, Ctx>{res}` at a call site is rejected as
// private and cannot bypass the well-formedness gate or the
// Continue-and-Loop resolution.
//
// It is deliberately unconstrained.  Both callers run their own
// admission first, and the exposed LoopCtx parameter is what gives a
// non-void loop context a sanctioned construction site, which the
// public factory cannot reach because it always builds LoopCtx = void.
template <typename Proto, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto make_session_handle(
    Resource r,
    std::source_location loc = std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
    -> SessionHandle<Proto, Resource, LoopCtx> {
    return SessionHandle<Proto, Resource, LoopCtx>{std::move(r), loc};
}

template <typename R, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto step_to_next(Resource r,
                                          std::source_location loc = std::source_location::current()) noexcept {
    if constexpr (std::is_same_v<R, Continue>) {
        using ActiveLoopCtx = session_loop_ctx_inner_t<LoopCtx>;
        static_assert(!std::is_void_v<ActiveLoopCtx>, "crucible::session::diagnostic [Continue_Without_Loop]: "
                                                      "proto: Continue appears outside a Loop context.  "
                                                      "Every Continue must have an enclosing Loop<Body>.");
        using NextBody = typename ActiveLoopCtx::body;
        // The body may itself begin with a Loop or a Continue, so this
        // recurses.  Forwarding `loc` keeps the outermost caller's site
        // rather than replacing it with this frame's.
        return step_to_next<NextBody, Resource, LoopCtx>(std::move(r), loc);
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = typename R::body;
        using InnerCtx = session_loop_ctx_rebind_inner_t<LoopCtx, R>;
        // Entering an inner Loop shadows the enclosing loop context.
        // That shadowing is what binds Continue to the nearest Loop.
        return step_to_next<InnerBody, Resource, InnerCtx>(std::move(r), loc);
    } else {
        static_assert(is_head_v<R>, "crucible::session::diagnostic [Protocol_Ill_Formed]: "
                                    "proto: unexpected protocol shape after resolution.  "
                                    "Only Send/Recv/Select/Offer/End/Continue are valid heads.");
        return make_session_handle<R, Resource, LoopCtx>(std::move(r), loc);
    }
}

}  // namespace detail

template <typename Resource, typename LoopCtx>
class [[nodiscard]]
SessionHandle<End, Resource, LoopCtx> : public SessionHandleBase<End, SessionHandle<End, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename R, typename L>
    friend class SessionHandle;

    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<End, SessionHandle<End, Resource, LoopCtx>>{loc}, resource_{std::move(r)} {}

public:
    using protocol = End;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    // Closing is what hands the Resource back.  It is available only
    // at End, so a caller can recover the Resource only after the
    // protocol has run to completion.
    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return std::move(resource_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename T, typename R, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Send<T, R>, Resource, LoopCtx>
    : public SessionHandleBase<Send<T, R>, SessionHandle<Send<T, R>, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename Res, typename L>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Send<T, R>, SessionHandle<Send<T, R>, Resource, LoopCtx>>{loc}, resource_{std::move(r)} {}

public:
    using protocol = Send<T, R>;
    using message_type = T;
    using continuation = R;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    // The Transport is what physically moves the value to the peer.
    // Its signature is void(Resource&, T&&).  The returned handle sits
    // at the continuation, with Continue and Loop already resolved.
    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto
    send(T value, Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, T&&>
                                                   && std::is_nothrow_move_constructible_v<Resource>
                                                   && std::is_nothrow_move_constructible_v<T>) {
        std::invoke(transport, resource_, std::move(value));
        this->mark_consumed_();
        return detail::step_to_next<R, Resource, LoopCtx>(std::move(resource_));
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename T, typename R, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Recv<T, R>, Resource, LoopCtx>
    : public SessionHandleBase<Recv<T, R>, SessionHandle<Recv<T, R>, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename Res, typename L>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Recv<T, R>, SessionHandle<Recv<T, R>, Resource, LoopCtx>>{loc}, resource_{std::move(r)} {}

public:
    using protocol = Recv<T, R>;
    using message_type = T;
    using continuation = R;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    // The Transport signature is T(Resource&).  The pair is the
    // received value and the handle at the continuation.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto
    recv(Transport transport) && noexcept(std::is_nothrow_invocable_r_v<T, Transport, Resource&>
                                          && std::is_nothrow_move_constructible_v<Resource>
                                          && std::is_nothrow_move_constructible_v<T>) {
        T value = std::invoke(transport, resource_);
        this->mark_consumed_();
        auto next = detail::step_to_next<R, Resource, LoopCtx>(std::move(resource_));
        return std::pair{std::move(value), std::move(next)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Select<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Select<Branches...>, SessionHandle<Select<Branches...>, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename Res, typename L>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Select<Branches...>, SessionHandle<Select<Branches...>, Resource, LoopCtx>>{loc},
          resource_{std::move(r)} {}

public:
    using protocol = Select<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    // A second empty-choice rejection, so a construction route that
    // reaches this class without passing the factory's own check still
    // fires the same diagnostic.  Subtyping uses of Select<> never
    // instantiate this class and are unaffected.
    static_assert(branch_count > 0, "crucible::session::diagnostic [Empty_Choice_Combinator]: "
                                    "SessionHandle<Select<>>: cannot construct a runnable handle "
                                    "on Select<> with zero branches — there is no branch for "
                                    ".pick<I>() to select.  See mint_session_handle for the full "
                                    "diagnostic and remediation.");

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    // Picks branch I and signals the choice to the peer through
    // Transport, whose signature is void(Resource&, std::size_t).
    //
    // The index bound is a body static_assert rather than a
    // requires-clause so that an out-of-range index reports the named
    // diagnostic instead of a bare unsatisfied-constraint message.
    // Transport still gates overload resolution.
    template <std::size_t I, typename Transport>
        requires std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto
    select(Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, std::size_t>
                                            && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < sizeof...(Branches), "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                               "SessionHandle<Select<...>>::select<I>(transport): branch "
                                               "index I is out of range for this Select position.  The "
                                               "protocol has fewer branches than the index requested; "
                                               "verify I < branch_count at the call site (decltype("
                                               "handle)::branch_count is exposed for compile-time queries).");
        std::invoke(transport, resource_, I);
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

    // Advances the local handle WITHOUT telling the peer which branch
    // was picked.  The name carries the omission so it is visible at
    // every call site.  On a wire-based session the peer never learns
    // the choice and the two endpoints drift apart, so this is for an
    // in-memory channel, a mocked transport, or a pipeline whose
    // branch is fixed at compile time on both sides.
    template <std::size_t I>
    [[nodiscard]] constexpr auto select_local() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < sizeof...(Branches), "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                               "SessionHandle<Select<...>>::select_local<I>(): branch index "
                                               "I is out of range for this Select position.  The protocol "
                                               "has fewer branches than the index requested; verify I < "
                                               "branch_count at the call site.");
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

    // Deleting the zero-argument form forces every call site to state
    // whether the peer is told.  A default would have to pick one, and
    // picking the silent one drifts wire-based sessions apart.
    template <std::size_t I>
    void select() && = delete("[Wire_Variant_Required] SessionHandle<Select<...>>::select<I>() "
                              "without arguments is not allowed.  Choose one: "
                              "(a) `select<I>(transport)` to signal the branch choice over "
                              "the wire (the peer sees the I-th branch and stays in sync), "
                              "OR (b) `select_local<I>()` to advance the local handle WITHOUT "
                              "signalling the peer (in-memory channels and unit tests only — "
                              "wire-based sessions where the peer doesn't observe the "
                              "choice will silently drift off-protocol).  The framework "
                              "refuses to guess which one you meant.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Offer<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>, SessionHandle<Offer<Branches...>, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename Res, typename L>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Offer<Branches...>, SessionHandle<Offer<Branches...>, Resource, LoopCtx>>{loc},
          resource_{std::move(r)} {}

public:
    using protocol = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    // Mirror of the Select guard.  No peer label decodes to a valid
    // branch here.
    static_assert(branch_count > 0, "crucible::session::diagnostic [Empty_Choice_Combinator]: "
                                    "SessionHandle<Offer<>>: cannot construct a runnable handle "
                                    "on Offer<> with zero branches — there is no label the peer "
                                    "can send.  See mint_session_handle for the full diagnostic "
                                    "and remediation.");

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    // Receives the peer's branch label through Transport, whose
    // signature is std::size_t(Resource&), then calls the handler with
    // the handle for that branch.  The handler is invoked once per
    // branch type and every branch must give the handler the same
    // return type, or all of them void.
    //
    // An out-of-range label aborts.  The peer has sent a label this
    // protocol does not define, so the two endpoints no longer agree
    // and no branch can be entered safely.
    template <typename Transport, typename Handler>
        requires std::is_invocable_r_v<std::size_t, Transport, Resource&>
    constexpr auto branch(Transport transport, Handler handler) && {
        const std::size_t idx = std::invoke(transport, resource_);
        this->mark_consumed_();
        return dispatch_branch_(idx, std::move(resource_), std::move(handler),
                                std::make_index_sequence<sizeof...(Branches)>{});
    }

    // Assumes branch I WITHOUT receiving the peer's label.  The name
    // carries the omission so it is visible at every call site.  If
    // the peer signals a different branch the two endpoints diverge,
    // so this is for an in-memory channel, a mocked transport, or a
    // pipeline whose branch is fixed at compile time on both sides.
    template <std::size_t I>
    [[nodiscard]] constexpr auto pick_local() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < sizeof...(Branches), "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                               "SessionHandle<Offer<...>>::pick_local<I>(): branch index "
                                               "I is out of range for this Offer position.  The protocol "
                                               "has fewer branches than the index requested; verify I < "
                                               "branch_count at the call site.");
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

    // Deleting the zero-argument form forces every call site to state
    // whether the peer's label is read.
    template <std::size_t I>
    void pick() && = delete("[Wire_Variant_Required] SessionHandle<Offer<...>>::pick<I>() "
                            "without arguments is not allowed.  Use "
                            "`pick_local<I>()` to advance the local handle WITHOUT "
                            "receiving a peer label (in-memory channels and unit tests "
                            "only — wire-based sessions where the peer's actual choice "
                            "differs from I will silently drift off-protocol).  The "
                            "framework refuses to guess that the peer-skipping "
                            "variant was what you meant.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }

private:
    template <std::size_t I>
    static constexpr auto make_branch_handle_(Resource r) {
        using B = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<B, Resource, LoopCtx>(std::move(r));
    }

    template <std::size_t... Is, typename Handler>
    static constexpr auto dispatch_branch_(std::size_t idx, Resource res, Handler handler, std::index_sequence<Is...>) {
        if (idx >= sizeof...(Branches)) [[unlikely]] {
            std::abort();
        }

        // The result type comes from branch 0.  A branch whose handler
        // returns a different type then fails to convert into the
        // single optional below, which is what enforces the
        // same-return-type rule.
        using FirstHandle = decltype(make_branch_handle_<0>(std::declval<Resource>()));
        using Result = std::invoke_result_t<Handler&&, FirstHandle>;

        if constexpr (std::is_void_v<Result>) {
            bool dispatched = false;
            (
                [&]() {
                    if (!dispatched && idx == Is) {
                        std::invoke(std::move(handler), make_branch_handle_<Is>(std::move(res)));
                        dispatched = true;
                    }
                }(),
                ...);
        } else {
            std::optional<Result> result;
            bool dispatched = false;
            (
                [&]() {
                    if (!dispatched && idx == Is) {
                        result.emplace(std::invoke(std::move(handler), make_branch_handle_<Is>(std::move(res))));
                        dispatched = true;
                    }
                }(),
                ...);
            if (!result) [[unlikely]]
                std::abort();
            return std::move(*result);
        }
    }
};

// The Resource is what a handle stores at runtime, and the concept
// exists to stop a handle from outliving what it points at.
//
// A value type is safe because the handle owns it and their lifetimes
// coincide.  An lvalue reference to a Pinned object is safe because a
// Pinned object cannot be moved, so its address is stable for its
// whole lifetime and the caller only has to outlive the handles.  An
// lvalue reference to a non-Pinned object is rejected: moving or
// assigning to the referent relocates it and every live handle dangles
// with no diagnostic.  An rvalue reference is rejected because it
// would bind to a temporary that dies before the handle is used.
//
// A raw object pointer is admitted without requiring a Pinned pointee.
// Code that reaches for a raw pointer is already in a manual-lifetime
// regime the type system can only partly support, and the pointer
// itself is a value the handle owns.  A function pointer is admitted
// because a function's address is stable by language rule.

template <typename Resource>
concept SessionResource =
    !std::is_reference_v<Resource>
    || (std::is_lvalue_reference_v<Resource>
        && std::derived_from<std::remove_reference_t<Resource>, safety::Pinned<std::remove_reference_t<Resource>>>);

// The admission gate for handle construction, expressed as a concept
// rather than as body static_asserts alone.  A body static_assert is
// not visible to SFINAE: overload resolution accepts the signature for
// any Proto and the failure only appears at instantiation, where a
// requires-expression further up the stack cannot observe it.
// Downstream concepts that ask whether a handle can be minted need
// that answer, so the checks live in the signature.

template <typename Proto>
concept WellFormedRunnableProtocol = is_well_formed_v<Proto> && !is_empty_choice_v<Proto>;

// A Proto that starts with a Loop is unrolled one iteration, so the
// returned handle sits at the loop body with the Loop as its LoopCtx.
// Any other head passes through unchanged.
//
// The body static_asserts repeat the concept's checks.  They fire only
// if some route reaches the body without the concept having run, and
// their prose is what explains a rejection the concept states only as
// a boolean.

template <typename Proto, typename Resource>
    requires WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
[[nodiscard]] constexpr auto mint_session_handle(Resource r,
                                                 std::source_location loc = std::source_location::current()) noexcept {
    static_assert(is_well_formed_v<Proto>, "crucible::session::diagnostic [Protocol_Ill_Formed]: "
                                           "proto: protocol is ill-formed.  Most likely cause: a Continue "
                                           "appears outside any enclosing Loop<Body>.  Every Continue must "
                                           "have a Loop above it in the protocol tree.");

    // The rejection lives at the handle boundary, not in the type
    // machinery, so subtyping keeps admitting an empty choice as a
    // legitimate operand while handle construction refuses it.
    static_assert(!is_empty_choice_v<Proto>, "crucible::session::diagnostic [Empty_Choice_Combinator]: "
                                             "proto: mint_session_handle<Proto> — Proto contains a "
                                             "reachable empty Select<> / Offer<> / Offer<Sender<R>> "
                                             "(top-level or nested under Send/Recv/Loop/branch/Delegate/"
                                             "Accept).  Cannot construct a runnable handle: Select<> has "
                                             "no branch for .pick<I>() to select; Offer<> has no label "
                                             "the peer can signal.  The trait walks recursively so "
                                             "nested empties are caught at mint time, not at the "
                                             "eventual .pick<I>() / .recv() that hits the dead-end.  "
                                             "If you intend a type-level subtyping witness, use the "
                                             "subtyping trait directly; if you intend a runnable "
                                             "handle, add at least one branch at every reachable choice "
                                             "position (e.g., Select<Send<Stop, End>>).");

    static_assert(SessionResource<Resource>, "crucible::session::diagnostic [SessionResource_NotPinned]: "
                                             "mint_session_handle<Proto, Resource>: Resource must be either "
                                             "a value type (handle owns it by value) or an lvalue reference "
                                             "to a type derived from safety::Pinned<T>.  An lvalue reference "
                                             "to a non-Pinned object lets a subsequent move of the channel "
                                             "leave live handles dangling (use-after-free).  Either: (a) "
                                             "make the channel Pinned by deriving it from "
                                             "safety::Pinned<ChannelType>, or (b) pass the channel by "
                                             "value (copies are fine for value-like channels), or (c) wrap "
                                             "the channel in std::reference_wrapper if the caller's "
                                             "lifetime contract is satisfied by other means.  Rvalue-"
                                             "reference Resource is also rejected — the handle would bind "
                                             "to a temporary and dangle immediately on return.");

    if constexpr (is_loop_v<Proto>) {
        using Body = typename Proto::body;
        // Forwarding `loc` keeps the caller's site in the abandonment
        // diagnostic even though the unroll inserts an intermediate
        // handle whose own default location would otherwise win.
        return detail::step_to_next<Body, Resource, Proto>(std::move(r), loc);
    } else {
        static_assert(!std::is_same_v<Proto, Continue>, "crucible::session::diagnostic [Continue_Without_Loop]: "
                                                        "proto: Continue cannot be the top-level protocol.");
        return detail::make_session_handle<Proto, Resource, void>(std::move(r), loc);
    }
}

// Channel construction carries both endpoints' execution contexts, so
// that the row, vendor, epoch and permission gates run against both
// local protocols rather than only one.  The resource-only form is
// deleted because it can check neither.

template <typename Proto, typename ResourceA, typename ResourceB>
void mint_channel(ResourceA, ResourceB) noexcept = delete(
    "mint_channel<Proto>(resource_a, resource_b) is removed.  Include SessionMint.h and call mint_channel<Proto>(ctx_a, ctx_b, resource_a, resource_b) so both endpoints are row-admitted.");

// These assertions state the framework's semantics by example and
// fail the build on any edit that breaks duality, composition or
// well-formedness.
//
// They are gated because every translation unit that includes this
// family would otherwise pay their compile cost, and the invariants
// are global: checking them in one dedicated translation unit gives
// the same coverage.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::self_test {

static_assert(std::is_same_v<dual_of_t<End>, End>);
static_assert(std::is_same_v<dual_of_t<Continue>, Continue>);
static_assert(std::is_same_v<dual_of_t<Send<int, End>>, Recv<int, End>>);
static_assert(std::is_same_v<dual_of_t<Recv<int, End>>, Send<int, End>>);
static_assert(std::is_same_v<dual_of_t<Select<End, End>>, Offer<End, End>>);
static_assert(std::is_same_v<dual_of_t<Offer<End, End>>, Select<End, End>>);
static_assert(std::is_same_v<dual_of_t<Loop<Send<int, Continue>>>, Loop<Recv<int, Continue>>>);

// Duality is involutive everywhere except a Sender-annotated Offer.
static_assert(std::is_same_v<dual_of_t<dual_of_t<Send<int, End>>>, Send<int, End>>);
static_assert(std::is_same_v<dual_of_t<dual_of_t<Loop<Select<Send<int, Continue>, End>>>>,
                             Loop<Select<Send<int, Continue>, End>>>);
static_assert(is_dual_involutive_v<Send<int, End>>);
static_assert(is_dual_involutive_v<Loop<Select<Send<int, Continue>, End>>>);
static_assert(is_dual_involutive_v<Offer<Send<int, End>, Recv<int, End>>>);

// The asymmetry is asserted, not merely tolerated, so that a change
// which silently restores involution reddens here instead of quietly
// changing what generic code is allowed to assume.
namespace dual_involution_asymmetry {
struct RoleA {};
using Annotated = Offer<Sender<RoleA>, Recv<int, End>>;
using RoundTrip = dual_of_t<dual_of_t<Annotated>>;
using Stripped = Offer<Recv<int, End>>;
static_assert(!std::is_same_v<RoundTrip, Annotated>, "The dual of a Sender-annotated Offer drops the role tag, so "
                                                     "involution fails on this shape by design.");
static_assert(std::is_same_v<RoundTrip, Stripped>, "The round trip of a Sender-annotated Offer produces the "
                                                   "un-annotated form.");
static_assert(!is_dual_involutive_v<Annotated>, "is_dual_involutive_v reports false on a Sender-annotated Offer "
                                                "so generic code can refuse the protocol.");
static_assert(is_dual_involutive_v<Stripped>, "An un-annotated Offer remains involutive.");
}  // namespace dual_involution_asymmetry

static_assert(std::is_same_v<compose_t<End, Send<int, End>>, Send<int, End>>);
static_assert(std::is_same_v<compose_t<Send<int, End>, Recv<bool, End>>, Send<int, Recv<bool, End>>>);
static_assert(std::is_same_v<compose_t<Send<int, Select<End, End>>, Recv<bool, End>>,
                             Send<int, Select<Recv<bool, End>, Recv<bool, End>>>>);
// Continue is not End, so the loop body stays closed under composition.
static_assert(std::is_same_v<compose_t<Loop<Send<int, Continue>>, Recv<bool, End>>, Loop<Send<int, Continue>>>);

static_assert(is_well_formed_v<End>);
static_assert(is_well_formed_v<Send<int, End>>);
static_assert(is_well_formed_v<Loop<Send<int, Continue>>>);
static_assert(is_well_formed_v<Loop<Select<Send<int, Continue>, End>>>);
// A Continue with no enclosing Loop is stuck.
static_assert(!is_well_formed_v<Continue>);
static_assert(!is_well_formed_v<Send<int, Continue>>);
static_assert(!is_well_formed_v<Select<Continue, End>>);

// A loop body that is entirely terminal never reaches Continue.
static_assert(!is_well_formed_v<Loop<End>>);
static_assert(!is_well_formed_v<Loop<VendorPinned<VendorBackend::CPU, End>>>);

// Nested loops: Continue binds to the INNERMOST enclosing Loop.
static_assert(is_well_formed_v<Loop<Loop<Send<int, Continue>>>>);

static_assert(is_send_v<Send<int, End>>);
static_assert(!is_send_v<Recv<int, End>>);
static_assert(is_loop_v<Loop<End>>);
static_assert(!is_loop_v<End>);

namespace mpmc_shape_test {
struct Item {};
using ProducerP = Loop<Select<Send<Item, Continue>, End>>;
using ConsumerP = dual_of_t<ProducerP>;
static_assert(is_well_formed_v<ProducerP>);
static_assert(is_well_formed_v<ConsumerP>);
static_assert(std::is_same_v<ConsumerP, Loop<Offer<Recv<Item, Continue>, End>>>);
static_assert(std::is_same_v<dual_of_t<ConsumerP>, ProducerP>);
}  // namespace mpmc_shape_test

namespace req_resp_test {
struct Req {};
struct Resp {};
using Server = Loop<Recv<Req, Send<Resp, Continue>>>;
using Client = dual_of_t<Server>;
static_assert(std::is_same_v<Client, Loop<Send<Req, Recv<Resp, Continue>>>>);
static_assert(is_well_formed_v<Server>);
static_assert(is_well_formed_v<Client>);
}  // namespace req_resp_test

namespace two_pc_test {
struct Prepare {};
struct Vote {};
struct Commit {};
struct Abort {};
using Coord = Send<Prepare, Recv<Vote, Select<Send<Commit, End>, Send<Abort, End>>>>;
using Follower = dual_of_t<Coord>;
static_assert(std::is_same_v<Follower, Recv<Prepare, Send<Vote, Offer<Recv<Commit, End>, Recv<Abort, End>>>>>);
static_assert(is_well_formed_v<Coord>);
static_assert(is_well_formed_v<Follower>);
}  // namespace two_pc_test

// The concept's truth table stands in for the mint's, because the
// mint's requires-clause is exactly this concept conjoined with the
// resource gate.  Asserting it here is what pins the gate as
// SFINAE-visible: a caller asking whether a handle can be minted gets
// false for an ill-formed protocol instead of a hard error at
// instantiation.

namespace mint_sfinae_test {
struct FakeRes {
    int sentinel = 0;
};

static_assert(WellFormedRunnableProtocol<End>);
static_assert(WellFormedRunnableProtocol<Send<int, End>>);
static_assert(WellFormedRunnableProtocol<Loop<Send<int, Continue>>>);
static_assert(!WellFormedRunnableProtocol<Continue>);
static_assert(!WellFormedRunnableProtocol<Send<int, Continue>>);
static_assert(!WellFormedRunnableProtocol<Select<>>);
static_assert(!WellFormedRunnableProtocol<Offer<>>);
static_assert(!WellFormedRunnableProtocol<Loop<End>>);
}  // namespace mint_sfinae_test

}  // namespace detail::self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

// A handle must add no bytes beyond its Resource in release.  These
// checks apply only under NDEBUG, since a debug handle carries the
// abandonment tracker and is legitimately larger.
//
// A failure here means the empty-base optimization did not collapse
// SessionHandleBase, most likely because the compiler declined to
// treat the [[no_unique_address]] empty member as a collapse
// candidate.  The remedy is to split SessionHandleBase into two
// implementations selected on NDEBUG, one truly empty and one holding
// the flag.

#ifdef NDEBUG
namespace detail::release_size_test {
struct OneByteRes {
    char x;
};
struct FourByteRes {
    int x;
};
struct EightByteRes {
    double x;
};

static_assert(sizeof(SessionHandle<End, OneByteRes>) == sizeof(OneByteRes),
              "Release-mode SessionHandle<End, OneByteRes> must equal sizeof(OneByteRes) "
              "— the EBO must collapse SessionHandleBase to zero bytes.  If this fires, "
              "see the comment block above the assert for remediation.");

static_assert(sizeof(SessionHandle<End, FourByteRes>) == sizeof(FourByteRes),
              "Release-mode SessionHandle<End, FourByteRes> must equal sizeof(FourByteRes).");

static_assert(sizeof(SessionHandle<End, EightByteRes>) == sizeof(EightByteRes),
              "Release-mode SessionHandle<End, EightByteRes> must equal sizeof(EightByteRes).");

static_assert(sizeof(SessionHandle<Send<int, End>, FourByteRes>) == sizeof(FourByteRes),
              "Release-mode SessionHandle<Send, FourByteRes> must equal sizeof(FourByteRes).");
}  // namespace detail::release_size_test
#endif  // NDEBUG

}  // namespace crucible::safety::proto
