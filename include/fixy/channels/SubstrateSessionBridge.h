#pragma once

// The session that a channel handle runs.  handle_for names the handle of
// each role of a channel, and default_proto_for_t names the protocol of that
// role.  mint_substrate_session takes the handle by move and gives the
// first session handle, so the session owns the channel handle for its
// whole life.
//
// The table holds each form of the permissioned channel of
// fixy/concurrent/PermissionedChannel.h.
//
// fixy/session/Admission.h refuses a raw pointer to a channel handle as a
// session resource, because the handle can move or die first.  So the
// session takes the handle by move.  The gate does not ask that the
// footprint of one call fits a residency tier, because a context carries
// no residency tier (foundation/effects/Ctx.h).

#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::channels {

enum class Direction : std::uint8_t {
    Producer = 0,
    Consumer = 1,
};

namespace detail {

// The channels of the table.  The set is read by reflection off the one
// template, so no other type joins it, and a cv-qualified channel is not a
// channel of the table.
template <class Substr>
concept IsBridgedChannel = std::same_as<Substr, std::remove_cvref_t<Substr>>
                        && ::foundation::reflect::IsInstanceOf<Substr, ^^::fixy::concurrent::PermissionedChannel>;

}  // namespace detail

// Each form names its handles ProducerHandle and ConsumerHandle, so one rule
// gives the handle of each role of each form.
template <class Substr, Direction Dir>
struct handle_for;

template <class Substr, Direction Dir>
    requires detail::IsBridgedChannel<Substr>
struct handle_for<Substr, Dir> {
    using type = std::conditional_t<Dir == Direction::Producer, typename Substr::ProducerHandle,
                                    typename Substr::ConsumerHandle>;
};

template <class Substr, Direction Dir>
using handle_for_t = typename handle_for<Substr, Dir>::type;

// A loop with no exit branch is the deliberate spelling of a stream that
// runs until it is torn down.  Shutdown is a detach carrying a typed
// reason, detach_reason::InfiniteLoopProtocol, not a branch the protocol
// offers.  The detach releases the channel handle that the session owns.
//
// The protocol of a channel is one value for the channel: the producer end
// sends each value, and the consumer end runs the dual of the producer end.
// So the two ends of one channel run dual protocols, although each mint
// gives one end.  The protocols are aliases, and no unit can specialize an
// alias, so no unit can give one end a protocol of its own.

template <class Substr>
    requires detail::IsBridgedChannel<Substr>
using producer_proto_t =
    ::fixy::session::Loop<::fixy::session::Send<typename Substr::value_type, ::fixy::session::Continue>>;

template <class Substr, Direction Dir>
    requires detail::IsBridgedChannel<Substr>
using default_proto_for_t = std::conditional_t<Dir == Direction::Producer, producer_proto_t<Substr>,
                                               ::fixy::session::dual_of_t<producer_proto_t<Substr>>>;

namespace detail {

template <class Substr, Direction Dir>
concept HasHandleFor = requires { typename handle_for<Substr, Dir>::type; };

template <class Substr, Direction Dir>
concept HasDefaultProtoFor = requires { typename default_proto_for_t<Substr, Dir>; };

// The handle of a direction has the pole shape of that direction, so a
// stage can take it.  fixy/concurrent/StageShape.h reads the same shape.
template <class Substr, Direction Dir>
concept HandleHasPoleOf =
    (Dir == Direction::Producer && ::fixy::concurrent::is_producer_handle_v<handle_for_t<Substr, Dir>>)
    || (Dir == Direction::Consumer && ::fixy::concurrent::is_consumer_handle_v<handle_for_t<Substr, Dir>>);

}  // namespace detail

// A direction is bridgeable when the table names both its handle and its
// protocol, and the handle has the pole shape of the direction.  Only the
// two channels have a row, so the concept is closed over them.
template <class Substr, Direction Dir>
concept IsBridgeableDirection = detail::HasHandleFor<Substr, Dir> && detail::HasDefaultProtoFor<Substr, Dir>
                             && detail::HandleHasPoleOf<Substr, Dir>;

// The pole table of the bridge (fixy/session/Admission.h, section The pole
// table of a channel): a handle of a bridged channel runs the protocol of
// its direction, and each handle of the other direction runs the protocol of
// that direction.  The producer writes with try_push and the consumer reads
// with try_pop.  The pop of a merge takes the bare tag of Nondet from the
// context of the step, so that context holds Nondet.
template <class Pole>
    requires detail::IsBridgedChannel<typename Pole::channel_type>
          && (std::same_as<Pole, typename Pole::channel_type::ProducerHandle>
              || std::same_as<Pole, typename Pole::channel_type::ConsumerHandle>)
struct bridged_pole_protocols {
    using value_type = typename Pole::channel_type::value_type;
    static constexpr Direction direction =
        std::same_as<Pole, typename Pole::channel_type::ProducerHandle> ? Direction::Producer : Direction::Consumer;
    static constexpr Direction other_direction =
        direction == Direction::Producer ? Direction::Consumer : Direction::Producer;
    using self_protocol = default_proto_for_t<typename Pole::channel_type, direction>;
    using peer_protocol = default_proto_for_t<typename Pole::channel_type, other_direction>;

    template <::foundation::effects::IsExecCtx Ctx>
        requires(direction == Direction::Producer)
    [[nodiscard]] static constexpr bool write(Ctx const& /*ctx*/, Pole& pole, value_type& value) noexcept {
        return pole.try_push(value);
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires(direction == Direction::Consumer)
             && (!::fixy::concurrent::is_merge_consumer_handle_v<Pole>
                 || ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>)
    [[nodiscard]] static constexpr ::fixy::Option<value_type> read([[maybe_unused]] Ctx const& ctx,
                                                                   Pole& pole) noexcept {
        if constexpr (::fixy::concurrent::is_merge_consumer_handle_v<Pole>) {
            return pole.try_pop(
                ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx));
        } else {
            return pole.try_pop();
        }
    }
};

// The gate of mint_substrate_session: a bridgeable direction, and the gate
// of the pole door of fixy/session/Handle.h for its handle.  So the context
// admits each effect that a payload of the protocol carries.  The pop of a
// merge pole takes the bare tag of Nondet, so the context of such a pole
// holds Nondet.
template <class Substr, Direction Dir, class Ctx>
concept CtxFitsSubstrateSessionMint =
    ::foundation::effects::IsExecCtx<Ctx> && IsBridgeableDirection<Substr, Dir>
    && ::fixy::session::CtxFitsPoleSession<Ctx, handle_for_t<Substr, Dir>>
    && (!::fixy::concurrent::is_merge_consumer_handle_v<handle_for_t<Substr, Dir>>
        || ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>);

// The handle comes in by move.  The caller keeps only a moved-from handle,
// which reaches no channel, so the session and the caller never act on the
// channel at the same time.  The pole door takes the protocol of the handle
// from the table above.
template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSubstrateSessionMint<Substr, Dir, Ctx>
[[nodiscard]] constexpr auto mint_substrate_session(Ctx const& ctx, handle_for_t<Substr, Dir>&& handle) noexcept {
    return ::fixy::session::SessionMintDoor::open_pole<::fixy::session::DefaultAbandonmentPolicy>(ctx,
                                                                                                  std::move(handle));
}

}  // namespace fixy::channels

// The family of the bridge in the root of the pole tables.  The
// registration stands in the file of its template.
namespace fixy::session::pole_tables::bridge {

inline constexpr ::fixy::session::pole_table permissioned_channel{.table = ^^::fixy::channels::bridged_pole_protocols};

inline constexpr ::fixy::session::transition::seal table_seal{.kind = ^^::fixy::session::pole_table, .count = 1};

}  // namespace fixy::session::pole_tables::bridge
