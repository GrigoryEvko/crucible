#pragma once

// SipHash-2-4, the keyed hash of Aumasson and Bernstein (2012), used as a
// message authentication code.  The key is 16 bytes and the tag is 8 bytes.
// Both use the byte order of the reference implementation, so its published
// test vectors apply with no conversion.
//
// The key is a classified value.  It stays in a fixy::Secret, and each
// operation takes the key and gives it back beside its result.  The key
// leaves the Secret only inside transform(), for the duration of one hash.
//
// A tag leaves classification through HashForCompare: a tag is a hash of
// the message under the key, and it does not release the key.  verify()
// compares the tag it computes with fixy::ct::eq, which reads every byte of
// the two tags.  A refusal then takes the same time wherever the first
// wrong byte is.
//
// Every step is an addition, a rotation or an exclusive or on 64-bit words.
// No branch and no memory access depends on the key or on the message
// bytes.  The number of rounds depends on the message length, which is
// public.

#include <fixy/ConstantTime.h>
#include <fixy/Secret.h>
#include <fixy/Tags.h>
#include <foundation/contracts/Pre.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace fixy::siphash {

inline constexpr std::size_t key_bytes = 16;
inline constexpr std::size_t tag_bytes = 8;

using Key = std::array<std::byte, key_bytes>;
using Tag = std::array<std::byte, tag_bytes>;

namespace detail::siphash {

inline constexpr std::size_t word_bytes = 8;

// The initial state is the ASCII text "somepseudorandomlygeneratedbytes",
// read as four little-endian words.
inline constexpr std::uint64_t initial_word0 = 0x736f6d6570736575ULL;
inline constexpr std::uint64_t initial_word1 = 0x646f72616e646f6dULL;
inline constexpr std::uint64_t initial_word2 = 0x6c7967656e657261ULL;
inline constexpr std::uint64_t initial_word3 = 0x7465646279746573ULL;

// The finalisation marks the state with this constant before its four
// rounds, so the last message block and the output cannot share one state.
inline constexpr std::uint64_t finalisation_mark = 0xffULL;

inline constexpr int compression_rounds = 2;
inline constexpr int finalisation_rounds = 4;

// Reads at most eight bytes as one little-endian word.  A short span fills
// the low bytes and leaves the high bytes zero.
[[nodiscard]] constexpr std::uint64_t load_le64(std::span<const std::byte> bytes) noexcept {
    CRUCIBLE_PRE(bytes.size() <= word_bytes);
    std::uint64_t word = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        word |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[i])) << (8 * i);
    }
    return word;
}

// The four state words are v0 to v3 of the paper.
struct SipState {
    std::uint64_t word0 = 0;
    std::uint64_t word1 = 0;
    std::uint64_t word2 = 0;
    std::uint64_t word3 = 0;
};

// One SipRound of the reference implementation.
constexpr void sip_round(SipState& state) noexcept {
    state.word0 += state.word1;
    state.word1 = std::rotl(state.word1, 13);
    state.word1 ^= state.word0;
    state.word0 = std::rotl(state.word0, 32);
    state.word2 += state.word3;
    state.word3 = std::rotl(state.word3, 16);
    state.word3 ^= state.word2;
    state.word0 += state.word3;
    state.word3 = std::rotl(state.word3, 21);
    state.word3 ^= state.word0;
    state.word2 += state.word1;
    state.word1 = std::rotl(state.word1, 17);
    state.word1 ^= state.word2;
    state.word2 = std::rotl(state.word2, 32);
}

constexpr void absorb(SipState& state, std::uint64_t block) noexcept {
    state.word3 ^= block;
    for (int round = 0; round < compression_rounds; ++round) {
        sip_round(state);
    }
    state.word0 ^= block;
}

// The tag of a message under a raw key.  The cost is O(n) in the message
// length, with one absorb for each eight bytes and one for the tail.
[[nodiscard]] constexpr Tag compute(Key const& key, std::span<const std::byte> message) noexcept {
    std::span<const std::byte, key_bytes> const key_view{key};
    std::uint64_t const key_low = load_le64(key_view.first<word_bytes>());
    std::uint64_t const key_high = load_le64(key_view.last<word_bytes>());
    SipState state{.word0 = initial_word0 ^ key_low,
                   .word1 = initial_word1 ^ key_high,
                   .word2 = initial_word2 ^ key_low,
                   .word3 = initial_word3 ^ key_high};

    std::size_t const full_blocks = message.size() / word_bytes;
    for (std::size_t block = 0; block < full_blocks; ++block) {
        absorb(state, load_le64(message.subspan(block * word_bytes, word_bytes)));
    }

    // The last block holds the tail bytes and, in its top byte, the message
    // length modulo 256.
    std::uint64_t const length_in_top_byte = std::uint64_t{message.size()} << 56;
    absorb(state, length_in_top_byte | load_le64(message.subspan(full_blocks * word_bytes)));

    state.word2 ^= finalisation_mark;
    for (int round = 0; round < finalisation_rounds; ++round) {
        sip_round(state);
    }
    std::uint64_t const out = state.word0 ^ state.word1 ^ state.word2 ^ state.word3;

    Tag tag{};
    for (std::size_t i = 0; i < tag_bytes; ++i) {
        tag[i] = static_cast<std::byte>(static_cast<std::uint8_t>(out >> (8 * i)));
    }
    return tag;
}

}  // namespace detail::siphash

// Computes the tag of the message and gives the key back beside it.
[[nodiscard]] constexpr std::pair<Secret<Key>, Tag> sign(Secret<Key>&& key, std::span<const std::byte> message) noexcept {
    Secret<Tag> tag = mint_secret<Tag>();
    Secret<Key> kept = std::move(key).transform([&](Key raw) noexcept {
        tag = mint_secret<Tag>(detail::siphash::compute(raw, message));
        return raw;
    });
    return {std::move(kept), std::move(tag).declassify<tags::secret_policy::HashForCompare>()};
}

// Gives the key back beside the answer to one question: does the tag
// authenticate the message under this key?
[[nodiscard]] constexpr std::pair<Secret<Key>, bool> verify(Secret<Key>&& key, std::span<const std::byte> message,
                                                           Tag const& tag) noexcept {
    Secret<bool> matches = mint_secret<bool>(false);
    Secret<Key> kept = std::move(key).transform([&](Key raw) noexcept {
        Tag const computed = detail::siphash::compute(raw, message);
        matches = mint_secret<bool>(ct::eq(std::span<const std::byte, tag_bytes>{computed},
                                           std::span<const std::byte, tag_bytes>{tag}));
        return raw;
    });
    return {std::move(kept), std::move(matches).declassify<tags::secret_policy::HashForCompare>()};
}

}  // namespace fixy::siphash
