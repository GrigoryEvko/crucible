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

}  // namespace fixy::session::detail::transport_checks
