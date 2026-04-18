#pragma once

// ── crucible::safety::Session<Resource, Steps...> ───────────────────
//
// Type-state protocol channel.  A Session's remaining protocol is its
// template parameter.  Each operation consumes the current Session and
// returns a new Session for the remaining protocol.  Wrong message
// type, wrong order, or missing step = compile error.
//
//   Axiom coverage: BorrowSafe, determinism of protocol order.
//   Runtime cost:   zero.  State lives entirely in the type; only the
//                   carried Resource handle occupies runtime memory.
//
// Step combinators:
//   Send<T>   — session sends a T next
//   Recv<T>   — session receives a T next
//   End       — terminal state; destructor cleans up
//
// Usage:
//   auto s = make_session<Send<Req>, Recv<Resp>, End>(fd);
//   auto s1 = std::move(s).send(req, [](auto& r, auto&& v){ r.write(v); });
//   auto [resp, s2] = std::move(s1).recv([](auto& r){ return r.read<Resp>(); });
//   (void)std::move(s2).close();
//
// Both send and recv take a transport callable that operates on the
// underlying Resource.  The Session framework enforces ORDER; the
// caller supplies the I/O.

#include <crucible/Platform.h>

#include <functional>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Step markers.
template <typename T> struct Send {};
template <typename T> struct Recv {};
struct End {};

// Forward declaration.
template <typename Resource, typename... Steps> class Session;

// Factory: start a protocol with a resource and a Steps... protocol spec.
template <typename... Steps, typename Resource>
[[nodiscard]] constexpr auto make_session(Resource r)
    noexcept(std::is_nothrow_move_constructible_v<Resource>)
{
    return Session<Resource, Steps...>{std::move(r)};
}

// ── Send<T> specialization ─────────────────────────────────────────

template <typename Resource, typename T, typename... Rest>
class [[nodiscard]] Session<Resource, Send<T>, Rest...> {
    Resource resource_;

    template <typename R, typename... S> friend class Session;

public:
    explicit constexpr Session(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

    Session(const Session&)            = delete("Session state is linear");
    Session& operator=(const Session&) = delete("Session state is linear");
    Session(Session&&)                  = default;
    Session& operator=(Session&&)       = default;

    // Send value via user-supplied transport.  Transport signature:
    //   void(Resource&, T&&)
    // Returns a Session for the remaining protocol steps.
    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr Session<Resource, Rest...> send(T value, Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&, T&&>
                 && std::is_nothrow_move_constructible_v<Resource>)
    {
        std::invoke(transport, resource_, std::move(value));
        return Session<Resource, Rest...>{std::move(resource_)};
    }

    // Borrow the resource for inspection (logging, metrics).  Does NOT
    // advance the type-state; the same step is still required.
    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ── Recv<T> specialization ─────────────────────────────────────────

template <typename Resource, typename T, typename... Rest>
class [[nodiscard]] Session<Resource, Recv<T>, Rest...> {
    Resource resource_;

    template <typename R, typename... S> friend class Session;

public:
    explicit constexpr Session(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

    Session(const Session&)            = delete("Session state is linear");
    Session& operator=(const Session&) = delete("Session state is linear");
    Session(Session&&)                  = default;
    Session& operator=(Session&&)       = default;

    // Receive via user-supplied transport.  Transport signature:
    //   T(Resource&)
    // Returns (value, next-Session).
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) &&
    {
        T value = std::invoke(transport, resource_);
        return std::pair<T, Session<Resource, Rest...>>{
            std::move(value),
            Session<Resource, Rest...>{std::move(resource_)}
        };
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const & noexcept { return resource_; }
};

// ── End specialization ─────────────────────────────────────────────

template <typename Resource>
class [[nodiscard]] Session<Resource, End> {
    Resource resource_;

    template <typename R, typename... S> friend class Session;

public:
    explicit constexpr Session(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

    Session(const Session&)            = delete("Session state is linear");
    Session& operator=(const Session&) = delete("Session state is linear");
    Session(Session&&)                  = default;
    Session& operator=(Session&&)       = default;
    ~Session()                          = default;  // Resource's dtor runs

    // Explicit close — consumes the Session and returns the resource
    // for the caller to close (useful when Resource is not RAII).
    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        return std::move(resource_);
    }
};

} // namespace crucible::safety
