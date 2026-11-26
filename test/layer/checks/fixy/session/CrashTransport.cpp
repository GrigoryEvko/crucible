// The compile-time checks of fixy/session/CrashTransport.h.

#include <fixy/session/CrashTransport.h>

#include <foundation/core/CaptureStore.h>

#include <cstddef>

namespace fixy::session::detail::crash_transport_checks {

struct Port {};

// A write of a word that holds a View of const elements by value.  The view
// is empty, and only its type matters to a gate.
struct HoldingWrite {
    ::foundation::core::View<int const> view{};
    bool operator()(Port&, std::size_t& word) const noexcept { return view.size() != word; }
};

// The guarded write of a word moves the transport in, so the store check
// reads each capture of the transport as its own, as the gate of the crash
// door read it.  A write that held the transport by reference would make the
// View of the transport writable, and the gate of the inner handle would
// refuse the write.
consteval bool is_watched_word_write_owned() {
    HoldingWrite write{};
    bool is_written = false;
    auto watched = crash_transport::watched_word_write<Port>(nullptr, write, is_written);
    return ::foundation::core::StoresNoBorrowOutside<HoldingWrite>
        && ::foundation::core::StoresNoBorrowOutside<decltype(watched)>;
}
static_assert(is_watched_word_write_owned(),
              "watched_word_write holds the transport by value, so the store check admits it");

}  // namespace fixy::session::detail::crash_transport_checks
