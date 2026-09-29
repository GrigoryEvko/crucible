// The peer unit of test_session_wire_word.cpp.  It includes the session
// headers in another order than that file, and never includes
// fixy/session/Projection.h: the declaration of PeerMsg and its payload
// rule stand in fixy/session/Protocol.h.

#include <fixy/session/Handle.h>
#include <fixy/session/Crash.h>

#include <array>
#include <cstdint>

namespace s = ::fixy::session;

// The same definitions stand in test_session_wire_word.cpp.
namespace wire_word_probe {
struct Alice {};
struct Hello {};
struct Bye {};
using Menu =
    ::fixy::session::Select<::fixy::session::Send<::fixy::session::PeerMsg<Alice, Hello, int>, ::fixy::session::End>,
                            ::fixy::session::Send<::fixy::session::PeerMsg<Alice, Bye, int>, ::fixy::session::End>>;

[[nodiscard]] std::array<std::uint64_t, 2> words_of_the_peer_unit() noexcept;
}  // namespace wire_word_probe

namespace {

inline constexpr std::array<std::uint64_t, 2> peer_words{s::branch_wire_word_v<wire_word_probe::Menu, 0>,
                                                         s::branch_wire_word_v<wire_word_probe::Menu, 1>};

static_assert(s::is_well_formed_v<wire_word_probe::Menu>);

}  // namespace

std::array<std::uint64_t, 2> wire_word_probe::words_of_the_peer_unit() noexcept { return peer_words; }
