// The label word of a keyed branch is one value in every translation
// unit.  This file and test_session_wire_word_peer.cpp include the
// session headers in different orders, and the peer file reads its words
// before it includes a second header.  The registrations and the payload
// rules that give a branch its label key stand under the seals of
// fixy/session/Protocol.h, so neither file can add one.  The word is then
// the stable type id of the label key, and the label key is a function of
// the payload type alone.  main compares the words of the two files.
//
// This file also specializes step_wire_word_v for one keyed step, and
// branch_wire_word_v for one branch of a keyed choice.  Each
// specialization changes what this file reads through that spelling, and
// the handle still sends the word of the registry, so no user spelling
// reaches the wire.

#include <fixy/session/Projection.h>
#include <fixy/session/Crash.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace s = ::fixy::session;
namespace tr = ::foundation::algebra::transition;

// The same definitions stand in the peer file.
namespace wire_word_probe {
struct Alice {};
struct Hello {};
struct Bye {};
using Menu =
    ::fixy::session::Select<::fixy::session::Send<::fixy::session::PeerMsg<Alice, Hello, int>, ::fixy::session::End>,
                            ::fixy::session::Send<::fixy::session::PeerMsg<Alice, Bye, int>, ::fixy::session::End>>;

[[nodiscard]] std::array<std::uint64_t, 2> words_of_the_peer_unit() noexcept;

using KeyedHello = ::fixy::session::Send<::fixy::session::Labelled<Hello, int>, ::fixy::session::End>;
using KeyedPick =
    ::fixy::session::Select<::fixy::session::Send<::fixy::session::Labelled<Hello, void>, ::fixy::session::End>,
                            ::fixy::session::Send<::fixy::session::Labelled<Bye, void>, ::fixy::session::End>>;
inline constexpr std::uint64_t forged_word = 7;
}  // namespace wire_word_probe

namespace fixy::session {
template <>
inline constexpr std::uint64_t step_wire_word_v<wire_word_probe::KeyedHello> = wire_word_probe::forged_word;
template <>
inline constexpr std::uint64_t branch_wire_word_v<wire_word_probe::KeyedPick, 0> = wire_word_probe::forged_word;
}  // namespace fixy::session

namespace {

using wire_word_probe::Alice;
using wire_word_probe::Bye;
using wire_word_probe::Hello;
using wire_word_probe::KeyedHello;
using wire_word_probe::KeyedPick;
using wire_word_probe::Menu;

static_assert(s::is_keyed_choice_v<Menu>);
static_assert(s::step_wire_word_v<KeyedHello> == wire_word_probe::forged_word,
              "the specialization answers for its author");
static_assert(s::branch_wire_word_v<KeyedPick, 0> == wire_word_probe::forged_word,
              "the specialization answers for its author");

// The Resource of the keyed send: the word that the transport took.
struct WordWire {
    std::uint64_t* written = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;

// Sends the keyed message of KeyedHello and returns the word that the
// handle wrote.
[[nodiscard]] std::uint64_t word_the_handle_sends() {
    const BgCtx ctx{::foundation::effects::testing::bg()};
    std::uint64_t written = 0;
    auto head = s::mint_session<KeyedHello>(ctx, WordWire{&written});
    auto at_value = std::move(head).send([](WordWire& wire, std::size_t& word) noexcept {
        *wire.written = word;
        return true;
    });
    auto at_end = std::move(at_value).send(1, [](WordWire&, int&) noexcept { return true; });
    static_cast<void>(std::move(at_end).close());
    return written;
}

// Picks branch 0 of KeyedPick and returns the word that the handle wrote.
[[nodiscard]] std::uint64_t word_the_select_sends() {
    const BgCtx ctx{::foundation::effects::testing::bg()};
    std::uint64_t written = 0;
    auto head = s::mint_session<KeyedPick>(ctx, WordWire{&written});
    auto at_end = std::move(head).select<0>([](WordWire& wire, std::size_t& word) noexcept {
        *wire.written = word;
        return true;
    });
    static_cast<void>(std::move(at_end).close());
    return written;
}

inline constexpr std::array<std::uint64_t, 2> own_words{s::branch_wire_word_v<Menu, 0>, s::branch_wire_word_v<Menu, 1>};

static_assert(own_words[0] == tr::label_word_of(^^s::Labelled<Hello, void>));
static_assert(own_words[0] == (::foundation::reflect::stable_type_id<s::Labelled<Hello, void>> | tr::label_word_bit),
              "the word is the stable type id of the label key, with the top bit set");
static_assert(own_words[0] != own_words[1]);

// Each endpoint names the other role as its peer.  A PeerMsg is keyed by
// its label alone, so a choice, its dual and its stripped view put the
// same word on the wire for each label.
struct Bob {};
using AliceMenu =
    s::Offer<s::Sender<Bob>, s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>, s::Recv<s::PeerMsg<Bob, Bye, int>, s::End>>;
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
    const std::uint64_t sent = word_the_handle_sends();
    if (sent != tr::label_word_of(^^s::Labelled<Hello, void>) || sent == wire_word_probe::forged_word) {
        std::fprintf(stderr,
                     "test_session_wire_word: the handle sent %016llx, and a specialization of step_wire_word_v "
                     "reached the wire\n",
                     static_cast<unsigned long long>(sent));
        return 1;
    }
    const std::uint64_t picked = word_the_select_sends();
    if (picked != tr::label_word_of(^^s::Labelled<Hello, void>) || picked == wire_word_probe::forged_word) {
        std::fprintf(stderr,
                     "test_session_wire_word: the select sent %016llx, and a specialization of branch_wire_word_v "
                     "reached the wire\n",
                     static_cast<unsigned long long>(picked));
        return 1;
    }
    return 0;
}
