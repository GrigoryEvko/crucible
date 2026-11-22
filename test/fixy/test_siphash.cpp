// foundation/core/SipHash.h and fixy/SipHash.h against the published
// SipHash-2-4 test vectors.
//
// The vectors are the 64 rows of vectors.h in the reference implementation
// by Aumasson and Bernstein: key 00..0f, message 00..len-1 for each length
// from 0 to 63.  The row at length 15 is also the worked example in the
// appendix of the paper.  OpenSSL 3.5 gives the same 64 tags.

#include <fixy/SipHash.h>

#include <foundation/core/SipHash.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace sh = ::fixy::siphash;
namespace fc = ::foundation::core;
using fc::SipKeyBytes;
using fc::SipTag;
using ::fixy::mint_secret;
using ::fixy::Secret;

inline constexpr std::size_t kVectorCount = 64;

inline constexpr fc::FixedArray<std::uint64_t, kVectorCount> kReferenceTags{
    0x726fdb47dd0e0e31ULL, 0x74f839c593dc67fdULL, 0x0d6c8009d9a94f5aULL, 0x85676696d7fb7e2dULL, 0xcf2794e0277187b7ULL,
    0x18765564cd99a68dULL, 0xcbc9466e58fee3ceULL, 0xab0200f58b01d137ULL, 0x93f5f5799a932462ULL, 0x9e0082df0ba9e4b0ULL,
    0x7a5dbbc594ddb9f3ULL, 0xf4b32f46226bada7ULL, 0x751e8fbc860ee5fbULL, 0x14ea5627c0843d90ULL, 0xf723ca908e7af2eeULL,
    0xa129ca6149be45e5ULL, 0x3f2acc7f57c29bdbULL, 0x699ae9f52cbe4794ULL, 0x4bc1b3f0968dd39cULL, 0xbb6dc91da77961bdULL,
    0xbed65cf21aa2ee98ULL, 0xd0f2cbb02e3b67c7ULL, 0x93536795e3a33e88ULL, 0xa80c038ccd5ccec8ULL, 0xb8ad50c6f649af94ULL,
    0xbce192de8a85b8eaULL, 0x17d835b85bbb15f3ULL, 0x2f2e6163076bcfadULL, 0xde4daaaca71dc9a5ULL, 0xa6a2506687956571ULL,
    0xad87a3535c49ef28ULL, 0x32d892fad841c342ULL, 0x7127512f72f27cceULL, 0xa7f32346f95978e3ULL, 0x12e0b01abb051238ULL,
    0x15e034d40fa197aeULL, 0x314dffbe0815a3b4ULL, 0x027990f029623981ULL, 0xcadcd4e59ef40c4dULL, 0x9abfd8766a33735cULL,
    0x0e3ea96b5304a7d0ULL, 0xad0c42d6fc585992ULL, 0x187306c89bc215a9ULL, 0xd4a60abcf3792b95ULL, 0xf935451de4f21df2ULL,
    0xa9538f0419755787ULL, 0xdb9acddff56ca510ULL, 0xd06c98cd5c0975ebULL, 0xe612a3cb9ecba951ULL, 0xc766e62cfcadaf96ULL,
    0xee64435a9752fe72ULL, 0xa192d576b245165aULL, 0x0a8787bf8ecb74b2ULL, 0x81b3e73d20b49b6fULL, 0x7fa8220ba3b2eceaULL,
    0x245731c13ca42499ULL, 0xb78dbfaf3a8d83bdULL, 0xea1ad565322a1a0bULL, 0x60e61c23a3795013ULL, 0x6606d7e446282b93ULL,
    0x6ca4ecb15c5f91e1ULL, 0x9f626da15c9625f3ULL, 0xe51b38608ef25f57ULL, 0x958a324ceb064572ULL,
};

[[nodiscard]] constexpr SipKeyBytes reference_key() noexcept {
    SipKeyBytes key{};
    key.with_mut([](auto run) noexcept {
        std::uint8_t next = 0;
        for (std::byte& byte : run) {
            byte = static_cast<std::byte>(next);
            ++next;
        }
    });
    return key;
}

[[nodiscard]] constexpr fc::FixedArray<std::byte, kVectorCount> reference_message() noexcept {
    fc::FixedArray<std::byte, kVectorCount> message{};
    message.with_mut([](auto run) noexcept {
        std::uint8_t next = 0;
        for (std::byte& byte : run) {
            byte = static_cast<std::byte>(next);
            ++next;
        }
    });
    return message;
}

// The tag of the vector at length: its word, as eight little-endian bytes.
[[nodiscard]] constexpr SipTag expected_tag(std::size_t length) noexcept {
    std::uint64_t const word = kReferenceTags.at(fc::Index<kVectorCount>::check(length).expect("a vector length"));
    SipTag tag{};
    tag.with_mut([word](auto run) noexcept {
        int shift = 0;
        for (std::byte& byte : run) {
            byte = static_cast<std::byte>(static_cast<std::uint8_t>(word >> shift));
            shift += 8;
        }
    });
    return tag;
}

[[nodiscard]] constexpr bool same_tag(SipTag const& left, SipTag const& right) noexcept {
    return ::foundation::core::equal(left, right);
}

[[nodiscard]] constexpr fc::View<std::byte const>
prefix(fc::FixedArray<std::byte, kVectorCount> const& message CRUCIBLE_LIFETIMEBOUND, std::size_t length) noexcept {
    return fc::View<std::byte const>{message.view()}.window(0, length).expect("the length is inside the message");
}
template <class Whole, class... Rest>
void prefix(Whole const&&, Rest&&...) = delete("the View borrows the message, and a temporary ends with the full "
                                               "expression");

[[nodiscard]] constexpr SipTag tag_of(std::size_t length) noexcept {
    fc::FixedArray<std::byte, kVectorCount> const message = reference_message();
    return fc::sip_hash(reference_key(), prefix(message, length));
}

[[nodiscard]] constexpr SipTag signed_tag_of(std::size_t length) noexcept {
    fc::FixedArray<std::byte, kVectorCount> const message = reference_message();
    auto signed_message = sh::sign(mint_secret<SipKeyBytes>(reference_key()), prefix(message, length));
    return signed_message.at(fc::index<1>());
}

// The hash runs at compile time, so the empty message, the worked example of
// the paper and the longest message are checked by the compiler, through the
// raw hash and through the message authentication code.
static_assert(same_tag(tag_of(0), expected_tag(0)));
static_assert(same_tag(tag_of(15), expected_tag(15)));
static_assert(same_tag(tag_of(kVectorCount - 1), expected_tag(kVectorCount - 1)));
static_assert(same_tag(signed_tag_of(15), expected_tag(15)));

// The tag of a text is the tag of its characters as bytes.
constexpr bool text_gives_the_bytes_tag() noexcept {
    constexpr fc::TextView text{"a text of nineteen."};
    fc::FixedArray<std::byte, 19> bytes{};
    bytes.with_mut([text](auto run) noexcept {
        auto cursor = run.begin();
        for (char const letter : text) {
            *cursor = static_cast<std::byte>(letter);
            ++cursor;
        }
    });
    return text.size() == 19
        && same_tag(fc::sip_hash(reference_key(), text),
                    fc::sip_hash(reference_key(), fc::View<std::byte const>{bytes.view()}));
}
static_assert(text_gives_the_bytes_tag());

// A key reaches an operation only by a move, so a caller cannot keep a
// second copy of it by accident.
static_assert(!std::is_invocable_v<decltype(&sh::sign), Secret<SipKeyBytes>&, fc::View<std::byte const>>);
static_assert(std::is_invocable_v<decltype(&sh::sign), Secret<SipKeyBytes>&&, fc::View<std::byte const>>);
static_assert(
    !std::is_invocable_v<decltype(&sh::verify), Secret<SipKeyBytes>&, fc::View<std::byte const>, SipTag const&>);

// ── Runtime cells ───────────────────────────────────────────────────

// Every published vector, at run time, through the key that each call
// gives back.
int check_every_vector() {
    fc::FixedArray<std::byte, kVectorCount> const message = reference_message();
    Secret<SipKeyBytes> key = mint_secret<SipKeyBytes>(reference_key());
    for (std::size_t length = 0; length < kVectorCount; ++length) {
        if (!same_tag(fc::sip_hash(reference_key(), prefix(message, length)), expected_tag(length))) return 9;
        auto [kept, tag] = sh::sign(std::move(key), prefix(message, length));
        if (!same_tag(tag, expected_tag(length))) return 10;
        auto [checked, accepted] = sh::verify(std::move(kept), prefix(message, length), tag);
        if (!accepted) return 11;
        key = std::move(checked);
    }
    return 0;
}

// A tag with any one bit changed does not authenticate the message.
int check_each_forged_bit() {
    fc::FixedArray<std::byte, kVectorCount> const message = reference_message();
    fc::View<std::byte const> const longest = prefix(message, kVectorCount - 1);
    SipTag const good = expected_tag(kVectorCount - 1);
    Secret<SipKeyBytes> key = mint_secret<SipKeyBytes>(reference_key());
    for (std::size_t bit = 0; bit < fc::sip_tag_bytes * 8; ++bit) {
        SipTag forged = good;
        std::byte const flip = static_cast<std::byte>(1U << (bit % 8));
        forged.with_element(fc::Index<fc::sip_tag_bytes>::check(bit / 8).expect("a byte of the tag"),
                            [flip](std::byte& held) noexcept { held ^= flip; });
        auto [kept, accepted] = sh::verify(std::move(key), longest, forged);
        if (accepted) return 20;
        key = std::move(kept);
    }
    return 0;
}

// The right tag does not authenticate a changed message, and it does not
// authenticate the message under a different key.
int check_other_message_and_key() {
    fc::FixedArray<std::byte, kVectorCount> changed = reference_message();
    changed.with_element(fc::index<0>(), [](std::byte& held) noexcept { held ^= std::byte{1}; });
    SipTag const good = expected_tag(kVectorCount - 1);

    auto [kept, accepted_changed] =
        sh::verify(mint_secret<SipKeyBytes>(reference_key()), prefix(changed, kVectorCount - 1), good);
    if (accepted_changed) return 30;

    fc::FixedArray<std::byte, kVectorCount> const message = reference_message();
    SipKeyBytes other = reference_key();
    other.with_element(fc::index<0>(), [](std::byte& held) noexcept { held ^= std::byte{1}; });
    auto [other_kept, accepted_other_key] =
        sh::verify(mint_secret<SipKeyBytes>(other), prefix(message, kVectorCount - 1), good);
    if (accepted_other_key) return 31;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_every_vector(); rc != 0) return rc;
    if (int rc = check_each_forged_bit(); rc != 0) return rc;
    if (int rc = check_other_message_and_key(); rc != 0) return rc;
    return 0;
}
