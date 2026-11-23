#pragma once

// Binding a channel handle to an execution context checks the fit between
// the two once, at construction.  Every send and receive afterwards runs
// with no further check.
//
// The endpoint owns its handle.  The handle is a frame handle of a channel
// (the frame doors of fixy/concurrent/PermissionedChannel.h), and its type
// carries the scope brand of the frame, so the endpoint ends inside the
// frame and the channel outlives it.  The handle releases its permission
// when the endpoint ends.
//
// Design notes:
//   - A handle of fixy/concurrent is move-constructible, and its move
//     clears the binding.  So the endpoint takes the handle by move and
//     owns it, and no handle that the caller keeps acts on the channel
//     beside the endpoint.
//   - The gate is the gate of the session that the endpoint can start.  A
//     context carries no residency tier and no workload hint
//     (foundation/effects/Ctx.h), so the gate checks neither.
//   - The endpoint keeps a copy of the minting context, because a context
//     has no public default constructor.

#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/Handle.h>

#include <foundation/NoObject.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Ctx.h>

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace fixy::channels {

// The class template, the endpoint door and the mint all spell this one
// gate, so they cannot drift apart.  An endpoint keeps a copy of its
// context, so the context must be copyable.  A branded context is not.
template <class Substr, Direction Dir, class Ctx>
concept CtxFitsEndpointMint = CtxFitsSubstrateSessionMint<Substr, Dir, Ctx> && std::copy_constructible<Ctx>;

// The gate of an endpoint over one handle: the gate of the direction, and a
// frame handle of the end of the direction (the frame doors of
// fixy/concurrent/PermissionedChannel.h).  The handle carries the scope brand
// of its frame, so the endpoint ends inside that frame.
template <class Substr, Direction Dir, class Ctx, class Handle>
concept CtxFitsEndpointHandle = CtxFitsEndpointMint<Substr, Dir, Ctx> && FrameHandleOfDirection<Handle, Substr, Dir>;

class EndpointDoor;

template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx, class Handle>
    requires CtxFitsEndpointHandle<Substr, Dir, Ctx, Handle>
class [[nodiscard]] Endpoint {
public:
    using substrate_type = Substr;
    static constexpr Direction direction = Dir;
    using ctx_type = Ctx;
    using handle_type = Handle;
    using value_type = typename Substr::value_type;
    using user_tag = typename Substr::user_tag;
    using proto_type = default_proto_for_t<Substr, Dir>;

    static constexpr std::size_t per_call_working_set = handle_type::per_call_working_set;

private:
    handle_type handle_;

    [[no_unique_address]] Ctx ctx_;

    friend class EndpointDoor;

    constexpr Endpoint(Ctx const& ctx, handle_type&& handle) noexcept : handle_{std::move(handle)}, ctx_{ctx} {}

public:
    Endpoint(Endpoint const&) = delete("Endpoint owns the typed view of a linear handle — copy would "
                                       "duplicate the producer/consumer Permission's typed projection.  "
                                       "Use std::move to transfer; or mint a second Endpoint if the "
                                       "underlying substrate is multi-producer/multi-consumer.");
    Endpoint& operator=(Endpoint const&) = delete("Endpoint owns the typed view of a linear handle.");

    // The move of the handle clears its binding, so a moved-from endpoint
    // reaches no channel.  The handle binds to one channel for life and
    // refuses move-assignment, so the endpoint refuses it too.
    constexpr Endpoint(Endpoint&&) noexcept = default;
    Endpoint& operator=(Endpoint&&) = delete("Endpoint binds to ONE channel for life, as its handle does");
    ~Endpoint() = default;

    template <class T = value_type>
        requires(Dir == Direction::Producer) && std::same_as<std::remove_cvref_t<T>, value_type>
    [[nodiscard, gnu::hot]] bool try_send(T const& v) noexcept {
        return handle_.try_push(v);
    }

    // The Option<value_type> of the pop of the handle.  The type is deduced,
    // so the definition of the class names no Option, and a gate that reads
    // the class does not read the open niche of the payload.  The pop of a
    // merge pole takes the bare tag of Nondet from the context of the
    // endpoint, and the gate of the mint made sure that the row holds it.
    template <Direction D = Dir>
        requires(D == Direction::Consumer)
    [[nodiscard, gnu::hot]] auto try_recv() noexcept {
        if constexpr (::fixy::concurrent::is_merge_consumer_handle_v<handle_type>) {
            return handle_.try_pop(
                ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx_));
        } else {
            return handle_.try_pop();
        }
    }

    // A snapshot of the channel.  The schedule decides its value, so the
    // snapshot takes the bare tag of Nondet from the context of the
    // endpoint, and only a context whose row holds Nondet reads one.
    [[nodiscard]] bool empty_approx() const noexcept
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>
              && requires(handle_type const& h, ::foundation::effects::cap::Nondet nondet) { h.empty_approx(nondet); }
    {
        return handle_.empty_approx(
            ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx_));
    }
    [[nodiscard]] std::size_t size_approx() const noexcept
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>
              && requires(handle_type const& h, ::foundation::effects::cap::Nondet nondet) { h.size_approx(nondet); }
    {
        return handle_.size_approx(
            ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx_));
    }

    // The session carries an empty permission set.  The handle's own
    // permission already fixes how many producers or consumers there may
    // be, so the protocol has nothing further to transfer per step.  The
    // session opens in the context of the endpoint, so the gate of the
    // session mint admits each payload of the protocol in that context.

    [[nodiscard]] constexpr auto into_session() && noexcept {
        return mint_substrate_session<Substr, Dir>(ctx_, std::move(handle_));
    }

    [[nodiscard]] constexpr handle_type& handle() & noexcept { return handle_; }
    [[nodiscard]] constexpr handle_type const& handle() const& noexcept { return handle_; }

    // The returned handle is the only one that holds the permission, and
    // the endpoint that gave it reaches no channel afterwards.

    [[nodiscard]] constexpr handle_type into_handle() && noexcept { return std::move(handle_); }

    [[nodiscard]] constexpr Ctx ctx() const noexcept { return ctx_; }
};

template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx, class Handle>
    requires CtxFitsEndpointHandle<Substr, Dir, Ctx, Handle>
[[nodiscard]] constexpr auto mint_endpoint(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::detail::late_door_t<EndpointDoor, Substr>::template make_<Substr, Dir>(ctx,
                                                                                                   std::move(handle));
}

// ── The door of the endpoint mint ────────────────────────────────────
//
// mint_endpoint gets the endpoint through this class.  The member of the
// class is private and static.  The one friend of the class is the mint,
// which does the check of the context and the handle before it calls the
// member.  The member does that check again and builds the endpoint.  The
// class is final, and no object of it exists.
class EndpointDoor final : ::foundation::NoObject<EndpointDoor> {
    template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx, class Handle>
        requires CtxFitsEndpointHandle<Substr, Dir, Ctx, Handle>
    friend constexpr auto mint_endpoint(Ctx const& ctx, Handle&& handle) noexcept;

    template <class Substr, Direction Dir, class Ctx, class Handle>
    [[nodiscard]] static constexpr auto make_(Ctx const& ctx, Handle&& handle) noexcept
        -> Endpoint<Substr, Dir, Ctx, Handle> {
        static_assert(CtxFitsEndpointHandle<Substr, Dir, Ctx, Handle>,
                      "fixy::channels::diagnostic [Endpoint_Door_Refused]: the endpoint door accepts only a "
                      "context and a handle that mint_endpoint accepts.");
        return Endpoint<Substr, Dir, Ctx, Handle>{ctx, std::move(handle)};
    }
};

// The tags and the context that only checks name.  The witness roster
// (the witness rows of utils/rule-table.txt) and the check file of this header
// spell an endpoint over them, and no code builds a channel of them.  The
// namespace is public, because a witness row names no detail namespace of the
// base.
namespace endpoint_witness {
struct UserTag {};
struct UserBrand {};
using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
}  // namespace endpoint_witness

}  // namespace fixy::channels
