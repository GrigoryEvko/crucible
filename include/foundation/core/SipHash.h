#pragma once

// SipHash-2-4, the keyed hash of Aumasson and Bernstein (2012): a tag of 8
// bytes of a message under a key of 16 bytes.  The key and the tag use the
// byte order of the reference implementation, so its published test vectors
// apply with no conversion.
//
//   SipKeyBytes             the 16 bytes of a key
//   SipTag                  the 8 bytes of a tag
//   sip_hash(key, bytes)    the tag of the bytes under the key
//   sip_hash(key, text)     the tag of the characters of a text, one byte each
//
// This header is the hash over a raw key.  fixy/SipHash.h holds a classified
// key in a fixy::Secret and gives the message authentication code.  A caller
// with a fixed public key, such as the registry of foundation/ThreadLocalRef.h,
// calls sip_hash directly.  SipHash-2-4 claims the security of a pseudorandom
// function under a secret key.  Under a known key it is only a fixed function
// of the message: a check against an accident, never against a peer that
// chooses its input.  The key bytes are a value and no passkey, so the name of
// their class does not end with Key.
//
// Each step is an addition, a rotation or an exclusive or of 64-bit words.  No
// branch and no memory access depends on the key or on the message bytes.  The
// number of rounds depends on the message length, which is public.  The hash
// clears each copy of the key in its frame before it returns
// (foundation/core/Zeroize.h).
//
// Complexity: one absorb of two rounds for each 8 bytes of the message, one for
// the tail, and four rounds of finalisation.

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/core/Zeroize.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core {

inline constexpr std::size_t sip_key_bytes = 16;
inline constexpr std::size_t sip_tag_bytes = 8;

// The 16 bytes of a key and the 8 bytes of a tag.
using SipKeyBytes = FixedArray<std::byte, sip_key_bytes>;
using SipTag = FixedArray<std::byte, sip_tag_bytes>;

namespace detail {

// The initial state is the ASCII text "somepseudorandomlygeneratedbytes",
// read as four little-endian words.
inline constexpr std::uint64_t sip_initial_word0 = 0x736f6d6570736575ULL;
inline constexpr std::uint64_t sip_initial_word1 = 0x646f72616e646f6dULL;
inline constexpr std::uint64_t sip_initial_word2 = 0x6c7967656e657261ULL;
inline constexpr std::uint64_t sip_initial_word3 = 0x7465646279746573ULL;

// The finalisation marks the state with this constant before its four
// rounds, so the last message block and the output cannot share one state.
inline constexpr std::uint64_t sip_finalisation_mark = 0xffULL;

inline constexpr int sip_compression_rounds = 2;
inline constexpr int sip_finalisation_rounds = 4;

// The four state words are v0 to v3 of the paper.
struct SipState final {
    std::uint64_t word0 = 0;
    std::uint64_t word1 = 0;
    std::uint64_t word2 = 0;
    std::uint64_t word3 = 0;
};

[[nodiscard]] constexpr std::uint64_t sip_rotl_(std::uint64_t word, int count) noexcept {
    return (word << count) | (word >> (64 - count));
}

// One SipRound of the reference implementation.
constexpr void sip_round_(SipState& state) noexcept {
    state.word0 += state.word1;
    state.word1 = sip_rotl_(state.word1, 13);
    state.word1 ^= state.word0;
    state.word0 = sip_rotl_(state.word0, 32);
    state.word2 += state.word3;
    state.word3 = sip_rotl_(state.word3, 16);
    state.word3 ^= state.word2;
    state.word0 += state.word3;
    state.word3 = sip_rotl_(state.word3, 21);
    state.word3 ^= state.word0;
    state.word2 += state.word1;
    state.word1 = sip_rotl_(state.word1, 17);
    state.word1 ^= state.word2;
    state.word2 = sip_rotl_(state.word2, 32);
}

constexpr void sip_absorb_(SipState& state, std::uint64_t block) noexcept {
    state.word3 ^= block;
    for (int round = 0; round < sip_compression_rounds; ++round) {
        sip_round_(state);
    }
    state.word0 ^= block;
}

// The tag of the elements of a message, each element one byte, under the
// key.  Each 8 elements are one little-endian word, and the last block
// holds the tail and, in its top byte, the length modulo 256.
template <class Message>
[[nodiscard]] constexpr SipTag sip_hash_of_(SipKeyBytes const& key, Message const& message,
                                            std::size_t length) noexcept {
    std::uint64_t key_low = 0;
    std::uint64_t key_high = 0;
    std::size_t at = 0;
    for (std::byte const byte : key) {
        std::uint64_t const word = static_cast<std::uint64_t>(static_cast<std::uint8_t>(byte));
        if (at < 8) {
            key_low |= word << (8 * at);
        } else {
            key_high |= word << (8 * (at - 8));
        }
        ++at;
    }
    SipState state{.word0 = sip_initial_word0 ^ key_low,
                   .word1 = sip_initial_word1 ^ key_high,
                   .word2 = sip_initial_word2 ^ key_low,
                   .word3 = sip_initial_word3 ^ key_high};

    std::uint64_t block = 0;
    std::size_t filled = 0;
    for (auto const element : message) {
        block |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(element)) << (8 * filled);
        ++filled;
        if (filled == 8) {
            sip_absorb_(state, block);
            block = 0;
            filled = 0;
        }
    }
    sip_absorb_(state, (static_cast<std::uint64_t>(length) << 56) | block);

    state.word2 ^= sip_finalisation_mark;
    for (int round = 0; round < sip_finalisation_rounds; ++round) {
        sip_round_(state);
    }
    std::uint64_t const out = state.word0 ^ state.word1 ^ state.word2 ^ state.word3;

    // The two words of the key and the state end here.  The state holds the
    // key under a public constant, so each of the three is a copy of the key.
    zeroize_bytes(key_low);
    zeroize_bytes(key_high);
    zeroize_bytes(state);

    SipTag tag{};
    int shift = 0;
    for (std::byte& byte : tag) {
        byte = static_cast<std::byte>(static_cast<std::uint8_t>(out >> shift));
        shift += 8;
    }
    return tag;
}

}  // namespace detail

// The tag of the bytes of the message under the key.
[[nodiscard]] constexpr SipTag sip_hash(SipKeyBytes const& key, View<std::byte const> message) noexcept {
    return detail::sip_hash_of_(key, message, message.size());
}

// The tag of the characters of the text under the key, each character one
// byte.
[[nodiscard]] constexpr SipTag sip_hash(SipKeyBytes const& key, TextView text) noexcept {
    return detail::sip_hash_of_(key, text, text.size());
}

}  // namespace foundation::core
