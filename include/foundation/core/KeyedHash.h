#pragma once

// The keyed hash of the base: BLAKE3 in its keyed mode (O'Connor, Aumasson,
// Neves and Wilcox-O'Hearn, 2020), with its native output of 32 bytes.
//
//   KeyedHashKey          the 32 bytes of a key, as 8 little-endian words
//   Hash256               the 32 bytes of an output, as 4 words of 64 bits,
//                         each little-endian, the first bytes in word 0
//   keyed_hash(key, m)    the Hash256 of the bytes m under the key
//
// THE CHOICE.  A key of a federation of caches crosses a process boundary
// between organizations.  A member of the federation holds the key and can
// choose the input.  Under a known key the keyed mode of BLAKE3 stays a hash
// with collision resistance: 32 bytes of output give 128 bits of it against
// the birthday bound, where 16 bytes give 64 bits, which a member with
// resources can reach.  SipHash-2-4 claims only the security of a
// pseudorandom function, which says nothing once the key is known.  An id
// inside one process stays the 64-bit hash of foundation/core/Protocol.h.
//
// THE ALGORITHM.  The message divides into chunks of 1024 bytes and each
// chunk into blocks of 64 bytes.  The compression takes a chaining value of
// 8 words, a block of 16 words, a counter, the count of bytes of the block
// and the flags, and it runs 7 rounds of 8 applications of g.  The chaining
// values of the chunks join in a binary tree whose left subtree is full and
// holds a power of two of chunks.  The root takes the flag ROOT.  Each word is
// little-endian and each step is an add, an xor or a rotation of 32 bits, so
// the output is the same on each target and at each ISA level.
//
// THE ARMS.  A constant evaluation runs the scalar compression.  At run time
// a message of one chunk runs in the rows: the compression holds the 4 rows
// of the state in vectors of 4 words, the columns in one step and the
// diagonals after a rotation of the lanes.  A longer message runs in the
// lanes: a vector holds one word of the state of each of 4, 8 or 16 inputs,
// as wide as the target holds, so the inputs run in parallel where one input
// waits for each step of its rounds.  The inputs are the chunks, the full
// blocks of the last chunk before its output block, and each level of the
// parents of the full subtrees.  With AVX-512 a rotation is one instruction,
// with SSSE3 or NEON a rotation by 16 or by 8 is one shuffle of bytes, and
// else it is two shifts and an or.  Each arm gives the bits of the scalar
// form.
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
class KeyedHashKey final {
public:
    // The key of 32 bytes, in the byte order of the reference: each 4 bytes
    // are one little-endian word.
    constexpr explicit KeyedHashKey(View<std::byte const, 32> bytes) noexcept {
        std::byte const* const first = first_of_(detail::ViewDoor::key(), bytes);
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

// The run-time compression follows the vector instructions of the target, so
// it is in the target namespace (THE TARGET NAMESPACE of
// foundation/Platform.h).
inline namespace CRUCIBLE_SIMD_TARGET {

// Four words of 32 bits, a row of the state.
typedef std::uint32_t Blake3Row __attribute__((vector_size(16)));
typedef std::uint16_t Blake3Halves __attribute__((vector_size(16)));
typedef std::uint8_t Blake3Bytes __attribute__((vector_size(16)));

template <int Count>
[[nodiscard]] inline Blake3Row blake3_rotr_row_(Blake3Row row) noexcept {
#if defined(__SSSE3__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
    if constexpr (Count == 16) {
        Blake3Halves const halves = __builtin_bit_cast(Blake3Halves, row);
        return __builtin_bit_cast(Blake3Row, __builtin_shufflevector(halves, halves, 1, 0, 3, 2, 5, 4, 7, 6));
    } else if constexpr (Count == 8) {
        Blake3Bytes const bytes = __builtin_bit_cast(Blake3Bytes, row);
        return __builtin_bit_cast(
            Blake3Row, __builtin_shufflevector(bytes, bytes, 1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12));
    }
#endif
    return (row >> Count) | (row << (32 - Count));
}

inline void blake3_g_rows_(Blake3Row& first, Blake3Row& second, Blake3Row& third, Blake3Row& fourth, Blake3Row mix_x,
                           Blake3Row mix_y) noexcept {
    first = first + second + mix_x;
    fourth = blake3_rotr_row_<16>(fourth ^ first);
    third = third + fourth;
    second = blake3_rotr_row_<12>(second ^ third);
    first = first + second + mix_y;
    fourth = blake3_rotr_row_<8>(fourth ^ first);
    third = third + fourth;
    second = blake3_rotr_row_<7>(second ^ third);
}

// The compression with the 4 rows of the state as vectors: the columns, then
// the diagonals after a rotation of the lanes of the rows 1 to 3.  Between
// the rounds the message takes the permutation 2 6 3 10 7 0 4 13 1 11 12 5 9
// 14 15 8 by shuffles of its 4 vectors.  It gives the bits of
// blake3_compress_scalar_.
inline void blake3_compress_rows_(std::uint32_t const (&value)[8], Blake3Block const& block,
                                  std::uint32_t (&out)[16]) noexcept {
    Blake3Row const low_value{value[0], value[1], value[2], value[3]};
    Blake3Row const high_value{value[4], value[5], value[6], value[7]};
    Blake3Row row0 = low_value;
    Blake3Row row1 = high_value;
    Blake3Row row2{blake3_iv[0], blake3_iv[1], blake3_iv[2], blake3_iv[3]};
    Blake3Row row3{static_cast<std::uint32_t>(block.counter), static_cast<std::uint32_t>(block.counter >> 32),
                   block.bytes, block.flags};
    Blake3Row word0{block.words[0], block.words[1], block.words[2], block.words[3]};
    Blake3Row word1{block.words[4], block.words[5], block.words[6], block.words[7]};
    Blake3Row word2{block.words[8], block.words[9], block.words[10], block.words[11]};
    Blake3Row word3{block.words[12], block.words[13], block.words[14], block.words[15]};
    for (int round = 0; round < 7; ++round) {
        blake3_g_rows_(row0, row1, row2, row3, __builtin_shufflevector(word0, word1, 0, 2, 4, 6),
                       __builtin_shufflevector(word0, word1, 1, 3, 5, 7));
        row1 = __builtin_shufflevector(row1, row1, 1, 2, 3, 0);
        row2 = __builtin_shufflevector(row2, row2, 2, 3, 0, 1);
        row3 = __builtin_shufflevector(row3, row3, 3, 0, 1, 2);
        blake3_g_rows_(row0, row1, row2, row3, __builtin_shufflevector(word2, word3, 0, 2, 4, 6),
                       __builtin_shufflevector(word2, word3, 1, 3, 5, 7));
        row1 = __builtin_shufflevector(row1, row1, 3, 0, 1, 2);
        row2 = __builtin_shufflevector(row2, row2, 2, 3, 0, 1);
        row3 = __builtin_shufflevector(row3, row3, 1, 2, 3, 0);
        if (round == 6) break;
        Blake3Row const next0 =
            __builtin_shufflevector(__builtin_shufflevector(word0, word1, 2, 6, 3, -1), word2, 0, 1, 2, 6);
        Blake3Row const next1 =
            __builtin_shufflevector(__builtin_shufflevector(word1, word0, 3, 4, 0, -1), word3, 0, 1, 2, 5);
        Blake3Row const next2 = __builtin_shufflevector(
            __builtin_shufflevector(__builtin_shufflevector(word0, word2, 1, 7, -1, -1), word3, 0, 1, 4, -1), word1, 0,
            1, 2, 5);
        Blake3Row const next3 = __builtin_shufflevector(word2, word3, 1, 6, 7, 0);
        word0 = next0;
        word1 = next1;
        word2 = next2;
        word3 = next3;
    }
    Blake3Row const out0 = row0 ^ row2;
    Blake3Row const out1 = row1 ^ row3;
    Blake3Row const out2 = row2 ^ low_value;
    Blake3Row const out3 = row3 ^ high_value;
    __builtin_memcpy(&out[0], &out0, sizeof(out0));
    __builtin_memcpy(&out[4], &out1, sizeof(out1));
    __builtin_memcpy(&out[8], &out2, sizeof(out2));
    __builtin_memcpy(&out[12], &out3, sizeof(out3));
}

// The compression of the arm: the scalar one in a constant evaluation, and
// the one of the rows at run time.
constexpr void blake3_compress_(std::uint32_t const (&value)[8], Blake3Block const& block,
                                std::uint32_t (&out)[16]) noexcept {
    if consteval {
        blake3_compress_scalar_(value, block, out);
    } else {
        blake3_compress_rows_(value, block, out);
    }
}

// ── The run-time arm ────────────────────────────────────────────────────

// A chaining value as two rows of 4 words.
struct Blake3Cv final {
    Blake3Row low{};
    Blake3Row high{};
};

// The input of the compression of an output node: the chaining value, the
// block as 4 rows, the counter, the count of bytes and the flags.
struct Blake3Node final {
    Blake3Cv cv{};
    Blake3Row words[4] = {};
    std::uint64_t counter = 0;
    std::uint32_t bytes = 0;
    std::uint32_t flags = 0;
};

// One compression of the rows, which gives the chaining value that follows:
// the first 8 words of the output.  The state and the message stay in
// registers.
[[nodiscard, gnu::always_inline]] inline Blake3Cv blake3_compress_cv_(Blake3Cv value, Blake3Row const (&words)[4],
                                                                      std::uint64_t counter, std::uint32_t bytes,
                                                                      std::uint32_t flags) noexcept {
    Blake3Row row0 = value.low;
    Blake3Row row1 = value.high;
    Blake3Row row2{blake3_iv[0], blake3_iv[1], blake3_iv[2], blake3_iv[3]};
    Blake3Row row3{static_cast<std::uint32_t>(counter), static_cast<std::uint32_t>(counter >> 32), bytes, flags};
    Blake3Row word0 = words[0];
    Blake3Row word1 = words[1];
    Blake3Row word2 = words[2];
    Blake3Row word3 = words[3];
    for (int round = 0; round < 7; ++round) {
        blake3_g_rows_(row0, row1, row2, row3, __builtin_shufflevector(word0, word1, 0, 2, 4, 6),
                       __builtin_shufflevector(word0, word1, 1, 3, 5, 7));
        row1 = __builtin_shufflevector(row1, row1, 1, 2, 3, 0);
        row2 = __builtin_shufflevector(row2, row2, 2, 3, 0, 1);
        row3 = __builtin_shufflevector(row3, row3, 3, 0, 1, 2);
        blake3_g_rows_(row0, row1, row2, row3, __builtin_shufflevector(word2, word3, 0, 2, 4, 6),
                       __builtin_shufflevector(word2, word3, 1, 3, 5, 7));
        row1 = __builtin_shufflevector(row1, row1, 3, 0, 1, 2);
        row2 = __builtin_shufflevector(row2, row2, 2, 3, 0, 1);
        row3 = __builtin_shufflevector(row3, row3, 1, 2, 3, 0);
        if (round == 6) break;
        Blake3Row const next0 =
            __builtin_shufflevector(__builtin_shufflevector(word0, word1, 2, 6, 3, -1), word2, 0, 1, 2, 6);
        Blake3Row const next1 =
            __builtin_shufflevector(__builtin_shufflevector(word1, word0, 3, 4, 0, -1), word3, 0, 1, 2, 5);
        Blake3Row const next2 = __builtin_shufflevector(
            __builtin_shufflevector(__builtin_shufflevector(word0, word2, 1, 7, -1, -1), word3, 0, 1, 4, -1), word1, 0,
            1, 2, 5);
        Blake3Row const next3 = __builtin_shufflevector(word2, word3, 1, 6, 7, 0);
        word0 = next0;
        word1 = next1;
        word2 = next2;
        word3 = next3;
    }
    return Blake3Cv{row0 ^ row2, row1 ^ row3};
}

// The 4 rows of a full block from first: its bytes, on a little-endian host.
inline void blake3_load_rows_(std::byte const* first, Blake3Row (&words)[4]) noexcept {
    __builtin_memcpy(&words[0], first, blake3_block_bytes);
}

// The 4 rows of a block of count bytes from first, with zero bytes after
// them.
inline void blake3_load_partial_rows_(std::byte const* first, std::size_t count, Blake3Row (&words)[4]) noexcept {
    unsigned char bytes[blake3_block_bytes] = {};
    __builtin_memcpy(bytes, first, count);
    __builtin_memcpy(&words[0], bytes, blake3_block_bytes);
}

[[nodiscard]] constexpr std::uint32_t blake3_chunk_flags_(std::size_t block) noexcept {
    return blake3_keyed_hash | (block == 0 ? blake3_chunk_start : 0)
         | (block + 1 == blake3_chunk_bytes / blake3_block_bytes ? blake3_chunk_end : 0);
}

// The chaining value of the full chunk at the index chunk.
[[nodiscard]] inline Blake3Cv blake3_chunk_cv_(Blake3Cv key, std::byte const* first, std::uint64_t chunk) noexcept {
    Blake3Cv value = key;
    for (std::size_t block = 0; block < blake3_chunk_bytes / blake3_block_bytes; ++block) {
        Blake3Row words[4];
        blake3_load_rows_(first + block * blake3_block_bytes, words);
        value = blake3_compress_cv_(value, words, chunk, blake3_block_bytes, blake3_chunk_flags_(block));
    }
    return value;
}

[[nodiscard]] inline Blake3Cv blake3_parent_cv_(Blake3Cv key, Blake3Cv left, Blake3Cv right) noexcept {
    Blake3Row const words[4] = {left.low, left.high, right.low, right.high};
    return blake3_compress_cv_(key, words, 0, blake3_block_bytes, blake3_keyed_hash | blake3_parent);
}

// ── The lanes: many chunks at once ──────────────────────────────────────
//
// A vector register holds one word of the state of each of blake3_lanes
// chunks.  The g function then runs on whole registers with no rotation of
// lanes, and the 16 words of a block of each chunk come in by a transpose.

// The widest count of lanes of the target: a register of 512 bits with
// AVX-512, of 256 bits with AVX2, and of 128 bits otherwise.  A narrower
// count serves fewer chunks.
#if defined(__AVX512F__)
inline constexpr std::size_t blake3_widest_lanes = 16;
#elif defined(__AVX2__)
inline constexpr std::size_t blake3_widest_lanes = 8;
#else
inline constexpr std::size_t blake3_widest_lanes = 4;
#endif

// The vector types of Width lanes: a word of 32 bits in each lane, and the
// bytes of such a vector.
template <std::size_t Width>
struct Blake3LaneTypes final {
    typedef std::uint32_t Words __attribute__((vector_size(4 * Width)));
    typedef std::uint8_t Bytes __attribute__((vector_size(4 * Width)));
};

template <std::size_t Width>
using Blake3Lanes = typename Blake3LaneTypes<Width>::Words;

template <std::size_t Width>
using Blake3LaneBytes = typename Blake3LaneTypes<Width>::Bytes;

// The index of the byte at position of a rotation right by Count bits in
// each word, for Count a multiple of 8.
template <int Count>
[[nodiscard]] consteval int blake3_rotr_byte_index_(std::size_t position) noexcept {
    return static_cast<int>((position & ~std::size_t{3}) | ((position + Count / 8) & 3));
}

template <int Count, std::size_t Width, std::size_t... Positions>
[[nodiscard]] inline Blake3Lanes<Width> blake3_rotr_bytes_(Blake3Lanes<Width> lanes,
                                                           std::index_sequence<Positions...>) noexcept {
    Blake3LaneBytes<Width> const bytes = __builtin_bit_cast(Blake3LaneBytes<Width>, lanes);
    return __builtin_bit_cast(Blake3Lanes<Width>,
                              __builtin_shufflevector(bytes, bytes, blake3_rotr_byte_index_<Count>(Positions)...));
}

// A rotation right of each word.  With AVX-512 each rotation is one
// instruction.  Else a rotation by 16 or by 8 is one shuffle of bytes, and
// a rotation by 12 or by 7 is two shifts and an or.
template <int Count, std::size_t Width>
[[nodiscard]] inline Blake3Lanes<Width> blake3_rotr_lanes_(Blake3Lanes<Width> lanes) noexcept {
#if !defined(__AVX512VL__) && (defined(__SSSE3__) || defined(__ARM_NEON) || defined(__ARM_NEON__))
    if constexpr (Count == 16 || Count == 8) {
        return blake3_rotr_bytes_<Count, Width>(lanes, std::make_index_sequence<4 * Width>{});
    }
#endif
    return (lanes >> Count) | (lanes << (32 - Count));
}

template <std::size_t Width>
inline void blake3_g_lanes_(Blake3Lanes<Width> (&state)[16], int first, int second, int third, int fourth,
                            Blake3Lanes<Width> mix_x, Blake3Lanes<Width> mix_y) noexcept {
    state[first] = state[first] + state[second] + mix_x;
    state[fourth] = blake3_rotr_lanes_<16, Width>(state[fourth] ^ state[first]);
    state[third] = state[third] + state[fourth];
    state[second] = blake3_rotr_lanes_<12, Width>(state[second] ^ state[third]);
    state[first] = state[first] + state[second] + mix_y;
    state[fourth] = blake3_rotr_lanes_<8, Width>(state[fourth] ^ state[first]);
    state[third] = state[third] + state[fourth];
    state[second] = blake3_rotr_lanes_<7, Width>(state[second] ^ state[third]);
}

// The lane of an exchange of the blocks of Distance lanes between a row with
// no bit Distance in its index (lower) and the row with it (upper).  The
// exchange swaps one bit of the index of a row with the same bit of the
// index of a lane, so the stages of all bits transpose the rows.
template <std::size_t Width, std::size_t Distance, bool IsUpper>
[[nodiscard]] consteval int blake3_exchange_index_(std::size_t position) noexcept {
    if constexpr (!IsUpper) {
        return static_cast<int>((position & Distance) != 0 ? Width + position - Distance : position);
    } else {
        return static_cast<int>((position & Distance) != 0 ? Width + position : position + Distance);
    }
}

template <std::size_t Width, std::size_t Distance, bool IsUpper, std::size_t... Positions>
[[nodiscard]] inline Blake3Lanes<Width> blake3_exchange_(Blake3Lanes<Width> lower, Blake3Lanes<Width> upper,
                                                         std::index_sequence<Positions...>) noexcept {
    return __builtin_shufflevector(lower, upper, blake3_exchange_index_<Width, Distance, IsUpper>(Positions)...);
}

template <std::size_t Width, std::size_t Distance>
inline void blake3_transpose_stage_(Blake3Lanes<Width> (&rows)[Width]) noexcept {
    if constexpr (Distance < Width) {
        for (std::size_t lower = 0; lower < Width; ++lower) {
            if ((lower & Distance) != 0) continue;
            Blake3Lanes<Width> const low = rows[lower];
            Blake3Lanes<Width> const high = rows[lower + Distance];
            rows[lower] = blake3_exchange_<Width, Distance, false>(low, high, std::make_index_sequence<Width>{});
            rows[lower + Distance] =
                blake3_exchange_<Width, Distance, true>(low, high, std::make_index_sequence<Width>{});
        }
    }
}

// Transposes the rows: lane j of row k moves to lane k of row j.
template <std::size_t Width>
inline void blake3_transpose_(Blake3Lanes<Width> (&rows)[Width]) noexcept {
    blake3_transpose_stage_<Width, 1>(rows);
    blake3_transpose_stage_<Width, 2>(rows);
    blake3_transpose_stage_<Width, 4>(rows);
    blake3_transpose_stage_<Width, 8>(rows);
}

// The 7 rounds over the state of the lanes, with the order of the message
// of each round a constant.
template <std::size_t Width, std::size_t Round>
inline void blake3_round_lanes_(Blake3Lanes<Width> (&state)[16], Blake3Lanes<Width> const (&message)[16]) noexcept {
    constexpr auto const& order = blake3_schedule[Round];
    blake3_g_lanes_<Width>(state, 0, 4, 8, 12, message[order[0]], message[order[1]]);
    blake3_g_lanes_<Width>(state, 1, 5, 9, 13, message[order[2]], message[order[3]]);
    blake3_g_lanes_<Width>(state, 2, 6, 10, 14, message[order[4]], message[order[5]]);
    blake3_g_lanes_<Width>(state, 3, 7, 11, 15, message[order[6]], message[order[7]]);
    blake3_g_lanes_<Width>(state, 0, 5, 10, 15, message[order[8]], message[order[9]]);
    blake3_g_lanes_<Width>(state, 1, 6, 11, 12, message[order[10]], message[order[11]]);
    blake3_g_lanes_<Width>(state, 2, 7, 8, 13, message[order[12]], message[order[13]]);
    blake3_g_lanes_<Width>(state, 3, 4, 9, 14, message[order[14]], message[order[15]]);
}

template <std::size_t Width, std::size_t... Rounds>
inline void blake3_rounds_lanes_(Blake3Lanes<Width> (&state)[16], Blake3Lanes<Width> const (&message)[16],
                                 std::index_sequence<Rounds...>) noexcept {
    (blake3_round_lanes_<Width, Rounds>(state, message), ...);
}

// One input of a run of the lanes: blocks blocks from first, from the key,
// with the counter counter.  Its first block adds start_flags and its last
// block adds end_flags to the flags of the run.  A full chunk has 16 blocks
// with CHUNK_START and CHUNK_END, the full blocks of the last chunk before
// its output block have no CHUNK_END, and a parent has one block.
struct Blake3Input final {
    std::byte const* first = nullptr;
    std::size_t blocks = 0;
    std::uint64_t counter = 0;
    std::uint32_t start_flags = 0;
    std::uint32_t end_flags = 0;
};

// The chaining values of count inputs, at most Width, in the lanes.  A lane
// past count repeats input 0, and a lane past the blocks of its input keeps
// its value and reads its last block again, so no lane reads past its
// input.  The time of a run hardly depends on count: the lanes run in
// parallel, where one input alone waits for each step of its rounds.
// Complexity: one compression of the lanes for each block of the longest
// input.
template <std::size_t Width>
inline void blake3_run_lanes_(Blake3Cv key, Blake3Input const* inputs, std::size_t count, std::uint32_t flags,
                              Blake3Cv* out) noexcept {
    using Lanes = Blake3Lanes<Width>;
    std::uint32_t key_words[8] = {};
    __builtin_memcpy(&key_words[0], &key.low, sizeof(key.low));
    __builtin_memcpy(&key_words[4], &key.high, sizeof(key.high));
    Lanes value[8];
    for (int word = 0; word < 8; ++word) {
        value[word] = Lanes{} + key_words[word];
    }
    Blake3Input lanes[Width];
    std::size_t longest = 0;
    for (std::size_t lane = 0; lane < Width; ++lane) {
        lanes[lane] = inputs[lane < count ? lane : 0];
        longest = longest < lanes[lane].blocks ? lanes[lane].blocks : longest;
    }
    std::uint32_t counter_low[Width] = {};
    std::uint32_t counter_high[Width] = {};
    std::uint32_t start_flags[Width] = {};
    std::uint32_t end_flags[Width] = {};
    std::uint32_t last_block[Width] = {};
    for (std::size_t lane = 0; lane < Width; ++lane) {
        counter_low[lane] = static_cast<std::uint32_t>(lanes[lane].counter);
        counter_high[lane] = static_cast<std::uint32_t>(lanes[lane].counter >> 32);
        start_flags[lane] = lanes[lane].start_flags;
        end_flags[lane] = lanes[lane].end_flags;
        last_block[lane] = static_cast<std::uint32_t>(lanes[lane].blocks - 1);
    }
    Lanes low_lanes;
    Lanes high_lanes;
    Lanes start_lanes;
    Lanes end_lanes;
    Lanes last_lanes;
    __builtin_memcpy(&low_lanes, counter_low, sizeof(Lanes));
    __builtin_memcpy(&high_lanes, counter_high, sizeof(Lanes));
    __builtin_memcpy(&start_lanes, start_flags, sizeof(Lanes));
    __builtin_memcpy(&end_lanes, end_flags, sizeof(Lanes));
    __builtin_memcpy(&last_lanes, last_block, sizeof(Lanes));
    constexpr std::size_t groups = 16 / Width;
    for (std::size_t block = 0; block < longest; ++block) {
        Lanes message[16];
        for (std::size_t group = 0; group < groups; ++group) {
            Lanes rows[Width];
            for (std::size_t lane = 0; lane < Width; ++lane) {
                std::size_t const read = block < lanes[lane].blocks ? block : lanes[lane].blocks - 1;
                __builtin_memcpy(&rows[lane], lanes[lane].first + read * blake3_block_bytes + group * Width * 4,
                                 sizeof(Lanes));
            }
            blake3_transpose_<Width>(rows);
            for (std::size_t word = 0; word < Width; ++word) {
                message[group * Width + word] = rows[word];
            }
        }
        Lanes const block_lanes = Lanes{} + static_cast<std::uint32_t>(block);
        // All ones in a lane whose block is its last, and in a lane that
        // still runs.
        Lanes const is_last = static_cast<Lanes>(block_lanes == last_lanes);
        Lanes const is_running = static_cast<Lanes>(block_lanes <= last_lanes);
        Lanes const block_flags = (Lanes{} + flags) | (block == 0 ? start_lanes : Lanes{}) | (is_last & end_lanes);
        Lanes state[16] = {value[0],
                           value[1],
                           value[2],
                           value[3],
                           value[4],
                           value[5],
                           value[6],
                           value[7],
                           Lanes{} + blake3_iv[0],
                           Lanes{} + blake3_iv[1],
                           Lanes{} + blake3_iv[2],
                           Lanes{} + blake3_iv[3],
                           low_lanes,
                           high_lanes,
                           Lanes{} + static_cast<std::uint32_t>(blake3_block_bytes),
                           block_flags};
        blake3_rounds_lanes_<Width>(state, message, std::make_index_sequence<7>{});
        for (int word = 0; word < 8; ++word) {
            value[word] = ((state[word] ^ state[word + 8]) & is_running) | (value[word] & ~is_running);
        }
    }
    std::uint32_t words[8][Width] = {};
    for (int word = 0; word < 8; ++word) {
        __builtin_memcpy(&words[word][0], &value[word], sizeof(Lanes));
    }
    for (std::size_t lane = 0; lane < count; ++lane) {
        out[lane].low = Blake3Row{words[0][lane], words[1][lane], words[2][lane], words[3][lane]};
        out[lane].high = Blake3Row{words[4][lane], words[5][lane], words[6][lane], words[7][lane]};
    }
}

// The chaining values of count inputs into out: runs of the widest lanes
// while they fill, then one run of the narrowest lanes that hold the rest.
// Fewer inputs than fewest_lanes run one after the other in the rows.
// Complexity: one compression of the lanes for each block of each run.
inline void blake3_run_inputs_(Blake3Cv key, Blake3Input const* inputs, std::size_t count, std::uint32_t flags,
                               Blake3Cv* out) noexcept {
    std::size_t done = 0;
    while (count - done >= blake3_widest_lanes) {
        blake3_run_lanes_<blake3_widest_lanes>(key, inputs + done, blake3_widest_lanes, flags, out + done);
        done += blake3_widest_lanes;
    }
    std::size_t const rest = count - done;
    if (rest == 0) return;
    // With SSE2 alone a rotation by 16 or by 8 is three steps, so two
    // inputs run faster one after the other than in four lanes.
#if defined(__SSSE3__) || defined(__AVX__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
    constexpr std::size_t fewest_lanes = 2;
#else
    constexpr std::size_t fewest_lanes = 3;
#endif
    if (rest < fewest_lanes) {
        for (; done < count; ++done) {
            Blake3Input const& input = inputs[done];
            Blake3Cv value = key;
            for (std::size_t block = 0; block < input.blocks; ++block) {
                Blake3Row words[4];
                blake3_load_rows_(input.first + block * blake3_block_bytes, words);
                std::uint32_t const block_flags =
                    flags | (block == 0 ? input.start_flags : 0) | (block + 1 == input.blocks ? input.end_flags : 0);
                value = blake3_compress_cv_(value, words, input.counter, blake3_block_bytes, block_flags);
            }
            out[done] = value;
        }
        return;
    }
    if (rest <= 4) {
        blake3_run_lanes_<4>(key, inputs + done, rest, flags, out + done);
    } else if constexpr (blake3_widest_lanes >= 8) {
        if (rest <= 8) {
            blake3_run_lanes_<8>(key, inputs + done, rest, flags, out + done);
        } else {
            blake3_run_lanes_<16>(key, inputs + done, rest, flags, out + done);
        }
    }
}

// The most chunks that one batch of the tree hashes: their inputs and
// chaining values are on the stack.
inline constexpr std::size_t blake3_batch_chunks = 64;

// The tree of a message of more than one chunk, at run time.  The chunks
// before the last go in batches of at most blake3_batch_chunks chunks, each
// a run of full subtrees: the subtrees of the binary digits of the count of
// chunks that remain, largest first, each at a multiple of its size.  The
// chunks of a batch run in lanes, the full blocks of the last chunk before
// its output block run with the last batch, and each level of the parents
// of the subtrees of a batch runs in lanes.  The root of each subtree joins
// the stack, and each value of the stack then joins from the left with the
// output node of the last chunk.  Linear in the count of chunks.
[[gnu::noinline]] inline void blake3_join_tree_rows_(Blake3Cv key, std::byte const* first, std::size_t chunks,
                                                     Blake3Node& node, std::size_t prefix_blocks) noexcept {
    Blake3Cv stack[blake3_max_depth] = {};
    std::size_t depth = 0;
    std::uint64_t chunk = 0;
    bool has_prefix = prefix_blocks != 0;
    while (chunk < chunks) {
        // The subtrees of the batch: each the largest power of two that fits
        // what remains, that is at a multiple of its size, and that keeps
        // the batch within its bound.
        std::size_t sizes[8] = {};
        std::size_t subtrees = 0;
        std::size_t batch = 0;
        for (std::uint64_t at = chunk; at < chunks && subtrees < 8 && batch < blake3_batch_chunks;) {
            // Size 1 meets each test, because at is below chunks and the
            // batch is below its bound.
            std::size_t size = blake3_batch_chunks;
            while (size > chunks - at || (at & (size - 1)) != 0 || batch + size > blake3_batch_chunks) {
                size /= 2;
            }
            sizes[subtrees] = size;
            ++subtrees;
            batch += size;
            at += size;
        }
        bool const is_last_batch = chunk + batch == chunks;
        Blake3Input inputs[blake3_batch_chunks + 1];
        for (std::size_t index = 0; index < batch; ++index) {
            inputs[index] = Blake3Input{.first = first + (chunk + index) * blake3_chunk_bytes,
                                        .blocks = blake3_chunk_bytes / blake3_block_bytes,
                                        .counter = chunk + index,
                                        .start_flags = blake3_chunk_start,
                                        .end_flags = blake3_chunk_end};
        }
        std::size_t runs = batch;
        if (is_last_batch && has_prefix) {
            inputs[batch] = Blake3Input{.first = first + chunks * blake3_chunk_bytes,
                                        .blocks = prefix_blocks,
                                        .counter = chunks,
                                        .start_flags = blake3_chunk_start,
                                        .end_flags = 0};
            ++runs;
        }
        Blake3Cv values[blake3_batch_chunks + 1];
        blake3_run_inputs_(key, inputs, runs, blake3_keyed_hash, values);
        if (runs > batch) {
            node.cv = values[batch];
            has_prefix = false;
        }
        // Each level halves each subtree of two or more values.  The
        // subtrees come largest first, each at an even index, so the pairs
        // of a level are the leading values, and the pair of two neighbor
        // values is the 64 bytes of the block of their parent.
        std::size_t left = batch;
        for (std::size_t level_size = 1; left > subtrees; level_size *= 2) {
            std::size_t pairs = 0;
            for (std::size_t index = 0; index < subtrees; ++index) {
                if (sizes[index] > level_size) pairs += sizes[index] / level_size / 2;
            }
            Blake3Input parent_inputs[blake3_batch_chunks / 2];
            std::byte const* const pair_bytes = static_cast<std::byte const*>(static_cast<void const*>(&values[0]));
            for (std::size_t index = 0; index < pairs; ++index) {
                parent_inputs[index] = Blake3Input{.first = pair_bytes + index * blake3_block_bytes,
                                                   .blocks = 1,
                                                   .counter = 0,
                                                   .start_flags = 0,
                                                   .end_flags = 0};
            }
            Blake3Cv joined[blake3_batch_chunks / 2];
            blake3_run_inputs_(key, parent_inputs, pairs, blake3_keyed_hash | blake3_parent, joined);
            for (std::size_t index = 0; index < pairs; ++index) {
                values[index] = joined[index];
            }
            for (std::size_t index = 2 * pairs; index < left; ++index) {
                values[index - pairs] = values[index];
            }
            left -= pairs;
        }
        // The root of each subtree joins the stack, as its chunks would.
        for (std::size_t index = 0; index < subtrees; ++index) {
            Blake3Cv value = values[index];
            chunk += sizes[index];
            for (std::uint64_t total = chunk / sizes[index]; (total & 1U) == 0; total >>= 1) {
                value = blake3_parent_cv_(key, stack[depth - 1], value);
                --depth;
            }
            stack[depth] = value;
            ++depth;
        }
    }
    while (depth != 0) {
        Blake3Cv const right = blake3_compress_cv_(node.cv, node.words, node.counter, node.bytes, node.flags);
        node.cv = key;
        node.words[0] = stack[depth - 1].low;
        node.words[1] = stack[depth - 1].high;
        node.words[2] = right.low;
        node.words[3] = right.high;
        node.counter = 0;
        node.bytes = blake3_block_bytes;
        node.flags = blake3_keyed_hash | blake3_parent;
        --depth;
    }
}

// The keyed hash of count bytes from first, at run time.  A message of one
// chunk runs in the rows.  A longer message runs its chunks in the lanes, and
// the full blocks of its last chunk run in a lane beside them.
[[nodiscard]] inline Hash256 blake3_keyed_hash_rows_(KeyedHashKey const& key, std::byte const* first,
                                                     std::size_t count) noexcept {
    std::uint32_t key_words[8] = {};
    for (auto const index : IndexRange<8>{}) {
        key_words[index.raw()] = key.word(index);
    }
    Blake3Cv key_cv{};
    __builtin_memcpy(&key_cv.low, &key_words[0], sizeof(key_cv.low));
    __builtin_memcpy(&key_cv.high, &key_words[4], sizeof(key_cv.high));
    std::size_t const full_chunks = count == 0 ? 0 : (count - 1) / blake3_chunk_bytes;
    std::size_t const at = full_chunks * blake3_chunk_bytes;
    std::size_t const rest = count - at;
    std::size_t const blocks = rest == 0 ? 1 : (rest + blake3_block_bytes - 1) / blake3_block_bytes;
    Blake3Node node{};
    node.cv = key_cv;
    if (full_chunks == 0) {
        for (std::size_t block = 0; block + 1 < blocks; ++block) {
            Blake3Row words[4];
            blake3_load_rows_(first + at + block * blake3_block_bytes, words);
            node.cv = blake3_compress_cv_(node.cv, words, full_chunks, blake3_block_bytes,
                                          blake3_keyed_hash | (block == 0 ? blake3_chunk_start : 0));
        }
    }
    std::size_t const last = rest - (blocks - 1) * blake3_block_bytes;
    blake3_load_partial_rows_(first + at + (blocks - 1) * blake3_block_bytes, last, node.words);
    node.counter = full_chunks;
    node.bytes = static_cast<std::uint32_t>(last);
    node.flags = blake3_keyed_hash | (blocks == 1 ? blake3_chunk_start : 0) | blake3_chunk_end;
    if (full_chunks != 0) blake3_join_tree_rows_(key_cv, first, full_chunks, node, blocks - 1);
    Blake3Cv const root = blake3_compress_cv_(node.cv, node.words, 0, node.bytes, node.flags | blake3_root);
    std::uint64_t out[4] = {};
    __builtin_memcpy(&out[0], &root.low, sizeof(root.low));
    __builtin_memcpy(&out[2], &root.high, sizeof(root.high));
    return Hash256{out[0], out[1], out[2], out[3]};
}

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
        blake3_compress_(value, block, out);
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
            blake3_compress_(key, block, out);
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
        blake3_compress_(value, node, out);
        std::uint32_t right[8] = {};
        for (int index = 0; index < 8; ++index) {
            right[index] = out[index];
            value[index] = key[index];
        }
        blake3_parent_block_(stack[depth - 1], right, node);
        --depth;
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The keyed hash of the bytes of message under the key (THE ALGORITHM):
// BLAKE3 in its keyed mode, with 32 bytes of output.  Complexity: one
// compression for each 64 bytes of the message and one for each parent.
[[nodiscard]] constexpr Hash256 keyed_hash(KeyedHashKey const& key, View<std::byte const> message) noexcept {
    std::byte const* const first = first_of_(detail::ViewDoor::key(), message);
    std::size_t const count = message.size();
    if !consteval {
        return detail::blake3_keyed_hash_rows_(key, first, count);
    }
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
        detail::blake3_compress_(value, block, out);
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
    detail::blake3_compress_(value, node, out);
    auto const pair = [&out](int at_word) noexcept {
        return static_cast<std::uint64_t>(out[at_word]) | (static_cast<std::uint64_t>(out[at_word + 1]) << 32);
    };
    return Hash256{pair(0), pair(2), pair(4), pair(6)};
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace foundation::core
