#pragma once

// The keyed hash of the base: BLAKE3 in its keyed mode (O'Connor, Aumasson,
// Neves and Wilcox-O'Hearn, 2020), with its native output of 32 bytes.
//
//   KeyedHashKeyBytes     the 32 bytes of a key, as 8 little-endian words.  It
//                         is a value and no passkey, so its name does not end
//                         with Key
//   Hash256               the 32 bytes of an output, as 4 words of 64 bits,
//                         each little-endian, the first bytes in word 0
//   keyed_hash(key, m)    the Hash256 of the bytes m under the key
//
// Choice.  A key of a federation of caches crosses a process boundary
// between organizations.  A member of the federation holds the key and can
// choose the input.  Under a known key the keyed mode of BLAKE3 stays a hash
// with collision resistance: 32 bytes of output give 128 bits of it against
// the birthday bound, where 16 bytes give 64 bits, which a member with
// resources can reach.  SipHash-2-4 claims only the security of a
// pseudorandom function, which says nothing once the key is known.  An id
// inside one process stays the 64-bit hash of foundation/core/Protocol.h.
//
// Algorithm.  The message divides into chunks of 1024 bytes and each
// chunk into blocks of 64 bytes.  The compression takes a chaining value of
// 8 words, a block of 16 words, a counter, the count of bytes of the block
// and the flags, and it runs 7 rounds of 8 applications of g.  The chaining
// values of the chunks join in a binary tree whose left subtree is full and
// holds a power of two of chunks.  The root takes the root flag.  Each word is
// little-endian and each step is an add, an xor or a rotation of 32 bits, so
// the output is the same on each target and at each ISA level.
//
// Arms.  A constant evaluation runs the scalar compression.  At run time
// a message of one chunk runs in the rows: the compression holds the 4 rows
// of the state in vectors of 4 words, the columns in one step and the
// diagonals after a rotation of three rows.  A longer message runs its chunks
// and then each level of their parents, up to the root.  Many inputs run in
// the lanes: a vector holds one word of the state of each of 4, 8 or 16
// inputs, as wide as the target holds, so the inputs run in parallel where
// one input waits for each step of its rounds.  Two to four inputs run in
// stacked rows: the rows of each input in their own block of 128 bits of one
// vector.  A short last chunk runs its full blocks beside the other chunks
// and its last block alone.  With AVX-512 a rotation is one instruction, with
// SSSE3 or NEON a rotation by 8 is one shuffle of bytes, a rotation by 16 is
// one shuffle, and else a rotation is two shifts and an or.  Each arm gives
// the bits of the scalar form.  The run-time arm is in
// src/foundation/core/KeyedHash.cpp, so a unit that includes this header
// compiles only the scalar arm.  The run-time arm instantiates wide vector
// code, and a build with a sanitizer compiles it in that one source and not
// in each unit that hashes a key.
//
// Entry.  The library gives the run-time arm through one symbol,
// detail::compute_keyed_hash_, whose name holds no target.  So a unit of each
// x86 level links a call of keyed_hash, and the call runs the arm of the level
// of the library (the target namespace in foundation/Platform.h).  The call
// is direct, with no table and no pointer: the loader picks the library of the
// level of the host.
//
// Complexity: one compression for each 64 bytes of the message and one for
// each parent node.  The tree holds at most 54 chaining values, one for each
// bit of the count of chunks, and a message of one chunk uses no tree.

#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Region.h>

#include <compare>
#include <cstddef>
#include <cstdint>

namespace foundation::core {

// The 32 bytes of a key of keyed_hash.
class KeyedHashKeyBytes final {
public:
    // The key of 32 bytes, in the byte order of the reference: each 4 bytes
    // are one little-endian word.
    constexpr explicit KeyedHashKeyBytes(View<std::byte const, 32> bytes) noexcept {
        std::byte const* const first = first_of_(detail::BaseLayerRead{}, bytes);
        for (std::size_t index = 0; index < 32; ++index) {
            words_[index / 4] |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(first[index]))
                              << (8 * (index % 4));
        }
    }

    // The word at index: the bytes 4 * index to 4 * index + 3 of the key.
    [[nodiscard]] constexpr std::uint32_t word(Index<8> index) const noexcept { return words_[index.raw()]; }

private:
    std::uint32_t words_[8] = {};
};

// The 32 bytes of an output of keyed_hash.
class Hash256 final {
public:
    constexpr Hash256(std::uint64_t first, std::uint64_t second, std::uint64_t third, std::uint64_t fourth) noexcept
        : words_{first, second, third, fourth} {}

    // The word at index: the bytes 8 * index to 8 * index + 7 of the output.
    [[nodiscard]] constexpr std::uint64_t word(Index<4> index) const noexcept { return words_.at(index); }

    // The 32 bytes of the output, in the byte order of the reference.
    [[nodiscard]] constexpr FixedArray<std::byte, 32> bytes() const noexcept {
        FixedArray<std::byte, 32> out{};
        out.with_mut([this](auto run) noexcept {
            auto cursor = run.begin();
            for (std::uint64_t const word : words_) {
                for (int shift = 0; shift < 64; shift += 8) {
                    *cursor = static_cast<std::byte>(static_cast<std::uint8_t>(word >> shift));
                    ++cursor;
                }
            }
        });
        return out;
    }

    [[nodiscard]] friend constexpr bool operator==(Hash256 const& left, Hash256 const& right) noexcept = default;
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Hash256 const& left,
                                                                    Hash256 const& right) noexcept = default;

private:
    FixedArray<std::uint64_t, 4> words_{};
};

namespace detail {

inline constexpr std::uint32_t blake3_iv[8] = {0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
                                               0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};

// The flags of a compression.
inline constexpr std::uint32_t blake3_chunk_start = 1U << 0;
inline constexpr std::uint32_t blake3_chunk_end = 1U << 1;
inline constexpr std::uint32_t blake3_parent = 1U << 2;
inline constexpr std::uint32_t blake3_root = 1U << 3;
inline constexpr std::uint32_t blake3_keyed_hash = 1U << 4;

inline constexpr std::size_t blake3_block_bytes = 64;
inline constexpr std::size_t blake3_chunk_bytes = 1024;
inline constexpr std::size_t blake3_max_depth = 54;

// The order of the words of the message in each round: the permutation of
// the message applied round times.
inline constexpr std::uint8_t blake3_schedule[7][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1}, {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4}, {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
    {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13}};

// The input of one compression.
struct Blake3Block final {
    std::uint32_t words[16] = {};
    std::uint64_t counter = 0;
    std::uint32_t bytes = 0;
    std::uint32_t flags = 0;
};

[[nodiscard]] constexpr std::uint32_t blake3_rotr_(std::uint32_t word, int count) noexcept {
    return (word >> count) | (word << (32 - count));
}

constexpr void blake3_g_(std::uint32_t (&state)[16], int first, int second, int third, int fourth, std::uint32_t mix_x,
                         std::uint32_t mix_y) noexcept {
    state[first] = state[first] + state[second] + mix_x;
    state[fourth] = blake3_rotr_(state[fourth] ^ state[first], 16);
    state[third] = state[third] + state[fourth];
    state[second] = blake3_rotr_(state[second] ^ state[third], 12);
    state[first] = state[first] + state[second] + mix_y;
    state[fourth] = blake3_rotr_(state[fourth] ^ state[first], 8);
    state[third] = state[third] + state[fourth];
    state[second] = blake3_rotr_(state[second] ^ state[third], 7);
}

// The scalar compression: the 16 words of its output.
constexpr void blake3_compress_scalar_(std::uint32_t const (&value)[8], Blake3Block const& block,
                                       std::uint32_t (&out)[16]) noexcept {
    std::uint32_t state[16] = {value[0],
                               value[1],
                               value[2],
                               value[3],
                               value[4],
                               value[5],
                               value[6],
                               value[7],
                               blake3_iv[0],
                               blake3_iv[1],
                               blake3_iv[2],
                               blake3_iv[3],
                               static_cast<std::uint32_t>(block.counter),
                               static_cast<std::uint32_t>(block.counter >> 32),
                               block.bytes,
                               block.flags};
    for (auto const& order : blake3_schedule) {
        auto const word = [&block, &order](int at) noexcept { return block.words[order[at]]; };
        blake3_g_(state, 0, 4, 8, 12, word(0), word(1));
        blake3_g_(state, 1, 5, 9, 13, word(2), word(3));
        blake3_g_(state, 2, 6, 10, 14, word(4), word(5));
        blake3_g_(state, 3, 7, 11, 15, word(6), word(7));
        blake3_g_(state, 0, 5, 10, 15, word(8), word(9));
        blake3_g_(state, 1, 6, 11, 12, word(10), word(11));
        blake3_g_(state, 2, 7, 8, 13, word(12), word(13));
        blake3_g_(state, 3, 4, 9, 14, word(14), word(15));
    }
    for (int index = 0; index < 8; ++index) {
        out[index] = state[index] ^ state[index + 8];
        out[index + 8] = state[index + 8] ^ value[index];
    }
}

// The keyed hash of the bytes of message under the key, at run time (the
// entry): the rows for a message of one chunk and the lanes for a longer one,
// in src/foundation/core/KeyedHash.cpp.  A unit that includes this header
// compiles only the scalar arm of a constant evaluation.
[[nodiscard]] Hash256 compute_keyed_hash_(KeyedHashKeyBytes const& key, View<std::byte const> message) noexcept;

// The 16 words of a block of count bytes from first, with zero bytes after
// them.  At run time on a little-endian host the words are the bytes.
constexpr void blake3_load_block_(std::byte const* first, std::size_t count, std::uint32_t (&words)[16]) noexcept {
    if !consteval {
        if constexpr (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) {
            unsigned char bytes[blake3_block_bytes] = {};
            __builtin_memcpy(bytes, first, count);
            __builtin_memcpy(words, bytes, blake3_block_bytes);
            return;
        }
    }
    for (std::uint32_t& word : words) {
        word = 0;
    }
    for (std::size_t index = 0; index < count; ++index) {
        words[index / 4] |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(first[index])) << (8 * (index % 4));
    }
}

// The chaining value of a full chunk at the index chunk.
constexpr void blake3_chunk_value_(std::uint32_t const (&key)[8], std::byte const* first, std::uint64_t chunk,
                                   std::uint32_t (&value)[8]) noexcept {
    for (int index = 0; index < 8; ++index) {
        value[index] = key[index];
    }
    constexpr std::size_t blocks = blake3_chunk_bytes / blake3_block_bytes;
    for (std::size_t at = 0; at < blocks; ++at) {
        Blake3Block block{};
        blake3_load_block_(first + at * blake3_block_bytes, blake3_block_bytes, block.words);
        block.counter = chunk;
        block.bytes = blake3_block_bytes;
        block.flags =
            blake3_keyed_hash | (at == 0 ? blake3_chunk_start : 0) | (at + 1 == blocks ? blake3_chunk_end : 0);
        std::uint32_t out[16] = {};
        blake3_compress_scalar_(value, block, out);
        for (int index = 0; index < 8; ++index) {
            value[index] = out[index];
        }
    }
}

// The block of the parent of two chaining values.
constexpr void blake3_parent_block_(std::uint32_t const (&left)[8], std::uint32_t const (&right)[8],
                                    Blake3Block& block) noexcept {
    for (int index = 0; index < 8; ++index) {
        block.words[index] = left[index];
        block.words[index + 8] = right[index];
    }
    block.counter = 0;
    block.bytes = blake3_block_bytes;
    block.flags = blake3_keyed_hash | blake3_parent;
}

// The tree of a message of more than one chunk.  It joins the chaining
// values of the chunks before the last into a stack, and then joins each
// value of the stack from the left with the output node of the last chunk,
// whose chaining value and block it takes and gives back.  A message of one
// chunk makes no call, so it writes no stack.  Linear in the count of
// chunks.
[[gnu::noinline]] constexpr void blake3_join_tree_(std::uint32_t const (&key)[8], std::byte const* first,
                                                   std::size_t chunks, std::uint32_t (&value)[8],
                                                   Blake3Block& node) noexcept {
    std::uint32_t stack[blake3_max_depth][8] = {};
    std::size_t depth = 0;
    for (std::uint64_t chunk = 0; chunk < chunks; ++chunk) {
        std::uint32_t chunk_value[8] = {};
        blake3_chunk_value_(key, first + chunk * blake3_chunk_bytes, chunk, chunk_value);
        for (std::uint64_t total = chunk + 1; (total & 1U) == 0; total >>= 1) {
            Blake3Block block{};
            blake3_parent_block_(stack[depth - 1], chunk_value, block);
            --depth;
            std::uint32_t out[16] = {};
            blake3_compress_scalar_(key, block, out);
            for (int index = 0; index < 8; ++index) {
                chunk_value[index] = out[index];
            }
        }
        for (int index = 0; index < 8; ++index) {
            stack[depth][index] = chunk_value[index];
        }
        ++depth;
    }
    while (depth != 0) {
        std::uint32_t out[16] = {};
        blake3_compress_scalar_(value, node, out);
        std::uint32_t right[8] = {};
        for (int index = 0; index < 8; ++index) {
            right[index] = out[index];
            value[index] = key[index];
        }
        blake3_parent_block_(stack[depth - 1], right, node);
        --depth;
    }
}

}  // namespace detail

// The keyed hash of the bytes of message under the key (the algorithm):
// BLAKE3 in its keyed mode, with 32 bytes of output.  Complexity: one
// compression for each 64 bytes of the message and one for each parent.
[[nodiscard]] constexpr Hash256 keyed_hash(KeyedHashKeyBytes const& key, View<std::byte const> message) noexcept {
    if !consteval {
        return detail::compute_keyed_hash_(key, message);
    }
    std::byte const* const first = first_of_(detail::BaseLayerRead{}, message);
    std::size_t const count = message.size();
    std::uint32_t words[8] = {};
    for (auto const index : IndexRange<8>{}) {
        words[index.raw()] = key.word(index);
    }
    std::size_t const full_chunks = count == 0 ? 0 : (count - 1) / detail::blake3_chunk_bytes;

    // The last chunk: its blocks but the last, and the last block as the
    // input of the output node.
    std::size_t const at = full_chunks * detail::blake3_chunk_bytes;
    std::size_t const rest = count - at;
    std::size_t const blocks = rest == 0 ? 1 : (rest + detail::blake3_block_bytes - 1) / detail::blake3_block_bytes;
    std::uint32_t value[8] = {};
    for (int index = 0; index < 8; ++index) {
        value[index] = words[index];
    }
    for (std::size_t block_index = 0; block_index + 1 < blocks; ++block_index) {
        detail::Blake3Block block{};
        detail::blake3_load_block_(first + at + block_index * detail::blake3_block_bytes, detail::blake3_block_bytes,
                                   block.words);
        block.counter = full_chunks;
        block.bytes = detail::blake3_block_bytes;
        block.flags = detail::blake3_keyed_hash | (block_index == 0 ? detail::blake3_chunk_start : 0);
        std::uint32_t out[16] = {};
        detail::blake3_compress_scalar_(value, block, out);
        for (int index = 0; index < 8; ++index) {
            value[index] = out[index];
        }
    }
    std::size_t const last = rest - (blocks - 1) * detail::blake3_block_bytes;
    detail::Blake3Block node{};
    detail::blake3_load_block_(first + at + (blocks - 1) * detail::blake3_block_bytes, last, node.words);
    node.counter = full_chunks;
    node.bytes = static_cast<std::uint32_t>(last);
    node.flags = detail::blake3_keyed_hash | (blocks == 1 ? detail::blake3_chunk_start : 0) | detail::blake3_chunk_end;

    if (full_chunks != 0) detail::blake3_join_tree_(words, first, full_chunks, value, node);
    node.flags |= detail::blake3_root;
    node.counter = 0;
    std::uint32_t out[16] = {};
    detail::blake3_compress_scalar_(value, node, out);
    auto const pair = [&out](int at_word) noexcept {
        return static_cast<std::uint64_t>(out[at_word]) | (static_cast<std::uint64_t>(out[at_word + 1]) << 32);
    };
    return Hash256{pair(0), pair(2), pair(4), pair(6)};
}

}  // namespace foundation::core
