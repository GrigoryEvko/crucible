#pragma once

// The relation here is synchronous: message order is preserved across
// refinement, so a subtype may not move a Send ahead of an unrelated Recv.
// Branches are positional rather than labeled, which makes the relation
// stricter than a labeled formulation — reordering the branches of a Select or
// an Offer is not a subtype even though it preserves the branch set.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionGrade.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// A specialisation of is_subsort declares one payload type to be a value-level
// subtype of another.  Reflexivity holds by the default definition.
// Transitivity is not closed: the relation propagates through payload positions
// exactly as declared, so a user who declares A below B and B below C but not A
// below C gets a hole in the derived protocol relation.  Closing it costs
// compile time quadratic in the number of specialisations, so a user who needs
// transitivity declares every edge, or routes each payload class through one
// canonical supertype.

template <typename T, typename U>
struct is_subsort : std::is_same<T, U> {};

template <typename T, typename U>
inline constexpr bool is_subsort_v = is_subsort<T, U>::value;

template <typename T, typename U>
struct is_subtype_sync_structural : std::false_type {};

template <>
struct is_subtype_sync_structural<End, End> : std::true_type {};

// This is the coinductive hypothesis.  Reaching a Continue means the two
// enclosing Loops are already being related, and each Continue refers to its
// own enclosing Loop, so the pair holds by assumption.
template <>
struct is_subtype_sync_structural<Continue, Continue> : std::true_type {};

// Send is covariant in the payload: a narrower payload stands where a wider one
// is expected.
template <typename P1, typename R1, typename P2, typename R2>
struct is_subtype_sync_structural<Send<P1, R1>, Send<P2, R2>>
    : std::bool_constant<is_subsort_v<P1, P2> && is_subtype_sync_structural<R1, R2>::value> {};

// Recv is contravariant in the payload, hence the swapped argument order: a
// receiver that accepts a wider payload stands where one accepting a narrower
// payload is expected.
template <typename P1, typename R1, typename P2, typename R2>
struct is_subtype_sync_structural<Recv<P1, R1>, Recv<P2, R2>>
    : std::bool_constant<is_subsort_v<P2, P1> && is_subtype_sync_structural<R1, R2>::value> {};

// The recursion terminates because each step consumes one combinator
// constructor.  The only cycle runs through Continue, which the rule above
// discharges, so no explicit fixed-point tracking is needed.
template <typename B1, typename B2>
struct is_subtype_sync_structural<Loop<B1>, Loop<B2>> : is_subtype_sync_structural<B1, B2> {};

// The vendor arguments are swapped for the same reason the Recv payload is: a
// Portable protocol stands in where a vendor-pinned one is required, but a
// protocol pinned to one vendor stands in for neither another vendor nor
// Portable.
template <VendorBackend V1, typename P1, VendorBackend V2, typename P2>
struct is_subtype_sync_structural<VendorPinned<V1, P1>, VendorPinned<V2, P2>>
    : std::bool_constant<V1 != VendorBackend::None && V2 != VendorBackend::None && VendorLattice::leq(V2, V1)
                         && is_subtype_sync_structural<P1, P2>::value> {};

namespace detail::subtype {

template <typename BranchesA, typename BranchesB, std::size_t... Is>
[[nodiscard]] constexpr bool prefix_subtypes(std::index_sequence<Is...>) noexcept {
    return (is_subtype_sync_structural<std::tuple_element_t<Is, BranchesA>, std::tuple_element_t<Is, BranchesB>>::value
            && ...);
}

// The size gate is a separate template so that the prefix fold is instantiated
// only once the gate passes.  A single combined expression would instantiate
// std::tuple_element_t with an out-of-range index.
template <bool SizeOK, std::size_t PrefixLen, typename BranchesA, typename BranchesB>
struct gated_prefix_check : std::false_type {};

template <std::size_t PrefixLen, typename BranchesA, typename BranchesB>
struct gated_prefix_check<true, PrefixLen, BranchesA, BranchesB>
    : std::bool_constant<prefix_subtypes<BranchesA, BranchesB>(std::make_index_sequence<PrefixLen>{})> {};

}  // namespace detail::subtype

// A Select subtype picks from no more branches than its supertype.  The peer
// dual offers every supertype branch, so the branches the subtype never picks
// are simply never exercised, and the bound stops the subtype picking a
// position the peer does not have.
template <typename... B1s, typename... B2s>
struct is_subtype_sync_structural<Select<B1s...>, Select<B2s...>>
    : detail::subtype::gated_prefix_check<(sizeof...(B1s) <= sizeof...(B2s)), sizeof...(B1s), std::tuple<B1s...>,
                                          std::tuple<B2s...>> {};

// An Offer subtype handles at least as many branches as its supertype, so it
// covers every position the peer dual can pick.  Its extra branches are
// unreachable from a peer that speaks the supertype.
template <typename... B1s, typename... B2s>
struct is_subtype_sync_structural<Offer<B1s...>, Offer<B2s...>>
    : detail::subtype::gated_prefix_check<(sizeof...(B1s) >= sizeof...(B2s)), sizeof...(B2s), std::tuple<B1s...>,
                                          std::tuple<B2s...>> {};

// The grade filter follows the same polarity as the structural rule: Send
// compares payload grades covariantly and Recv contravariantly.  Comparing one
// aggregate grade per protocol would be simpler and would admit unsound Recv
// pairs.
namespace detail::subtype {

template <typename ProvidedPayload, typename RequiredPayload>
inline constexpr bool payload_grade_satisfies_v =
    ::crucible::safety::proto::detail::session_grade::satisfies_v<payload_grade_t<ProvidedPayload>,
                                                                  payload_grade_t<RequiredPayload>>;

template <typename T, typename U>
struct protocol_grade_satisfies : std::false_type {};

template <>
struct protocol_grade_satisfies<End, End> : std::true_type {};

template <>
struct protocol_grade_satisfies<Continue, Continue> : std::true_type {};

template <typename P1, typename R1, typename P2, typename R2>
struct protocol_grade_satisfies<Send<P1, R1>, Send<P2, R2>>
    : std::bool_constant<payload_grade_satisfies_v<P1, P2> && protocol_grade_satisfies<R1, R2>::value> {};

template <typename P1, typename R1, typename P2, typename R2>
struct protocol_grade_satisfies<Recv<P1, R1>, Recv<P2, R2>>
    : std::bool_constant<payload_grade_satisfies_v<P2, P1> && protocol_grade_satisfies<R1, R2>::value> {};

template <typename B1, typename B2>
struct protocol_grade_satisfies<Loop<B1>, Loop<B2>> : protocol_grade_satisfies<B1, B2> {};

template <VendorBackend V1, typename P1, VendorBackend V2, typename P2>
struct protocol_grade_satisfies<VendorPinned<V1, P1>, VendorPinned<V2, P2>>
    : std::bool_constant<VendorLattice::leq(V2, V1) && protocol_grade_satisfies<P1, P2>::value> {};

template <typename BranchesA, typename BranchesB, std::size_t... Is>
[[nodiscard]] constexpr bool prefix_grade_satisfies(std::index_sequence<Is...>) noexcept {
    return (protocol_grade_satisfies<std::tuple_element_t<Is, BranchesA>, std::tuple_element_t<Is, BranchesB>>::value
            && ...);
}

template <bool SizeOK, std::size_t PrefixLen, typename BranchesA, typename BranchesB>
struct gated_grade_prefix_check : std::false_type {};

template <std::size_t PrefixLen, typename BranchesA, typename BranchesB>
struct gated_grade_prefix_check<true, PrefixLen, BranchesA, BranchesB>
    : std::bool_constant<prefix_grade_satisfies<BranchesA, BranchesB>(std::make_index_sequence<PrefixLen>{})> {};

template <typename... B1s, typename... B2s>
struct protocol_grade_satisfies<Select<B1s...>, Select<B2s...>>
    : gated_grade_prefix_check<(sizeof...(B1s) <= sizeof...(B2s)), sizeof...(B1s), std::tuple<B1s...>,
                               std::tuple<B2s...>> {};

template <typename... B1s, typename... B2s>
struct protocol_grade_satisfies<Offer<B1s...>, Offer<B2s...>>
    : gated_grade_prefix_check<(sizeof...(B1s) >= sizeof...(B2s)), sizeof...(B2s), std::tuple<B1s...>,
                               std::tuple<B2s...>> {};

template <bool StructuralOK, typename T, typename U>
struct grade_filtered_subtype : std::false_type {};

template <typename T, typename U>
struct grade_filtered_subtype<true, T, U> : protocol_grade_satisfies<T, U> {};

}  // namespace detail::subtype

template <typename T, typename U>
struct is_subtype_sync : detail::subtype::grade_filtered_subtype<is_subtype_sync_structural<T, U>::value, T, U> {};

template <typename Provided, typename Required>
inline constexpr bool protocol_grade_satisfies_v = detail::subtype::protocol_grade_satisfies<Provided, Required>::value;

template <typename T, typename U>
inline constexpr bool is_subtype_sync_v = is_subtype_sync<T, U>::value;

template <typename T, typename U>
concept SubtypeSync = is_subtype_sync_v<T, U>;

// The consteval wrapper puts the diagnostic at the call site instead of deep
// inside a metafunction instantiation.
template <typename T, typename U>
consteval void assert_subtype_sync() noexcept {
    static_assert(is_subtype_sync_v<T, U>, "crucible::session::diagnostic [SubtypeMismatch]: "
                                           "assert_subtype_sync: T is not a synchronous subtype of U.  "
                                           "Common causes: shape mismatch "
                                           "(Send vs Recv, Select vs Offer); too many/too few branches "
                                           "(subtype has more Select branches than supertype, or fewer "
                                           "Offer branches); payload types not related via is_subsort "
                                           "specialisation; [ProtocolGradeMismatch] the structural "
                                           "payload relation was admitted but the ProductLattice grade "
                                           "filter rejected at least one Vendor, NumericalTier, "
                                           "CipherTier, CrashClass, EpochVersioned, or NumaPlacement "
                                           "axis.  Check the template-instantiation context for the "
                                           "failing T and U.");
}

template <typename T, typename U>
consteval void assert_vendor_subtype_sync() noexcept {
    static_assert(is_subtype_sync_v<T, U>, "crucible::session::diagnostic [VendorCtx_Mismatch]: "
                                           "assert_vendor_subtype_sync: T is not a vendor-compatible "
                                           "synchronous subtype of U.  VendorPinned<V1, P1> may stand "
                                           "where VendorPinned<V2, P2> is expected only when "
                                           "VendorLattice::leq(V2, V1) holds and P1 is a synchronous "
                                           "subtype of P2.  Distinct vendor-specific protocols such as "
                                           "NV and AMD are intentionally incomparable; use "
                                           "VendorPinned<Portable, P> only for genuinely cross-vendor "
                                           "protocols.");
}

// Two protocols related in both directions describe the same set of runtime
// traces and are interchangeable in every context.
template <typename T, typename U>
inline constexpr bool equivalent_sync_v = is_subtype_sync_v<T, U> && is_subtype_sync_v<U, T>;

template <typename T, typename U>
concept EquivalentSync = equivalent_sync_v<T, U>;

// A proposed refinement that in fact produced an equivalent type reads as
// false here, which is the point: the relation rejects protocol evolutions
// that changed nothing.
template <typename T, typename U>
inline constexpr bool is_strict_subtype_sync_v = is_subtype_sync_v<T, U> && !is_subtype_sync_v<U, T>;

template <typename T, typename U>
concept StrictSubtypeSync = is_strict_subtype_sync_v<T, U>;

// Only adjacent pairs are checked.  The relation between the first and last
// element follows by transitivity of the structural rules.
namespace detail::subtype {

template <typename First, typename... Rest>
struct subtype_chain_impl : std::true_type {};

template <typename A, typename B, typename... Rest>
struct subtype_chain_impl<A, B, Rest...>
    : std::bool_constant<is_subtype_sync_v<A, B> && subtype_chain_impl<B, Rest...>::value> {};

}  // namespace detail::subtype

template <typename... Ts>
inline constexpr bool subtype_chain_v = detail::subtype::subtype_chain_impl<Ts...>::value;

// The peer of a server speaking ServerProto is the dual of ServerProto, so a
// client is compatible exactly when it is a safe substitute for that dual.
template <typename ClientProto, typename ServerProto>
concept CompatibleClient = is_subtype_sync_v<ClientProto, dual_of_t<ServerProto>>;

template <typename ServerProto, typename ClientProto>
concept CompatibleServer = is_subtype_sync_v<ServerProto, dual_of_t<ClientProto>>;

// The check is assert_subtype_sync with its arguments reversed: the older
// protocol comes first here because that reads as the version ladder, while the
// subtype relation runs from the newer protocol to the older one.
template <typename OldProto, typename NewProto>
consteval void check_protocol_evolution() noexcept {
    static_assert(is_subtype_sync_v<NewProto, OldProto>, "crucible::session::diagnostic [SubtypeMismatch]: "
                                                         "check_protocol_evolution: NewProto is not a safe refinement "
                                                         "of OldProto.  A valid refinement may: narrow a Select (pick "
                                                         "fewer branches), widen an Offer (handle more branches), or "
                                                         "restrict a payload type via is_subsort specialisation.  It "
                                                         "may NOT: add a Select branch, remove an Offer branch, change "
                                                         "Send<->Recv, swap Select<->Offer, or trigger "
                                                         "[ProtocolGradeMismatch] by weakening any ProductLattice "
                                                         "grade axis.");
}

template <typename T, typename U>
consteval void assert_equivalent_sync() noexcept {
    static_assert(equivalent_sync_v<T, U>, "crucible::session::diagnostic [SubtypeMismatch]: "
                                           "assert_equivalent_sync: T and U are not synchronously "
                                           "equivalent (not bidirectional subtypes).  Both "
                                           "is_subtype_sync_v<T, U> and is_subtype_sync_v<U, T> must hold. "
                                           "Common causes: asymmetric Select/Offer branch counts; "
                                           "differing payload types; mismatched Loop structure.");
}

template <typename ClientProto, typename ServerProto>
consteval void assert_compatible_client() noexcept {
    static_assert(CompatibleClient<ClientProto, ServerProto>,
                  "crucible::session::diagnostic [SubtypeMismatch]: "
                  "assert_compatible_client: ClientProto is not a synchronous "
                  "subtype of dual(ServerProto).  A client may safely talk to a "
                  "server only when the client's protocol is a subtype of the "
                  "server's DUAL (the server offers dual(ServerProto); the "
                  "client must be a sub-protocol of that).  Common causes: "
                  "forgot to dualise; both written from the same perspective "
                  "(e.g., both send-first); mismatched payload types; client's "
                  "Select picks branches the server's Offer does not provide.");
}

template <typename ServerProto, typename ClientProto>
consteval void assert_compatible_server() noexcept {
    static_assert(CompatibleServer<ServerProto, ClientProto>,
                  "crucible::session::diagnostic [SubtypeMismatch]: "
                  "assert_compatible_server: ServerProto is not a synchronous "
                  "subtype of dual(ClientProto).  Symmetric to "
                  "assert_compatible_client — see its diagnostic for the "
                  "structural rule.  Typically ServerProto = dual(ClientProto) "
                  "holds and this assertion is trivially true; when it fails, "
                  "one side has been refactored in a way that breaks the "
                  "symmetric sub-protocol relation.");
}

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::subtype_self_test {

static_assert(is_subtype_sync_v<End, End>);
static_assert(is_subtype_sync_v<Continue, Continue>);
static_assert(is_subtype_sync_v<Send<int, End>, Send<int, End>>);
static_assert(is_subtype_sync_v<Recv<int, End>, Recv<int, End>>);
static_assert(is_subtype_sync_v<Loop<Send<int, Continue>>, Loop<Send<int, Continue>>>);
using NvSendInt = VendorPinned<VendorBackend::NV, Send<int, End>>;
using AmdSendInt = VendorPinned<VendorBackend::AMD, Send<int, End>>;
using PortableSendInt = VendorPinned<VendorBackend::Portable, Send<int, End>>;
static_assert(is_subtype_sync_v<NvSendInt, NvSendInt>);
static_assert(is_subtype_sync_v<PortableSendInt, NvSendInt>);
static_assert(!is_subtype_sync_v<NvSendInt, AmdSendInt>);
static_assert(!is_subtype_sync_v<AmdSendInt, NvSendInt>);
static_assert(!is_subtype_sync_v<NvSendInt, PortableSendInt>);
static_assert(is_subtype_sync_v<Select<Send<int, End>, Recv<bool, End>>, Select<Send<int, End>, Recv<bool, End>>>);
static_assert(is_subtype_sync_v<Offer<Recv<int, End>, Send<bool, End>>, Offer<Recv<int, End>, Send<bool, End>>>);

static_assert(!is_subtype_sync_v<Send<int, End>, Recv<int, End>>);
static_assert(!is_subtype_sync_v<Recv<int, End>, Send<int, End>>);
static_assert(!is_subtype_sync_v<End, Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<int, End>, End>);
static_assert(!is_subtype_sync_v<Select<End>, Offer<End>>);
static_assert(!is_subtype_sync_v<Loop<Send<int, Continue>>, End>);
static_assert(!is_subtype_sync_v<End, Loop<Send<int, Continue>>>);
static_assert(!is_subtype_sync_v<Continue, End>);

struct PingReq {};
struct StopReq {};

static_assert(is_subtype_sync_v<Send<int, Select<Send<PingReq, End>>>,
                                Send<int, Select<Send<PingReq, End>, Send<StopReq, End>>>>);

static_assert(is_subtype_sync_v<Recv<int, Select<Send<PingReq, End>>>,
                                Recv<int, Select<Send<PingReq, End>, Send<StopReq, End>>>>);

static_assert(is_subtype_sync_v<Select<Send<PingReq, End>>, Select<Send<PingReq, End>, Send<StopReq, End>>>);

static_assert(is_subtype_sync_v<Select<>, Select<Send<PingReq, End>>>);

static_assert(!is_subtype_sync_v<Select<Send<PingReq, End>, Send<StopReq, End>>, Select<Send<PingReq, End>>>);

static_assert(is_subtype_sync_v<Offer<Recv<PingReq, End>, Recv<StopReq, End>, End>,
                                Offer<Recv<PingReq, End>, Recv<StopReq, End>>>);

static_assert(!is_subtype_sync_v<Offer<Recv<PingReq, End>>, Offer<Recv<PingReq, End>, Recv<StopReq, End>>>);

static_assert(!is_subtype_sync_v<Offer<>, Offer<Recv<PingReq, End>>>);

static_assert(is_subtype_sync_v<Offer<>, Offer<>>);

static_assert(is_subtype_sync_v<Loop<Send<int, Continue>>, Loop<Send<int, Continue>>>);

static_assert(is_subtype_sync_v<Loop<Select<Send<PingReq, Continue>>>,
                                Loop<Select<Send<PingReq, Continue>, Send<StopReq, End>>>>);

static_assert(is_subtype_sync_v<Continue, Continue>);

// The inner Continue binds to the innermost Loop.
static_assert(is_subtype_sync_v<Loop<Loop<Send<int, Continue>>>, Loop<Loop<Send<int, Continue>>>>);

namespace proto_evolution_example {
struct Req {};
struct Resp {};
struct CloseCmd {};

using ServerV1 =
    Loop<Offer<Recv<Req, Send<Resp, Continue>>, Recv<CloseCmd, End>, Recv<PingReq, Send<PingReq, Continue>>>>;

using ServerV2 = Loop<Offer<Recv<Req, Send<Resp, Continue>>, Recv<CloseCmd, End>,
                            Recv<PingReq, Send<PingReq, Continue>>, Recv<StopReq, Send<Resp, End>>>>;

static_assert(is_subtype_sync_v<ServerV2, ServerV1>);

static_assert(!is_subtype_sync_v<ServerV1, ServerV2>);
}  // namespace proto_evolution_example

namespace mpmc_subtype_example {
struct Job {};

// The two identical branches are deliberate: they exercise positional matching
// rather than matching by branch content.
using ProducerFull = Loop<Select<Send<Job, Continue>, Send<Job, Continue>, End>>;

using ProducerNarrow = Loop<Select<Send<Job, Continue>>>;

static_assert(is_subtype_sync_v<ProducerNarrow, ProducerFull>);
static_assert(!is_subtype_sync_v<ProducerFull, ProducerNarrow>);
}  // namespace mpmc_subtype_example

struct BaseInt {};
struct DerivedInt {};

}  // namespace detail::subtype_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace crucible::safety::proto {
template <>
struct is_subsort<detail::subtype_self_test::DerivedInt, detail::subtype_self_test::BaseInt> : std::true_type {};
}  // namespace crucible::safety::proto

namespace crucible::safety::proto::detail::subtype_self_test {

static_assert(is_subtype_sync_v<Send<DerivedInt, End>, Send<BaseInt, End>>);
static_assert(!is_subtype_sync_v<Send<BaseInt, End>, Send<DerivedInt, End>>);

static_assert(is_subtype_sync_v<Recv<BaseInt, End>, Recv<DerivedInt, End>>);
static_assert(!is_subtype_sync_v<Recv<DerivedInt, End>, Recv<BaseInt, End>>);

// The relation is contravariant under dualization: T below U implies dual(U)
// below dual(T).  The peer sees the dual, so a safe substitute for U is seen by
// the peer as something dual(U) is a safe substitute for.  The assertions below
// witness that flip on each combinator.
using DS1 = Select<Send<PingReq, End>>;
using DS2 = Select<Send<PingReq, End>, Send<StopReq, End>>;
static_assert(is_subtype_sync_v<DS1, DS2>);
static_assert(is_subtype_sync_v<dual_of_t<DS2>, dual_of_t<DS1>>);
static_assert(!is_subtype_sync_v<DS2, DS1>);
static_assert(!is_subtype_sync_v<dual_of_t<DS1>, dual_of_t<DS2>>);

using DO1 = Offer<Recv<PingReq, End>, Recv<StopReq, End>>;
using DO2 = Offer<Recv<PingReq, End>>;
static_assert(is_subtype_sync_v<DO1, DO2>);
static_assert(is_subtype_sync_v<dual_of_t<DO2>, dual_of_t<DO1>>);

static_assert(is_subtype_sync_v<Send<DerivedInt, End>, Send<BaseInt, End>>);
static_assert(is_subtype_sync_v<dual_of_t<Send<BaseInt, End>>, dual_of_t<Send<DerivedInt, End>>>);

using DLoopS1 = Loop<Send<int, Select<Send<PingReq, Continue>>>>;
using DLoopS2 = Loop<Send<int, Select<Send<PingReq, Continue>, Send<StopReq, End>>>>;
static_assert(is_subtype_sync_v<DLoopS1, DLoopS2>);
static_assert(is_subtype_sync_v<dual_of_t<DLoopS2>, dual_of_t<DLoopS1>>);

using TSelectT = Select<Send<PingReq, End>>;
using TSelectU = Select<Send<PingReq, End>, Send<StopReq, End>>;
using TSelectV = Select<Send<PingReq, End>, Send<StopReq, End>, Recv<PingReq, End>>;
static_assert(is_subtype_sync_v<TSelectT, TSelectU>);
static_assert(is_subtype_sync_v<TSelectU, TSelectV>);
static_assert(is_subtype_sync_v<TSelectT, TSelectV>);

using TOfferW = Offer<Recv<PingReq, End>, Recv<StopReq, End>, Send<PingReq, End>>;
using TOfferX = Offer<Recv<PingReq, End>, Recv<StopReq, End>>;
using TOfferY = Offer<Recv<PingReq, End>>;
static_assert(is_subtype_sync_v<TOfferW, TOfferX>);
static_assert(is_subtype_sync_v<TOfferX, TOfferY>);
static_assert(is_subtype_sync_v<TOfferW, TOfferY>);

// Loop<X> and X stay unrelated in both directions.  Relating them would need
// coercion rules, which this relation deliberately does not provide.
static_assert(!is_subtype_sync_v<Loop<End>, End>);
static_assert(!is_subtype_sync_v<End, Loop<End>>);
static_assert(!is_subtype_sync_v<Loop<Send<int, Continue>>, Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<int, End>, Loop<Send<int, Continue>>>);

consteval bool check_assert_subtype_sync() {
    assert_subtype_sync<DS1, DS2>();
    return true;
}
static_assert(check_assert_subtype_sync());

template <typename T, typename U>
    requires SubtypeSync<T, U>
consteval bool requires_subtype() {
    return true;
}

static_assert(requires_subtype<DS1, DS2>());

static_assert(!is_strict_subtype_sync_v<End, End>);
static_assert(!is_strict_subtype_sync_v<Send<int, End>, Send<int, End>>);
static_assert(!is_strict_subtype_sync_v<DS1, DS1>);

static_assert(is_strict_subtype_sync_v<DS1, DS2>);
static_assert(!is_strict_subtype_sync_v<DS2, DS1>);

static_assert(is_strict_subtype_sync_v<DO1, DO2>);
static_assert(!is_strict_subtype_sync_v<DO2, DO1>);

static_assert(!is_strict_subtype_sync_v<Send<int, End>, Recv<int, End>>);
static_assert(!is_strict_subtype_sync_v<Recv<int, End>, Send<int, End>>);

template <typename T, typename U>
    requires StrictSubtypeSync<T, U>
consteval bool requires_strict_subtype() {
    return true;
}

static_assert(requires_strict_subtype<DS1, DS2>());

static_assert(equivalent_sync_v<End, End>);
static_assert(equivalent_sync_v<Send<int, End>, Send<int, End>>);
static_assert(equivalent_sync_v<DS1, DS1>);

static_assert(!equivalent_sync_v<DS1, DS2>);
static_assert(!equivalent_sync_v<DO1, DO2>);

static_assert(subtype_chain_v<TSelectT, TSelectU, TSelectV>);
static_assert(subtype_chain_v<TOfferW, TOfferX, TOfferY>);
static_assert(subtype_chain_v<End>);
static_assert(subtype_chain_v<End, End>);

static_assert(!subtype_chain_v<TSelectU, TSelectT, TSelectV>);

namespace client_server_test {
struct Query {};
struct Reply {};
using ReqRespClient = Loop<Send<Query, Recv<Reply, Continue>>>;
using ReqRespServer = Loop<Recv<Query, Send<Reply, Continue>>>;

static_assert(std::is_same_v<dual_of_t<ReqRespServer>, ReqRespClient>);

static_assert(CompatibleClient<ReqRespClient, ReqRespServer>);
static_assert(CompatibleServer<ReqRespServer, ReqRespClient>);

static_assert(!CompatibleClient<ReqRespServer, ReqRespServer>);
static_assert(!CompatibleServer<ReqRespClient, ReqRespClient>);
}  // namespace client_server_test

consteval bool check_additional_asserts() {
    assert_equivalent_sync<DS1, DS1>();
    assert_compatible_client<client_server_test::ReqRespClient, client_server_test::ReqRespServer>();
    assert_compatible_server<client_server_test::ReqRespServer, client_server_test::ReqRespClient>();
    return true;
}
static_assert(check_additional_asserts());

}  // namespace crucible::safety::proto::detail::subtype_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS
