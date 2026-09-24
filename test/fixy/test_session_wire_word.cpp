// The label word of a keyed branch is one value in every translation
// unit.  This file and test_session_wire_word_peer.cpp include the
// session headers in different orders, and each registers a combinator
// of its own: this file before it reads its words, the peer file after.
// The payload rules that give a branch its label key stand under the
// seal of fixy/session/Protocol.h, so neither file can add one.  The word
// is then the stable type id of the label key, and the label key is a
// function of the payload type alone.  main compares the words of the two
// files.

#include <fixy/session/Projection.h>
#include <fixy/session/Crash.h>
#include <fixy/session/Handle.h>

#include <array>
#include <cstdint>
#include <cstdio>

namespace s = ::fixy::session;
namespace tr = ::foundation::algebra::transition;

namespace {

template <class T, class K>
struct Emit {};
template <class T, class K>
struct Absorb {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator wire_word_emit{
    .shape = ^^::Emit,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Absorb,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
inline constexpr ::foundation::algebra::transition::combinator wire_word_absorb{
    .shape = ^^::Absorb,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Emit,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
}  // namespace fixy::session::combinators

// The same definitions stand in the peer file.
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

using wire_word_probe::Alice;
using wire_word_probe::Bye;
using wire_word_probe::Hello;
using wire_word_probe::Menu;

static_assert(s::is_well_formed_v<Emit<int, Menu>>, "the combinator of this file is in the registry");
static_assert(s::is_keyed_choice_v<Menu>);

inline constexpr std::array<std::uint64_t, 2> own_words{s::branch_wire_word_v<Menu, 0>, s::branch_wire_word_v<Menu, 1>};

static_assert(own_words[0] == tr::label_word_of(^^s::Labelled<Hello, void>));
static_assert(own_words[0] == (::foundation::reflect::stable_type_id<s::Labelled<Hello, void>> | tr::label_word_bit),
              "the word is the stable type id of the label key, with the top bit set");
static_assert(own_words[0] != own_words[1]);

// Each endpoint names the other role as its peer.  A PeerMsg is keyed by
// its label alone, so a choice, its dual and its stripped view put the
// same word on the wire for each label.
struct Bob {};
using AliceMenu = s::Offer<s::Sender<Bob>, s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>,
                           s::Recv<s::PeerMsg<Bob, Bye, int>, s::End>>;
using ViewMenu = s::Select<s::Send<s::Labelled<Hello, int>, s::End>, s::Send<s::Labelled<Bye, int>, s::End>>;
static_assert(s::branch_wire_word_v<Menu, 0> == s::branch_wire_word_v<AliceMenu, 0>
              && s::branch_wire_word_v<Menu, 1> == s::branch_wire_word_v<AliceMenu, 1>);
static_assert(s::branch_wire_word_v<Menu, 0> == s::branch_wire_word_v<ViewMenu, 0>
              && s::branch_wire_word_v<Menu, 1> == s::branch_wire_word_v<ViewMenu, 1>);
static_assert(s::step_wire_word_v<s::Send<s::PeerMsg<Alice, Bye, int>, s::End>> == s::branch_wire_word_v<AliceMenu, 1>);

}  // namespace

int main() {
    const std::array<std::uint64_t, 2> peer_words = wire_word_probe::words_of_the_peer_unit();
    const bool is_same = peer_words == own_words;
    std::printf("test_session_wire_word: words %016llx %016llx here, %016llx %016llx in the peer unit\n",
                static_cast<unsigned long long>(own_words[0]), static_cast<unsigned long long>(own_words[1]),
                static_cast<unsigned long long>(peer_words[0]), static_cast<unsigned long long>(peer_words[1]));
    if (!is_same) {
        std::fprintf(stderr, "test_session_wire_word: two translation units put different words on one wire\n");
        return 1;
    }
    return 0;
}
