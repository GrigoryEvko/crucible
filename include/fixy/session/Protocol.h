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
//
// This header holds the TYPE level only: the combinators, the traits
// over them, duality, composition and well-formedness.  Nothing here
// has a runtime representation, so nothing here depends on the
// abandonment policy.  The handle and that policy live in
// fixy/session/Handle.h, which is where the only runtime cost is.
// Splitting them is deliberate: a translation unit that reasons about
// protocols — a subtyping check, a bridge's admission clause — pays
// for no destructor machinery and cannot be affected by the policy
// choice.
//
// Deviation from the ported source (crucible/sessions/Session.h): the
// EpochCtx context-axis wrapper and its three threshold helpers are
// NOT ported here.  They read EpochLattice and GenerationLattice,
// which exist only in the frozen tree — foundation/algebra/lattices/
// has VendorLattice but neither of those two, and porting them is the
// foundation stream's file, not this one's.  The LoopCtx traits keep
// their indirection (primary template plus the void case plus
// rebind_inner) so EpochCtx drops in as one specialization of each
// when those lattices land.  Without it session_loop_ctx_inner_t<L>
// is L, which is exactly what the unwrapped case already meant.

#include <foundation/algebra/lattices/VendorLattice.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::session {

using ::foundation::algebra::lattices::VendorBackend;

// ── Combinators ──────────────────────────────────────────────────────

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

// ── Loop context ─────────────────────────────────────────────────────
//
// The traits indirection has one inhabitant while EpochCtx is absent:
// a LoopCtx is its own inner context.  It is kept rather than folded
// away because the context axis is an extension point — a wrapper that
// carries admission facts alongside the loop specializes these three
// and every Continue resolution keeps working unchanged.

template <typename LoopCtx>
struct session_loop_ctx_traits {
    using inner_loop_ctx = LoopCtx;
};

template <>
struct session_loop_ctx_traits<void> {
    using inner_loop_ctx = void;
};

template <typename LoopCtx>
using session_loop_ctx_inner_t = typename session_loop_ctx_traits<LoopCtx>::inner_loop_ctx;

template <typename LoopCtx, typename NewInnerLoopCtx>
struct session_loop_ctx_rebind_inner {
    using type = NewInnerLoopCtx;
};

template <typename LoopCtx, typename NewInnerLoopCtx>
using session_loop_ctx_rebind_inner_t = typename session_loop_ctx_rebind_inner<LoopCtx, NewInnerLoopCtx>::type;

// ── Shape traits ─────────────────────────────────────────────────────

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
// sole non-head because it has no handle specialization: the factory
// unrolls it and positions the handle at the body.
template <typename P>
inline constexpr bool is_head_v = !is_loop_v<P>;

// ── Duality ──────────────────────────────────────────────────────────

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
    static_assert(is_dual_v<P1, P2>, "fixy::session::diagnostic [Dual_Mismatch]: "
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

// ── Composition ──────────────────────────────────────────────────────
//
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
    static_assert(I < sizeof...(Bs), "fixy::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
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
    static_assert(I < sizeof...(Bs), "fixy::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
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
    static_assert(I < sizeof...(Bs), "fixy::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
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
    static_assert(sizeof(Q) == 0, "fixy::session::diagnostic [Branch_Compose_No_Choice]: "
                                  "compose_at_branch_t<P, I, Q>: walked P's spine to End without "
                                  "encountering a Select<Bs...> or Offer<Bs...> to compose at.  "
                                  "Branch-asymmetric composition requires P to contain a choice "
                                  "combinator at some position reachable from the head via "
                                  "Send/Recv/Loop pass-through.  If you intended UNIFORM "
                                  "composition (every End -> Q), use the existing compose_t<P, Q> "
                                  "instead.");
};

template <std::size_t I, typename Q>
struct compose_at_branch<Continue, I, Q> {
    static_assert(sizeof(Q) == 0, "fixy::session::diagnostic [Branch_Compose_No_Choice]: "
                                  "compose_at_branch_t<P, I, Q>: walked P's spine to Continue "
                                  "without encountering a Select or Offer.  Continue is a loop-"
                                  "back marker, not a choice point; compose_at_branch is meant "
                                  "for choice combinators.");
};

template <typename P, std::size_t I, typename Q>
using compose_at_branch_t = typename compose_at_branch<P, I, Q>::type;

// ── Well-formedness ──────────────────────────────────────────────────
//
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

}  // namespace fixy::session
