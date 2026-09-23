// The peer unit of test_session_wire_word.cpp.  It includes the session
// headers in another order than that file, reads its words before it
// registers its own combinator, and never includes
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
using Menu = ::fixy::session::Select<
    ::fixy::session::Send<::fixy::session::PeerMsg<Alice, Hello, int>, ::fixy::session::End>,
    ::fixy::session::Send<::fixy::session::PeerMsg<Alice, Bye, int>, ::fixy::session::End>>;

[[nodiscard]] std::array<std::uint64_t, 2> words_of_the_peer_unit() noexcept;
}  // namespace wire_word_probe

namespace {

inline constexpr std::array<std::uint64_t, 2> peer_words{s::branch_wire_word_v<wire_word_probe::Menu, 0>,
                                                         s::branch_wire_word_v<wire_word_probe::Menu, 1>};

template <class T, class K>
struct Relay {};
template <class T, class K>
struct Unrelay {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator wire_word_relay{
    .shape = ^^::Relay,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Unrelay,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
inline constexpr ::foundation::algebra::transition::combinator wire_word_unrelay{
    .shape = ^^::Unrelay,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Relay,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
}  // namespace fixy::session::combinators

namespace {
static_assert(s::is_well_formed_v<Relay<int, wire_word_probe::Menu>>, "the combinator of this file is in the registry");
static_assert(peer_words[0] == s::branch_wire_word_v<wire_word_probe::Menu, 0>,
              "a combinator registered after the words does not move them");
}  // namespace

std::array<std::uint64_t, 2> wire_word_probe::words_of_the_peer_unit() noexcept { return peer_words; }
