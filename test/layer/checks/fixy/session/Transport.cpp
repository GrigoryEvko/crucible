// The compile-time checks of fixy/session/Transport.h.

#include <fixy/session/Transport.h>

#include <foundation/core/CaptureStore.h>

#include <cstddef>

namespace fixy::session::detail::transport_checks {

struct Port {};

// A write and a read that hold a View of const elements by value.  The view
// is empty, and only its type matters to a gate.
struct HoldingWrite {
    ::foundation::core::View<int const> view{};
    bool operator()(Port&, std::size_t& word) const noexcept { return view.size() != word; }
};
struct HoldingRead {
    ::foundation::core::View<int const> view{};
    ::fixy::Option<std::size_t> operator()(Port&) const noexcept { return ::fixy::some(view.size()); }
};

// The wrappers of a decorator move the transport in, so the store check
// reads each capture of the transport as its own, as the gate of the
// decorator read it.  A wrapper that held the transport by reference would
// make the View of the transport writable, and the gate of the inner handle
// would refuse the wrapper.
consteval bool are_observed_transports_owned() {
    HoldingRead read{};
    HoldingWrite write{};
    auto observed = observed_read<std::size_t, Port>(read, [](std::size_t const&) noexcept {});
    auto marked = observed_write<std::size_t, Port>(write, []() noexcept {});
    return ::foundation::core::StoresNoBorrowOutside<HoldingRead>
        && ::foundation::core::StoresNoBorrowOutside<decltype(observed)>
        && ::foundation::core::StoresNoBorrowOutside<decltype(marked)>;
}
static_assert(are_observed_transports_owned(),
              "observed_read and observed_write hold the transport by value, so the store check admits them");

// The gates admit a transport that holds a View by value, and refuse a
// transport or a stop test that can keep a View in an object that it
// captures by reference, past the call.  A user passes no stop test: the
// crash decorator gives its own to the send of the inner handle.
struct KeepingWrite {
    ::fixy::Option<::foundation::core::View<int const>>& kept;
    bool operator()(Port&, std::size_t&) const noexcept { return kept.is_some(); }
};
struct KeepingRead {
    ::fixy::Option<::foundation::core::View<int const>>& kept;
    ::fixy::Option<std::size_t> operator()(Port&) const noexcept { return ::fixy::some(std::size_t{0}); }
};
struct KeepingStop {
    ::fixy::Option<::foundation::core::View<int const>>& kept;
    bool operator()() const noexcept { return kept.is_some(); }
};
struct PlainStop {
    bool operator()() const noexcept { return false; }
};
static_assert(WriteTransport<HoldingWrite, Port, std::size_t> && ReadTransport<HoldingRead, Port, std::size_t>);
static_assert(!WriteTransport<KeepingWrite, Port, std::size_t> && !ReadTransport<KeepingRead, Port, std::size_t>);
static_assert(WriteStop<PlainStop> && !WriteStop<KeepingStop>);

}  // namespace fixy::session::detail::transport_checks
