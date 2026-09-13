#pragma once

// A payload marked this way may be delivered without crossing the wire
// at all, when the recipient already holds a copy addressed by the same
// content.  What the recipient ends up in depends only on the content,
// so which of the two routes delivered it is invisible to the protocol.
//
// Deciding whether a copy is already held is a question for whatever
// moves the bytes.  The type says only that the answer cannot change
// the protocol's outcome, which is what makes the elision safe.
//
// The marked and unmarked payload types are therefore subsorts of each
// other.  That is unusual: every other payload relation here runs one
// way, since it records a guarantee one side has and the other does
// not.  Here neither side gains or loses anything, so a protocol that
// takes up the marking stays equivalent to the one that did not.

#include <crucible/Platform.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstddef>
#include <type_traits>

namespace crucible::safety::proto {

template <typename T>
struct ContentAddressed {
    using wrapped = T;
};

template <typename T>
struct is_content_addressed : std::false_type {};

template <typename T>
struct is_content_addressed<ContentAddressed<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_content_addressed_v = is_content_addressed<T>::value;

template <typename T>
concept ContentAddressedType = is_content_addressed_v<T>;

template <typename T>
struct content_addressed_underlying {
    using type = T;
};

template <typename T>
struct content_addressed_underlying<ContentAddressed<T>> {
    using type = T;
};

template <typename T>
using content_addressed_underlying_t = typename content_addressed_underlying<T>::type;

template <typename T>
struct unwrap_content_addressed {
    using type = T;
};

template <typename T>
struct unwrap_content_addressed<ContentAddressed<T>> : unwrap_content_addressed<T> {};

template <typename T>
using unwrap_content_addressed_t = typename unwrap_content_addressed<T>::type;

namespace detail::ca {

template <typename T>
struct depth : std::integral_constant<std::size_t, 0> {};

template <typename T>
struct depth<ContentAddressed<T>> : std::integral_constant<std::size_t, 1 + depth<T>::value> {};

}  // namespace detail::ca

template <typename T>
inline constexpr std::size_t content_addressed_depth_v = detail::ca::depth<T>::value;

template <typename T>
struct is_subsort<ContentAddressed<T>, T> : std::true_type {};

template <typename T>
struct is_subsort<T, ContentAddressed<T>> : std::true_type {};

// The pair of rules above stops one layer deep, and the relation is
// not closed under composition, so nesting the marker would otherwise
// sever the payload from the type it wraps.  Nesting changes nothing
// about content, so the two rules below reach through any number of
// layers by stripping them all.
//
// They sit between the two rules above and the reflexive fall-through:
// looser than the literal one-layer rules, which therefore still win
// where they apply, and tighter than the unconstrained fall-through.
// Requiring the two sides to differ keeps a type from matching against
// itself here.

template <typename T, typename U>
    requires(is_content_addressed_v<T> && std::is_same_v<unwrap_content_addressed_t<T>, U> && !std::is_same_v<T, U>)
struct is_subsort<T, U> : std::true_type {};

template <typename T, typename U>
    requires(is_content_addressed_v<U> && std::is_same_v<T, unwrap_content_addressed_t<U>> && !std::is_same_v<T, U>)
struct is_subsort<T, U> : std::true_type {};

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::ca::ca_self_test {

struct Msg {};
struct Ack {};

static_assert(is_content_addressed_v<ContentAddressed<Msg>>);
static_assert(!is_content_addressed_v<Msg>);
static_assert(!is_content_addressed_v<int>);
static_assert(!is_content_addressed_v<End>);

template <ContentAddressedType T>
consteval bool requires_content_addressed() {
    return true;
}
static_assert(requires_content_addressed<ContentAddressed<Msg>>());

static_assert(std::is_same_v<content_addressed_underlying_t<ContentAddressed<Msg>>, Msg>);
static_assert(std::is_same_v<content_addressed_underlying_t<Msg>, Msg>);
static_assert(std::is_same_v<content_addressed_underlying_t<int>, int>);

static_assert(
    std::is_same_v<content_addressed_underlying_t<ContentAddressed<ContentAddressed<Msg>>>, ContentAddressed<Msg>>);

static_assert(std::is_same_v<unwrap_content_addressed_t<Msg>, Msg>);
static_assert(std::is_same_v<unwrap_content_addressed_t<ContentAddressed<Msg>>, Msg>);
static_assert(std::is_same_v<unwrap_content_addressed_t<ContentAddressed<ContentAddressed<Msg>>>, Msg>);
static_assert(
    std::is_same_v<unwrap_content_addressed_t<ContentAddressed<ContentAddressed<ContentAddressed<Msg>>>>, Msg>);

static_assert(content_addressed_depth_v<Msg> == 0);
static_assert(content_addressed_depth_v<int> == 0);
static_assert(content_addressed_depth_v<ContentAddressed<Msg>> == 1);
static_assert(content_addressed_depth_v<ContentAddressed<ContentAddressed<Msg>>> == 2);
static_assert(content_addressed_depth_v<ContentAddressed<ContentAddressed<ContentAddressed<Msg>>>> == 3);

static_assert(is_subsort_v<ContentAddressed<Msg>, Msg>);
static_assert(is_subsort_v<Msg, ContentAddressed<Msg>>);

static_assert(is_subsort_v<ContentAddressed<Msg>, ContentAddressed<Msg>>);
static_assert(is_subsort_v<Msg, Msg>);

static_assert(!is_subsort_v<ContentAddressed<Msg>, Ack>);
static_assert(!is_subsort_v<Msg, Ack>);
static_assert(!is_subsort_v<ContentAddressed<Msg>, ContentAddressed<Ack>>);

using CaCa = ContentAddressed<ContentAddressed<Msg>>;
using CaCaCa = ContentAddressed<ContentAddressed<ContentAddressed<Msg>>>;

static_assert(is_subsort_v<CaCa, Msg>);
static_assert(is_subsort_v<Msg, CaCa>);

static_assert(is_subsort_v<CaCaCa, Msg>);
static_assert(is_subsort_v<Msg, CaCaCa>);

using CaDepth5 = ContentAddressed<ContentAddressed<ContentAddressed<ContentAddressed<ContentAddressed<Msg>>>>>;
static_assert(is_subsort_v<CaDepth5, Msg>);
static_assert(is_subsort_v<Msg, CaDepth5>);

static_assert(is_subsort_v<CaCa, CaCa>);
static_assert(is_subsort_v<CaCaCa, CaCaCa>);
static_assert(is_subsort_v<CaDepth5, CaDepth5>);

// Depths one apart are related by the one-layer rules, taking the
// inner nest as the payload.

static_assert(is_subsort_v<CaCaCa, CaCa>);
static_assert(is_subsort_v<CaCa, CaCaCa>);

// Depths two apart are related by no rule here, and the relation does
// not compose on its own.  A protocol still crosses that gap, by
// relating each side to the unwrapped payload and meeting there.

using CaDepth4 = ContentAddressed<ContentAddressed<ContentAddressed<ContentAddressed<Msg>>>>;
static_assert(!is_subsort_v<CaDepth4, CaCa>);
static_assert(!is_subsort_v<CaCa, CaDepth4>);

static_assert(!is_subsort_v<CaCa, Ack>);
static_assert(!is_subsort_v<Ack, CaCa>);
static_assert(!is_subsort_v<CaDepth5, Ack>);

static_assert(!is_subsort_v<ContentAddressed<ContentAddressed<Msg>>, ContentAddressed<ContentAddressed<Ack>>>);

static_assert(is_subtype_sync_v<Send<ContentAddressed<Msg>, End>, Send<Msg, End>>);
static_assert(is_subtype_sync_v<Send<Msg, End>, Send<ContentAddressed<Msg>, End>>);

static_assert(equivalent_sync_v<Send<ContentAddressed<Msg>, End>, Send<Msg, End>>);

// A one-way payload relation would give only one direction on a
// receive.  This one being two-way, both directions survive.
static_assert(is_subtype_sync_v<Recv<ContentAddressed<Msg>, End>, Recv<Msg, End>>);
static_assert(is_subtype_sync_v<Recv<Msg, End>, Recv<ContentAddressed<Msg>, End>>);

static_assert(equivalent_sync_v<Recv<ContentAddressed<Msg>, End>, Recv<Msg, End>>);

static_assert(is_subtype_sync_v<Send<ContentAddressed<ContentAddressed<Msg>>, End>, Send<Msg, End>>);
static_assert(is_subtype_sync_v<Send<Msg, End>, Send<ContentAddressed<ContentAddressed<Msg>>, End>>);

static_assert(equivalent_sync_v<Send<ContentAddressed<ContentAddressed<Msg>>, End>, Send<Msg, End>>);

static_assert(is_subtype_sync_v<Recv<ContentAddressed<ContentAddressed<Msg>>, End>, Recv<Msg, End>>);
static_assert(is_subtype_sync_v<Recv<Msg, End>, Recv<ContentAddressed<ContentAddressed<Msg>>, End>>);

using CaLoopSend = Loop<Send<ContentAddressed<Msg>, Continue>>;
using RawLoopSend = Loop<Send<Msg, Continue>>;

static_assert(is_subtype_sync_v<CaLoopSend, RawLoopSend>);
static_assert(is_subtype_sync_v<RawLoopSend, CaLoopSend>);
static_assert(equivalent_sync_v<CaLoopSend, RawLoopSend>);

using SelectMixed = Select<Send<ContentAddressed<Msg>, End>, Send<Ack, End>>;
using SelectRaw = Select<Send<Msg, End>, Send<Ack, End>>;

static_assert(is_subtype_sync_v<SelectMixed, SelectRaw>);
static_assert(is_subtype_sync_v<SelectRaw, SelectMixed>);

// The marker is a payload, not a combinator, so duality leaves it
// where it is.  Both peers see the payload marked, or neither does.

using CaProto = Send<ContentAddressed<Msg>, End>;
using CaProtoDual = dual_of_t<CaProto>;
static_assert(std::is_same_v<CaProtoDual, Recv<ContentAddressed<Msg>, End>>);

static_assert(std::is_same_v<dual_of_t<CaProtoDual>, CaProto>);

static_assert(is_well_formed_v<Send<ContentAddressed<Msg>, End>>);
static_assert(is_well_formed_v<Loop<Send<ContentAddressed<Msg>, Continue>>>);
static_assert(is_well_formed_v<Recv<ContentAddressed<Ack>, End>>);

// The rules here relate a payload to its own marked form and nothing
// else.  A separately declared relation between two payload types does
// not reach across the marker on its own, which is deliberate: that
// would be composing two relations the code never claimed compose.

struct CipherSnapshot {};
using CipherPublisher_CA = Loop<Send<ContentAddressed<CipherSnapshot>, Continue>>;
using CipherSubscriber_CA = dual_of_t<CipherPublisher_CA>;

static_assert(std::is_same_v<CipherSubscriber_CA, Loop<Recv<ContentAddressed<CipherSnapshot>, Continue>>>);
static_assert(is_well_formed_v<CipherPublisher_CA>);
static_assert(is_well_formed_v<CipherSubscriber_CA>);

using CipherPublisher_Raw = Loop<Send<CipherSnapshot, Continue>>;
static_assert(equivalent_sync_v<CipherPublisher_CA, CipherPublisher_Raw>);

}  // namespace detail::ca::ca_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
