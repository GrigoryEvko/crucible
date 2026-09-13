#pragma once

#include <crucible/Platform.h>
#include <crucible/safety/Pinned.h>
#include <crucible/handles/PublishOnce.h>
#include <crucible/sessions/Session.h>

#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename Proto, typename Resource>
class [[nodiscard]] LazyEstablishedChannel : public Pinned<LazyEstablishedChannel<Proto, Resource>> {
    static_assert(safety::proto::is_well_formed_v<Proto>, "crucible::session::diagnostic [Protocol_Ill_Formed]: "
                                                          "LazyEstablishedChannel<Proto, Resource>: Proto must be "
                                                          "well-formed (every Continue must have an enclosing Loop).");

    PublishOnce<Resource> resource_;

public:
    using protocol = Proto;
    using resource_type = Resource;

    using session_handle_type = decltype(safety::proto::mint_session_handle<Proto>(std::declval<Resource*>()));

    constexpr LazyEstablishedChannel() noexcept = default;

    ~LazyEstablishedChannel() = default;

    // The caller owns two obligations the compiler cannot check. The
    // pointee must be fully initialised before this call returns, and
    // it must outlive every handle that any later observe yields.
    void establish(Resource* r) noexcept { resource_.publish(r); }

    // Every call yields a fresh handle onto one shared resource. Two
    // handles held at once do not coordinate with each other, so the
    // resource carries whatever discipline that needs.
    [[nodiscard]] std::optional<session_handle_type> observe() noexcept {
        Resource* r = resource_.observe();
        if (!r) return std::nullopt;
        return safety::proto::mint_session_handle<Proto>(r);
    }

    // This read carries no ordering. Reach the resource through
    // observe before dereferencing it, never on the strength of this.
    [[nodiscard]] bool is_established() const noexcept { return resource_.is_published(); }

    // The name is rendered from an implementation-specific spelling.
    // It serves runtime matching, not compile-time identity.
    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept {
        return safety::proto::detail::type_name<Proto>();
    }
};

namespace detail::lec_size_test {

struct AnyResource {};
using P1 = safety::proto::End;
using P2 = safety::proto::Loop<safety::proto::Send<int, safety::proto::Continue>>;

static_assert(sizeof(LazyEstablishedChannel<P1, AnyResource>) == sizeof(PublishOnce<AnyResource>),
              "LazyEstablishedChannel must add zero bytes beyond its PublishOnce.");

static_assert(sizeof(LazyEstablishedChannel<P2, AnyResource>) == sizeof(std::atomic<AnyResource*>),
              "LazyEstablishedChannel must collapse to one atomic pointer.");

}  // namespace detail::lec_size_test

namespace detail::lec_self_test {

struct DummyChannel {
    int sentinel = 0;
};

using SimpleProto =
    safety::proto::Loop<safety::proto::Select<safety::proto::Send<int, safety::proto::Continue>, safety::proto::End>>;

using LEC = LazyEstablishedChannel<SimpleProto, DummyChannel>;

static_assert(!std::is_copy_constructible_v<LEC>);
static_assert(!std::is_move_constructible_v<LEC>);
static_assert(std::is_base_of_v<Pinned<LEC>, LEC>);

static_assert(std::is_same_v<typename LEC::protocol, SimpleProto>);
static_assert(std::is_same_v<typename LEC::resource_type, DummyChannel>);

// A loop unrolls when the handle is constructed. The handle's own
// protocol is therefore the loop body, and the whole loop rides along
// as the surrounding context.
using ExpectedSession = safety::proto::SessionHandle<
    safety::proto::Select<safety::proto::Send<int, safety::proto::Continue>, safety::proto::End>, DummyChannel*,
    SimpleProto>;
static_assert(std::is_same_v<typename LEC::session_handle_type, ExpectedSession>);

}  // namespace detail::lec_self_test

}  // namespace crucible::safety
