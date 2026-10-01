#pragma once

// Binding a channel handle to an execution context checks the fit between
// the two once, at construction.  Every send and receive afterwards runs
// with no further check.
//
// The endpoint owns its handle.  The channel outlives the handle by
// construction, and the handle releases its permission when the endpoint
// ends.
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

#include <fixy/concurrent/SubstrateSessionBridge.h>
#include <fixy/session/Handle.h>

#include <foundation/NoObject.h>
#include <foundation/effects/Ctx.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

// The class template, the endpoint door and the mint all spell this one
// gate, so they cannot drift apart.  An endpoint keeps a copy of its
// context, so the context must be copyable.  A branded context is not.
template <class Substr, Direction Dir, class Ctx>
concept CtxFitsEndpointMint = CtxFitsSubstrateSessionMint<Substr, Dir, Ctx> && std::copy_constructible<Ctx>;

class EndpointDoor;

template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsEndpointMint<Substr, Dir, Ctx>
class [[nodiscard]] Endpoint {
public:
    using substrate_type = Substr;
    static constexpr Direction direction = Dir;
    using ctx_type = Ctx;
    using handle_type = handle_for_t<Substr, Dir>;
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

    template <Direction D = Dir>
        requires(D == Direction::Consumer)
    [[nodiscard, gnu::hot]] std::optional<value_type> try_recv() noexcept {
        return handle_.try_pop();
    }

    [[nodiscard]] bool empty_approx() const noexcept
        requires requires(handle_type const& h) { h.empty_approx(); }
    {
        return handle_.empty_approx();
    }
    [[nodiscard]] std::size_t size_approx() const noexcept
        requires requires(handle_type const& h) { h.size_approx(); }
    {
        return handle_.size_approx();
    }

    // The session carries an empty permission set.  The handle's own
    // permission already fixes how many producers or consumers there may
    // be, so the protocol has nothing further to transfer per step.

    [[nodiscard]] constexpr auto into_session() && noexcept {
        return mint_substrate_session<Substr, Dir>(ctx_, std::move(handle_));
    }

    // Dropping the context to hand out a bare session handle is safe for
    // the same reason: the gate of the mint already admitted each payload
    // of the protocol, and an empty set evolves at no step of it.

    [[nodiscard]] constexpr auto into_bare_session() && noexcept {
        return ::fixy::session::mint_session_handle<proto_type>(std::move(handle_));
    }

    [[nodiscard]] constexpr handle_type& handle() & noexcept { return handle_; }
    [[nodiscard]] constexpr handle_type const& handle() const& noexcept { return handle_; }

    // The returned handle is the only one that holds the permission, and
    // the endpoint that gave it reaches no channel afterwards.

    [[nodiscard]] constexpr handle_type into_handle() && noexcept { return std::move(handle_); }

    [[nodiscard]] constexpr Ctx ctx() const noexcept { return ctx_; }
};

template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsEndpointMint<Substr, Dir, Ctx>
[[nodiscard]] constexpr auto mint_endpoint(Ctx const& ctx, handle_for_t<Substr, Dir>&& handle) noexcept {
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
    template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsEndpointMint<Substr, Dir, Ctx>
    friend constexpr auto mint_endpoint(Ctx const& ctx, handle_for_t<Substr, Dir>&& handle) noexcept;

    template <class Substr, Direction Dir, class Ctx>
    [[nodiscard]] static constexpr auto make_(Ctx const& ctx, handle_for_t<Substr, Dir>&& handle) noexcept
        -> Endpoint<Substr, Dir, Ctx> {
        static_assert(CtxFitsEndpointMint<Substr, Dir, Ctx>,
                      "fixy::concurrent::diagnostic [Endpoint_Door_Refused]: the endpoint door accepts only a "
                      "context and a handle that mint_endpoint accepts.");
        return Endpoint<Substr, Dir, Ctx>{ctx, std::move(handle)};
    }
};

// The tags and the context that only checks name.  The witness roster
// (utils/scripts/witness-roster.txt) and the check file of this header
// spell an endpoint over them, and no code builds a channel of them.
namespace detail::endpoint_witness {
struct UserTag {};
struct UserBrand {};
using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
}  // namespace detail::endpoint_witness

}  // namespace fixy::concurrent
