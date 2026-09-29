#pragma once

// A session channel whose resource arrives after the channel exists.
// One thread builds the channel.  A different thread publishes the
// resource later.  An observer polls the channel until the resource is
// published, and then the channel mints one session over the resource
// to the first observer that asks.  The channel refuses each later
// request.
//
// ── Design notes ─────────────────────────────────────────────────────
//
//   * mint_established_session gives one handle, one time.  Two live
//     handles at one protocol position over one resource would each owe
//     the next step, and neither would know of the other.
//   * The Resource is a Pinned object, and the session holds it by
//     lvalue reference.  fixy::session::SessionResource refuses a raw
//     pointer.
//   * The mint takes an execution context.  The gate of
//     fixy::session::mint_session decides if the context can run the
//     protocol.
//   * The mint returns std::expected.  The refusal tells why no handle
//     came out: the resource is not published, or a different observer
//     holds the session.
//   * The claim flag sits beside the publication slot, so the channel is
//     two words and not one.

#include <fixy/handle/PublishOnce.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Stepping.h>

#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>

#include <atomic>
#include <concepts>
#include <cstdint>
#include <expected>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

// The claim the channel makes that no lattice grades.
// foundation/diag/RowHash.h folds the identity together with the
// protocol, so two channels over two protocols take two cache slots.
namespace fixy::row_discipline {
template <typename Proto>
struct lazy_established_channel;
}  // namespace fixy::row_discipline

namespace fixy::handle {

// The reason that mint_established_session gives no handle.
enum class LazyChannelRefusal : std::uint8_t {
    // No thread called establish yet.  The observer can ask again.
    NotEstablished,
    // A different observer holds the one session of the channel.  The
    // refusal is permanent for this channel.
    AlreadyClaimed,
};

template <typename Proto, typename Resource>
class [[nodiscard]] LazyEstablishedChannel : public ::foundation::Pinned<LazyEstablishedChannel<Proto, Resource>> {
    static_assert(::fixy::session::is_well_formed_v<Proto>,
                  "fixy::session::diagnostic [Protocol_Ill_Formed]: LazyEstablishedChannel<Proto, Resource>: Proto "
                  "must be well-formed.  Every Continue must have a Loop above it.");

    // The session holds the Resource by lvalue reference, and
    // fixy::session::SessionResource admits a reference only to a Pinned
    // object.  A raw pointer is refused, so the resource has no other
    // route into the session.
    static_assert(std::derived_from<Resource, ::foundation::Pinned<Resource>>,
                  "fixy::session::diagnostic [SessionResource_Refused]: LazyEstablishedChannel<Proto, Resource>: "
                  "the Resource must derive from foundation::Pinned<Resource>.  The session holds it by lvalue "
                  "reference, so its address must not change after establish.");

    PublishOnce<Resource> resource_;
    std::atomic<bool> claimed_{false};

public:
    using protocol = Proto;
    using resource_type = Resource;
    using row_discipline = ::fixy::row_discipline::lazy_established_channel<Proto>;
    using row_payload = Resource;

    constexpr LazyEstablishedChannel() noexcept = default;
    ~LazyEstablishedChannel() = default;

    // The caller has two obligations that the compiler cannot examine.
    // The resource must be fully initialized before this call.  The
    // resource must also stay alive until the session over it is closed
    // or detached.  A second establish ends the process, as
    // PublishOnce::publish does.
    void establish(Resource& resource) noexcept { resource_.publish(&resource); }

    // Gives the one session of the channel to the first caller after
    // establish.  A call before establish claims nothing, so the
    // observer can poll.  The load of the slot is an acquire, so the
    // session sees the resource as establish left it.  The claim is an
    // exchange, so two observers that race get one handle between them.
    template <::foundation::effects::IsExecCtx Ctx>
        requires ::fixy::session::CtxFitsSession<Ctx, Proto, Resource&>
    [[nodiscard]] auto mint_established_session(  // MINT-PATTERN-OK: reads the atomic slot and claim flag
        Ctx const& ctx, std::source_location loc = std::source_location::current()) noexcept
        -> std::expected<decltype(::fixy::session::mint_session<Proto, ::fixy::session::DefaultAbandonmentPolicy,
                                                                  Ctx, Resource&>(ctx, std::declval<Resource&>())),
                         LazyChannelRefusal> {
        Resource* const published = resource_.observe();
        if (published == nullptr) return std::unexpected(LazyChannelRefusal::NotEstablished);
        if (claimed_.exchange(true, std::memory_order_acq_rel)) {
            return std::unexpected(LazyChannelRefusal::AlreadyClaimed);
        }
        return ::fixy::session::mint_session<Proto, ::fixy::session::DefaultAbandonmentPolicy, Ctx, Resource&>(
            ctx, *published, loc);
    }

    // This read carries no ordering.  Get the session through
    // mint_established_session, never because this read is true.
    [[nodiscard]] bool is_established() const noexcept { return resource_.is_published(); }

    // The name comes from display_string_of.  It is for matching at run
    // time, not for identity at compile time.
    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept {
        return ::fixy::session::type_display_name_v<Proto>;
    }
};

namespace detail::lazy_established_channel_self_test {

struct Wire : ::foundation::Pinned<Wire> {
    int sentinel = 0;
};

using Stream = ::fixy::session::Loop<
    ::fixy::session::Select<::fixy::session::Send<int, ::fixy::session::Continue>, ::fixy::session::End>>;
using Channel = LazyEstablishedChannel<Stream, Wire>;

static_assert(!std::is_copy_constructible_v<Channel>);
static_assert(!std::is_move_constructible_v<Channel>);
static_assert(std::is_base_of_v<::foundation::Pinned<Channel>, Channel>);
static_assert(sizeof(Channel) == 2 * sizeof(std::atomic<Wire*>),
              "the channel is the publication slot and the claim flag, and nothing more");

}  // namespace detail::lazy_established_channel_self_test

}  // namespace fixy::handle
