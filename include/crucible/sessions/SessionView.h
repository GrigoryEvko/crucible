#pragma once

// Looking at where a handle sits in its protocol, without consuming or
// advancing it.
//
// A handle's operations all take it by rvalue, since taking a step ends
// the old position.  Inspection is not a step, and the two ways to do
// it without this header both cost something.  Moving the handle out
// and back consumes it, which is the opposite of inspecting.  Matching
// on the protocol type at the call site works but spreads knowledge of
// the protocol's shape into every caller that only wanted to look.
//
// A view instead borrows the handle and carries a tag naming the
// position.  The position is already fixed by the handle's type, so the
// tag costs nothing and a request for the wrong position fails to
// compile.
//
// One tag per kind of position, plus one covering the two terminal
// positions together, for callers that care only that the handle has
// reached somewhere it can safely be dropped.
//
// A view is a borrow and outlives nothing.  Several may look at one
// handle at once, since none of them can change it.  What they do not
// survive is the handle itself being consumed: mint a view, move from
// the handle, and the view is left pointing at nothing.  The attribute
// on the factory catches a view returned from the function that made
// it.  Anything further apart is the caller's to keep straight.

#include <crucible/safety/ScopedView.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

struct AtSend {};
struct AtRecv {};
struct AtSelect {};
struct AtOffer {};
struct AtEnd {};
struct AtStop {};
struct AtTerminal {};
struct AtCheckpointed {};
struct AtDelegate {};
struct AtAccept {};

template <typename Handle, typename Tag>
struct handle_is_at : std::false_type {};

template <typename T, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Send<T, R>, Resource, LoopCtx>, AtSend> : std::true_type {};

template <typename T, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Recv<T, R>, Resource, LoopCtx>, AtRecv> : std::true_type {};

template <typename... Bs, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Select<Bs...>, Resource, LoopCtx>, AtSelect> : std::true_type {};

template <typename... Bs, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Offer<Bs...>, Resource, LoopCtx>, AtOffer> : std::true_type {};

template <typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<End, Resource, LoopCtx>, AtEnd> : std::true_type {};

template <CrashClass C, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Stop_g<C>, Resource, LoopCtx>, AtStop> : std::true_type {};

// A finished protocol and a crashed one are both positions a handle
// may be dropped from, which is the only distinction this tag draws.

template <typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<End, Resource, LoopCtx>, AtTerminal> : std::true_type {};

template <CrashClass C, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Stop_g<C>, Resource, LoopCtx>, AtTerminal> : std::true_type {};

template <typename B, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<CheckpointedSession<B, R>, Resource, LoopCtx>, AtCheckpointed> : std::true_type {};

template <typename T, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Delegate<T, R>, Resource, LoopCtx>, AtDelegate> : std::true_type {};

template <typename T, typename R, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename Resource,
          typename LoopCtx>
struct handle_is_at<SessionHandle<EpochedDelegate<T, R, MinEpoch, MinGeneration>, Resource, LoopCtx>, AtDelegate>
    : std::true_type {};

template <typename T, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Accept<T, R>, Resource, LoopCtx>, AtAccept> : std::true_type {};

template <typename T, typename R, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename Resource,
          typename LoopCtx>
struct handle_is_at<SessionHandle<EpochedAccept<T, R, MinEpoch, MinGeneration>, Resource, LoopCtx>, AtAccept>
    : std::true_type {};

template <typename Handle, typename Tag>
inline constexpr bool handle_is_at_v = handle_is_at<Handle, Tag>::value;

template <typename Handle, typename Tag>
concept HandleIsAt = handle_is_at_v<Handle, Tag>;

// The generic view machinery asks this as a runtime precondition, but
// a handle's position is settled by its type, so the answer is already
// a constant and the check is trivially true or trivially false.  The
// rejection that matters happens a layer up, in the factory's
// requires-clause.

template <typename Proto, typename Resource, typename LoopCtx, typename Tag>
constexpr bool view_ok(SessionHandle<Proto, Resource, LoopCtx> const& /*h*/, std::type_identity<Tag>) noexcept {
    return handle_is_at_v<SessionHandle<Proto, Resource, LoopCtx>, Tag>;
}

template <typename Tag, typename Handle>
    requires HandleIsAt<Handle, Tag>
[[nodiscard]] constexpr auto mint_session_view(Handle const& handle CRUCIBLE_LIFETIMEBOUND) noexcept
    -> safety::ScopedView<Handle, Tag> {
    return safety::mint_view<Tag>(handle);
}

// The rendered name behind this carries the same caveat the underlying
// accessor does: it is a runtime helper, not something to capture as a
// constant, because its value is not stable across translation units.
template <typename View>
[[nodiscard]] constexpr std::string_view session_view_protocol_name() noexcept {
    using Handle = typename View::carrier_type;
    return Handle::protocol_name();
}

template <typename View>
struct session_view_message_type;

template <typename T, typename R, typename Resource, typename LoopCtx>
struct session_view_message_type<safety::ScopedView<SessionHandle<Send<T, R>, Resource, LoopCtx>, AtSend>> {
    using type = T;
};

template <typename T, typename R, typename Resource, typename LoopCtx>
struct session_view_message_type<safety::ScopedView<SessionHandle<Recv<T, R>, Resource, LoopCtx>, AtRecv>> {
    using type = T;
};

template <typename View>
using session_view_message_type_t = typename session_view_message_type<View>::type;

template <typename View>
struct session_view_branch_count;

template <typename... Bs, typename Resource, typename LoopCtx>
struct session_view_branch_count<safety::ScopedView<SessionHandle<Select<Bs...>, Resource, LoopCtx>, AtSelect>>
    : std::integral_constant<std::size_t, sizeof...(Bs)> {};

template <typename... Bs, typename Resource, typename LoopCtx>
struct session_view_branch_count<safety::ScopedView<SessionHandle<Offer<Bs...>, Resource, LoopCtx>, AtOffer>>
    : std::integral_constant<std::size_t, sizeof...(Bs)> {};

template <typename View>
inline constexpr std::size_t session_view_branch_count_v = session_view_branch_count<View>::value;

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::sv_self_test {

struct FakeRes {};
struct Msg {};
struct Other {};

static_assert(handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>);
static_assert(handle_is_at_v<SessionHandle<Recv<Msg, End>, FakeRes>, AtRecv>);
static_assert(handle_is_at_v<SessionHandle<Select<Send<Msg, End>>, FakeRes>, AtSelect>);
static_assert(handle_is_at_v<SessionHandle<Offer<Recv<Msg, End>>, FakeRes>, AtOffer>);
static_assert(handle_is_at_v<SessionHandle<End, FakeRes>, AtEnd>);
static_assert(handle_is_at_v<SessionHandle<Stop, FakeRes>, AtStop>);
static_assert(handle_is_at_v<SessionHandle<Stop_g<CrashClass::NoThrow>, FakeRes>, AtStop>);
static_assert(handle_is_at_v<SessionHandle<End, FakeRes>, AtTerminal>);
static_assert(handle_is_at_v<SessionHandle<Stop, FakeRes>, AtTerminal>);
static_assert(handle_is_at_v<SessionHandle<Stop_g<CrashClass::NoThrow>, FakeRes>, AtTerminal>);
static_assert(handle_is_at_v<SessionHandle<Delegate<Send<Msg, End>, End>, FakeRes>, AtDelegate>);
static_assert(handle_is_at_v<SessionHandle<Accept<Send<Msg, End>, End>, FakeRes>, AtAccept>);
static_assert(handle_is_at_v<SessionHandle<CheckpointedSession<End, End>, FakeRes>, AtCheckpointed>);

static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtRecv>);
static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtSelect>);
static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtTerminal>);
static_assert(!handle_is_at_v<SessionHandle<End, FakeRes>, AtSend>);
static_assert(!handle_is_at_v<SessionHandle<End, FakeRes>, AtStop>);
static_assert(!handle_is_at_v<SessionHandle<Stop, FakeRes>, AtEnd>);
static_assert(!handle_is_at_v<SessionHandle<Recv<Msg, End>, FakeRes>, AtCheckpointed>);
static_assert(!handle_is_at_v<SessionHandle<Offer<Recv<Msg, End>>, FakeRes>, AtSelect>);
static_assert(!handle_is_at_v<SessionHandle<Select<Send<Msg, End>>, FakeRes>, AtOffer>);

static_assert(!handle_is_at_v<int, AtSend>);
static_assert(!handle_is_at_v<FakeRes, AtTerminal>);
static_assert(!handle_is_at_v<Send<Msg, End>, AtSend>);

// There are deliberately no sample handles at namespace scope here.
// Marking one constexpr fixes its initializer and not its destructor,
// so a sample left at a mid-protocol position would run the
// abandonment check when the program exits.  Exercising the runtime
// path needs handles on a stack, consumed before the scope closes.

template <typename H, typename Tag>
concept can_mint_session_view = requires(H const& h) { mint_session_view<Tag>(h); };

static_assert(can_mint_session_view<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>);
static_assert(!can_mint_session_view<SessionHandle<Send<Msg, End>, FakeRes>, AtRecv>);
static_assert(!can_mint_session_view<SessionHandle<End, FakeRes>, AtSend>);
static_assert(can_mint_session_view<SessionHandle<End, FakeRes>, AtTerminal>);

}  // namespace detail::sv_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
