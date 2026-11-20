// The run-time arm of the keyed hash of foundation/core/KeyedHash.h (THE ARMS
// of that header): the rows for a message of one chunk, and the lanes for the
// chunks of a longer message, the full blocks of its last chunk and the
// parents of its full subtrees.  The arm is in a source of the base, so each
// unit that includes the header compiles only its scalar arm.  The arm gives
// the bits of the scalar arm at each ISA level.
//
// The helpers of the arm follow the vector instructions of the target, so
// they are in the target namespace.  The entry compute_keyed_hash_ is outside
// it (THE ENTRY of the header): one symbol serves a caller of each level, and
// it runs the arm of the level of this build.

#include <foundation/core/KeyedHash.h>

#include <cstddef>
#include <cstdint>

// The arm is a few large functions of vector code.  The variable tracking of
// the debug information took 90 percent of the compile of this source, about
// 300 G instructions in each build, and the source compiles in 19 G with no
// variable tracking.  The pragma turns it off for each function of this file
// after the includes, so the instructions and the line tables stay the same,
// and each unit of the foundation library keeps the code flags of the tree.
#pragma GCC optimize("no-var-tracking")

namespace foundation::core::detail {

// The run-time compression follows the vector instructions of the target, so
// it is in the target namespace (THE TARGET NAMESPACE of
// foundation/Platform.h).
inline namespace CRUCIBLE_SIMD_TARGET {

// The vector types of Count words of 32 bits: the words, their halves and
// their bytes.  A row of the state of Count / 4 inputs holds the 4 words of
// the row of each input in its own block of 128 bits.
template <std::size_t Count>
struct Blake3RowTypes final {
    typedef std::uint32_t Words __attribute__((vector_size(4 * Count)));
    typedef std::uint16_t Halves __attribute__((vector_size(4 * Count)));
    typedef std::uint8_t Bytes __attribute__((vector_size(4 * Count)));
};

template <std::size_t Count>
using Blake3Rows = typename Blake3RowTypes<Count>::Words;

// Four words of 32 bits, a row of the state of one input.
using Blake3Row = Blake3Rows<4>;

// The index of the byte at position of a rotation right by Count bits in
// each word, for Count a multiple of 8.
template <int Count>
[[nodiscard]] consteval int blake3_rotr_byte_index_(std::size_t position) noexcept {
    return static_cast<int>((position & ~std::size_t{3}) | ((position + Count / 8) & 3));
}

// The lane at position of a shuffle that takes, in each block of 4 lanes,
// the lanes Picks of the same block of the first vector (0 to 3) or of the
// second (4 to 7).  A pick of -1 takes no lane.
template <std::size_t Count, int First, int Second, int Third, int Fourth>
[[nodiscard]] consteval int blake3_block_pick_index_(std::size_t position) noexcept {
    int const picks[4] = {First, Second, Third, Fourth};
    int const pick = picks[position % 4];
    int const block = static_cast<int>(position / 4 * 4);
    if (pick < 0) return -1;
    return pick < 4 ? block + pick : static_cast<int>(Count) + block + pick - 4;
}

template <int First, int Second, int Third, int Fourth, std::size_t Count, std::size_t... Positions>
[[nodiscard, gnu::always_inline]] inline Blake3Rows<Count>
blake3_block_pick_(Blake3Rows<Count> first, Blake3Rows<Count> second, std::index_sequence<Positions...>) noexcept {
    return __builtin_shufflevector(first, second,
                                   blake3_block_pick_index_<Count, First, Second, Third, Fourth>(Positions)...);
}

template <int First, int Second, int Third, int Fourth, std::size_t Count>
[[nodiscard, gnu::always_inline]] inline Blake3Rows<Count> blake3_pick_(Blake3Rows<Count> first,
                                                                        Blake3Rows<Count> second) noexcept {
    return blake3_block_pick_<First, Second, Third, Fourth, Count>(first, second, std::make_index_sequence<Count>{});
}

template <std::size_t Count, std::size_t... Positions>
[[nodiscard]] inline Blake3Rows<Count> blake3_swap_halves_(Blake3Rows<Count> row,
                                                           std::index_sequence<Positions...>) noexcept {
    using Halves = typename Blake3RowTypes<Count>::Halves;
    Halves const halves = __builtin_bit_cast(Halves, row);
    return __builtin_bit_cast(Blake3Rows<Count>,
                              __builtin_shufflevector(halves, halves, static_cast<int>(Positions ^ 1)...));
}

template <int Rotation, std::size_t Count, std::size_t... Positions>
[[nodiscard]] inline Blake3Rows<Count> blake3_rotr_row_bytes_(Blake3Rows<Count> row,
                                                              std::index_sequence<Positions...>) noexcept {
    using Bytes = typename Blake3RowTypes<Count>::Bytes;
    Bytes const bytes = __builtin_bit_cast(Bytes, row);
    return __builtin_bit_cast(Blake3Rows<Count>,
                              __builtin_shufflevector(bytes, bytes, blake3_rotr_byte_index_<Rotation>(Positions)...));
}

// A rotation right of each word of a row.  With AVX-512 each rotation is one
// instruction.  Else a rotation by 16 is one shuffle of the halves, which
// SSE2 does in two steps, a rotation by 8 with SSSE3 or NEON is one shuffle
// of the bytes, and the others are two shifts and an or.
template <int Rotation, std::size_t Count>
[[nodiscard]] inline Blake3Rows<Count> blake3_rotr_row_(Blake3Rows<Count> row) noexcept {
#if !defined(__AVX512VL__) && (defined(__SSE2__) || defined(__ARM_NEON) || defined(__ARM_NEON__))
    if constexpr (Rotation == 16) {
        return blake3_swap_halves_<Count>(row, std::make_index_sequence<2 * Count>{});
    }
#endif
#if !defined(__AVX512VL__) && (defined(__SSSE3__) || defined(__ARM_NEON) || defined(__ARM_NEON__))
    if constexpr (Rotation == 8) {
        return blake3_rotr_row_bytes_<Rotation, Count>(row, std::make_index_sequence<4 * Count>{});
    }
#endif
    return (row >> Rotation) | (row << (32 - Rotation));
}

// The g function on the four rows.  The word of the message joins first, so
// the sum waits on the second row with one add.
template <std::size_t Count>
[[gnu::always_inline]] inline void blake3_g_rows_(Blake3Rows<Count>& first, Blake3Rows<Count>& second,
                                                  Blake3Rows<Count>& third, Blake3Rows<Count>& fourth,
                                                  Blake3Rows<Count> mix_x, Blake3Rows<Count> mix_y) noexcept {
    first = first + mix_x + second;
    fourth = blake3_rotr_row_<16, Count>(fourth ^ first);
    third = third + fourth;
    second = blake3_rotr_row_<12, Count>(second ^ third);
    first = first + mix_y + second;
    fourth = blake3_rotr_row_<8, Count>(fourth ^ first);
    third = third + fourth;
    second = blake3_rotr_row_<7, Count>(second ^ third);
}

// One round of the rows of Count / 4 inputs.  The column step mixes the
// columns of the state.  The diagonal step turns the rows first, third and
// fourth by three, one and two lanes, so that its columns are the
// diagonals, and its words of the message turn with them.  The second row
// stays in place: the column step writes it last, so no turn waits on it.
// After the diagonal step the three rows turn back, and the words take the
// order of the next round.  Each shuffle stays in the block of each input.
template <std::size_t Round, std::size_t Count>
[[gnu::always_inline]] inline void blake3_round_rows_(Blake3Rows<Count>& first, Blake3Rows<Count>& second,
                                                      Blake3Rows<Count>& third, Blake3Rows<Count>& fourth,
                                                      Blake3Rows<Count>& word0, Blake3Rows<Count>& word1,
                                                      Blake3Rows<Count>& word2, Blake3Rows<Count>& word3) noexcept {
    blake3_g_rows_<Count>(first, second, third, fourth, blake3_pick_<0, 2, 4, 6, Count>(word0, word1),
                          blake3_pick_<1, 3, 5, 7, Count>(word0, word1));
    first = blake3_pick_<3, 0, 1, 2, Count>(first, first);
    third = blake3_pick_<1, 2, 3, 0, Count>(third, third);
    fourth = blake3_pick_<2, 3, 0, 1, Count>(fourth, fourth);
    blake3_g_rows_<Count>(first, second, third, fourth, blake3_pick_<6, 0, 2, 4, Count>(word2, word3),
                          blake3_pick_<7, 1, 3, 5, Count>(word2, word3));
    first = blake3_pick_<1, 2, 3, 0, Count>(first, first);
    third = blake3_pick_<3, 0, 1, 2, Count>(third, third);
    fourth = blake3_pick_<2, 3, 0, 1, Count>(fourth, fourth);
    if constexpr (Round + 1 < 7) {
        Blake3Rows<Count> const next0 =
            blake3_pick_<0, 1, 2, 6, Count>(blake3_pick_<2, 6, 3, -1, Count>(word0, word1), word2);
        Blake3Rows<Count> const next1 =
            blake3_pick_<0, 1, 2, 5, Count>(blake3_pick_<3, 4, 0, -1, Count>(word1, word0), word3);
        Blake3Rows<Count> const next2 = blake3_pick_<0, 1, 2, 5, Count>(
            blake3_pick_<0, 1, 4, -1, Count>(blake3_pick_<1, 7, -1, -1, Count>(word0, word2), word3), word1);
        Blake3Rows<Count> const next3 = blake3_pick_<1, 6, 7, 0, Count>(word2, word3);
        word0 = next0;
        word1 = next1;
        word2 = next2;
        word3 = next3;
    }
}

template <std::size_t Count, std::size_t... Rounds>
[[gnu::always_inline]] inline void
blake3_rounds_rows_(Blake3Rows<Count>& first, Blake3Rows<Count>& second, Blake3Rows<Count>& third,
                    Blake3Rows<Count>& fourth, Blake3Rows<Count>& word0, Blake3Rows<Count>& word1,
                    Blake3Rows<Count>& word2, Blake3Rows<Count>& word3, std::index_sequence<Rounds...>) noexcept {
    (blake3_round_rows_<Rounds, Count>(first, second, third, fourth, word0, word1, word2, word3), ...);
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
// registers, and the 7 rounds unroll, so each value has its own name and no
// round waits on a branch.
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
    blake3_rounds_rows_<4>(row0, row1, row2, row3, word0, word1, word2, word3, std::make_index_sequence<7>{});
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

// The count of words of one part of a vector of Width lanes.  A vector of 8
// or 16 lanes has two parts, so one load of a part of a block of one input
// and one insert of the same part of the block of a second input give the
// first stage of the transpose.  A vector of 4 lanes has one part.
template <std::size_t Width>
inline constexpr std::size_t blake3_part_words = Width >= 8 ? Width / 2 : Width;

// The kinds of an unpack of two vectors: the low or the high half of the
// words, of the pairs of words or of the quads of words of each block of 4,
// 4 or 8 words, from the two vectors in turn.  Each is one instruction of
// the target: an unpack of words or of pairs in each block of 128 bits, or a
// permute of two vectors for the quads.
enum class Blake3Unpack : std::uint8_t {
    words_low,
    words_high,
    pairs_low,
    pairs_high,
    quads_low,
    quads_high
};

// The source lane of the lane at position of an unpack of Kind, where a lane
// at Width or above is a lane of the second vector.
template <std::size_t Width, Blake3Unpack Kind>
[[nodiscard]] consteval int blake3_unpack_index_(std::size_t position) noexcept {
    std::size_t const block = position / 4 * 4;
    std::size_t const inner = position % 4;
    std::size_t const quad_block = position / 8 * 8;
    std::size_t const quad_inner = position % 8;
    std::size_t source = 0;
    if constexpr (Kind == Blake3Unpack::words_low || Kind == Blake3Unpack::words_high) {
        std::size_t const half = Kind == Blake3Unpack::words_high ? 2 : 0;
        source = (inner % 2 == 0 ? 0 : Width) + block + half + inner / 2;
    } else if constexpr (Kind == Blake3Unpack::pairs_low || Kind == Blake3Unpack::pairs_high) {
        std::size_t const half = Kind == Blake3Unpack::pairs_high ? 2 : 0;
        source = (inner < 2 ? 0 : Width) + block + half + inner % 2;
    } else {
        std::size_t const half = Kind == Blake3Unpack::quads_high ? 4 : 0;
        source = (quad_inner < 4 ? 0 : Width) + quad_block + half + quad_inner % 4;
    }
    return static_cast<int>(source);
}

template <std::size_t Width, Blake3Unpack Kind, std::size_t... Positions>
[[nodiscard]] inline Blake3Lanes<Width> blake3_unpack_(Blake3Lanes<Width> first, Blake3Lanes<Width> second,
                                                       std::index_sequence<Positions...>) noexcept {
    return __builtin_shufflevector(first, second, blake3_unpack_index_<Width, Kind>(Positions)...);
}

// Transposes each part of the rows: lane j of part h of row k moves to lane
// k of part h of row j.  An unpack of the words and an unpack of the pairs
// transpose each block of 4 words, and for parts of 8 words an unpack of the
// quads moves the blocks.  The transpose is its own inverse.
template <std::size_t Width>
inline void blake3_transpose_parts_(Blake3Lanes<Width> (&rows)[blake3_part_words<Width>]) noexcept {
    constexpr std::size_t part = blake3_part_words<Width>;
    constexpr auto positions = std::make_index_sequence<Width>{};
    Blake3Lanes<Width> words[part] = {};
    for (std::size_t pair = 0; pair < part / 2; ++pair) {
        words[2 * pair] = blake3_unpack_<Width, Blake3Unpack::words_low>(rows[2 * pair], rows[2 * pair + 1], positions);
        words[2 * pair + 1] =
            blake3_unpack_<Width, Blake3Unpack::words_high>(rows[2 * pair], rows[2 * pair + 1], positions);
    }
    Blake3Lanes<Width> pairs[part] = {};
    for (std::size_t block = 0; block < part; block += 4) {
        for (std::size_t half = 0; half < 2; ++half) {
            Blake3Lanes<Width> const low = words[block + half];
            Blake3Lanes<Width> const high = words[block + half + 2];
            pairs[block + 2 * half] = blake3_unpack_<Width, Blake3Unpack::pairs_low>(low, high, positions);
            pairs[block + 2 * half + 1] = blake3_unpack_<Width, Blake3Unpack::pairs_high>(low, high, positions);
        }
    }
    if constexpr (part == 8) {
        for (std::size_t row = 0; row < 4; ++row) {
            rows[row] = blake3_unpack_<Width, Blake3Unpack::quads_low>(pairs[row], pairs[row + 4], positions);
            rows[row + 4] = blake3_unpack_<Width, Blake3Unpack::quads_high>(pairs[row], pairs[row + 4], positions);
        }
    } else {
        for (std::size_t row = 0; row < part; ++row) {
            rows[row] = pairs[row];
        }
    }
}

// The vector of the words from first of input lower in its first part and
// of input upper in its second part: one load and one insert.  A vector of 4
// lanes has one part, from input lower.
template <std::size_t Width, std::size_t... Positions>
[[nodiscard]] inline Blake3Lanes<Width> blake3_load_parts_(std::byte const* lower, std::byte const* upper,
                                                           std::index_sequence<Positions...>) noexcept {
    constexpr std::size_t part = blake3_part_words<Width>;
    if constexpr (part == Width) {
        Blake3Lanes<Width> words{};
        __builtin_memcpy(&words, lower, sizeof(words));
        return words;
    } else {
        Blake3Lanes<part> low{};
        Blake3Lanes<part> high{};
        __builtin_memcpy(&low, lower, sizeof(low));
        __builtin_memcpy(&high, upper, sizeof(high));
        return __builtin_shufflevector(low, high, Positions...);
    }
}

// The part from the lane at index Part times at of a vector.
template <std::size_t Width, std::size_t At, std::size_t... Positions>
[[nodiscard]] inline Blake3Lanes<blake3_part_words<Width>> blake3_part_of_(Blake3Lanes<Width> words,
                                                                           std::index_sequence<Positions...>) noexcept {
    return __builtin_shufflevector(words, words, (At * blake3_part_words<Width> + Positions)...);
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
// with CHUNK_START and CHUNK_END, the full blocks of a short last chunk
// before its last block have no CHUNK_END, and a parent has one block.
struct Blake3Input final {
    std::byte const* first = nullptr;
    std::size_t blocks = 0;
    std::uint64_t counter = 0;
    std::uint32_t start_flags = 0;
    std::uint32_t end_flags = 0;
};

// The chaining values of count inputs, at most Width, in the lanes.  Each
// step compresses one block of each lane.  The run has a step for each block
// of its longest input, and an input of fewer blocks waits: it reads the
// bytes before its first block while it waits, and it takes the key as its
// value at its first block, so each lane ends at the last step and no lane
// keeps a value of its own across the rounds.  Thus an input of fewer blocks
// needs as many readable bytes before it as the blocks it waits.  A lane
// past count repeats input 0.  The time of a run hardly depends on count: the
// lanes run in parallel, where one input alone waits for each step of its
// rounds.  Complexity: one compression of the lanes for each block of the
// longest input.
template <std::size_t Width>
inline void blake3_run_lanes_(Blake3Cv key, Blake3Input const* inputs, std::size_t count, std::uint32_t flags,
                              Blake3Cv* out) noexcept {
    using Lanes = Blake3Lanes<Width>;
    constexpr std::size_t part = blake3_part_words<Width>;
    std::uint32_t key_words[8] = {};
    __builtin_memcpy(&key_words[0], &key.low, sizeof(key.low));
    __builtin_memcpy(&key_words[4], &key.high, sizeof(key.high));
    std::size_t steps = 0;
    for (std::size_t lane = 0; lane < count; ++lane) {
        steps = steps < inputs[lane].blocks ? inputs[lane].blocks : steps;
    }
    std::byte const* firsts[Width] = {};
    std::uint32_t counter_low[Width] = {};
    std::uint32_t counter_high[Width] = {};
    std::uint32_t start_flags[Width] = {};
    std::uint32_t end_flags[Width] = {};
    std::uint32_t waits[Width] = {};
    std::size_t longest_wait = 0;
    for (std::size_t lane = 0; lane < Width; ++lane) {
        Blake3Input const& input = inputs[lane < count ? lane : 0];
        std::size_t const wait = steps - input.blocks;
        firsts[lane] = input.first - wait * blake3_block_bytes;
        counter_low[lane] = static_cast<std::uint32_t>(input.counter);
        counter_high[lane] = static_cast<std::uint32_t>(input.counter >> 32);
        start_flags[lane] = input.start_flags;
        end_flags[lane] = input.end_flags;
        waits[lane] = static_cast<std::uint32_t>(wait);
        longest_wait = longest_wait < wait ? wait : longest_wait;
    }
    Lanes low_lanes{};
    Lanes high_lanes{};
    Lanes start_lanes{};
    Lanes end_lanes{};
    Lanes wait_lanes{};
    __builtin_memcpy(&low_lanes, counter_low, sizeof(Lanes));
    __builtin_memcpy(&high_lanes, counter_high, sizeof(Lanes));
    __builtin_memcpy(&start_lanes, start_flags, sizeof(Lanes));
    __builtin_memcpy(&end_lanes, end_flags, sizeof(Lanes));
    __builtin_memcpy(&wait_lanes, waits, sizeof(Lanes));
    Lanes value[8] = {};
    for (int word = 0; word < 8; ++word) {
        value[word] = Lanes{} + key_words[word];
    }
    for (std::size_t step = 0; step < steps; ++step) {
        Lanes step_flags = Lanes{} + flags;
        if (step <= longest_wait) {
            // All ones in each lane whose first block is at this step.
            Lanes const is_first = static_cast<Lanes>(wait_lanes == static_cast<std::uint32_t>(step));
            step_flags |= is_first & start_lanes;
            if (step != 0) {
                for (int word = 0; word < 8; ++word) {
                    value[word] = (value[word] & ~is_first) | ((Lanes{} + key_words[word]) & is_first);
                }
            }
        }
        if (step + 1 == steps) step_flags |= end_lanes;
        // The words of the block of each lane: one load of a part of the
        // block of one lane, one insert of the same part of the block of a
        // second lane, and a transpose of the parts.
        Lanes message[16] = {};
        for (std::size_t group = 0; group < 16 / part; ++group) {
            Lanes rows[part] = {};
            for (std::size_t row = 0; row < part; ++row) {
                std::size_t const offset = step * blake3_block_bytes + group * part * 4;
                rows[row] = blake3_load_parts_<Width>(firsts[row] + offset, firsts[(row + part) % Width] + offset,
                                                      std::make_index_sequence<Width>{});
            }
            blake3_transpose_parts_<Width>(rows);
            for (std::size_t word = 0; word < part; ++word) {
                message[group * part + word] = rows[word];
            }
        }
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
                           step_flags};
        blake3_rounds_lanes_<Width>(state, message, std::make_index_sequence<7>{});
        for (int word = 0; word < 8; ++word) {
            value[word] = state[word] ^ state[word + 8];
        }
    }
    // The values go out by the transpose of the parts: each part of a row
    // then holds part words of the value of one lane.
    for (std::size_t group = 0; group < 8 / part; ++group) {
        Lanes rows[part] = {};
        for (std::size_t row = 0; row < part; ++row) {
            rows[row] = value[group * part + row];
        }
        blake3_transpose_parts_<Width>(rows);
        for (std::size_t row = 0; row < part; ++row) {
            Blake3Lanes<part> const low = blake3_part_of_<Width, 0>(rows[row], std::make_index_sequence<part>{});
            Blake3Lanes<part> const high =
                blake3_part_of_<Width, Width / part - 1>(rows[row], std::make_index_sequence<part>{});
            std::byte* const words_first = static_cast<std::byte*>(static_cast<void*>(out)) + group * part * 4;
            if (row < count) {
                __builtin_memcpy(words_first + row * sizeof(Blake3Cv), &low, sizeof(low));
            }
            if (part != Width && row + part < count) {
                __builtin_memcpy(words_first + (row + part) * sizeof(Blake3Cv), &high, sizeof(high));
            }
        }
    }
}

// ── The stacked rows: a few inputs at once ──────────────────────────────
//
// A vector of Count words holds the row of each of Count / 4 inputs in its
// own block of 128 bits.  The rows run as the rows of one input, with each
// shuffle inside its block, so the inputs run side by side in the time of
// one compression.  The lanes need more inputs than they have at a level of
// a few parents or at a message of a few chunks, and then the time of a run
// of the lanes is one step for each input that it lacks.

// The vector of Count words whose each block of 4 words is row.
template <std::size_t Count, std::size_t... Positions>
[[nodiscard]] inline Blake3Rows<Count> blake3_repeat_row_(Blake3Row row, std::index_sequence<Positions...>) noexcept {
    return __builtin_shufflevector(row, row, static_cast<int>(Positions % 4)...);
}

// The vector of the words of low and then the words of high.
template <std::size_t Half, std::size_t... Positions>
[[nodiscard]] inline Blake3Rows<2 * Half> blake3_concatenate_(Blake3Rows<Half> low, Blake3Rows<Half> high,
                                                              std::index_sequence<Positions...>) noexcept {
    return __builtin_shufflevector(low, high, static_cast<int>(Positions)...);
}

// The vector of the rows of Count / 4 inputs, each from the bytes at the
// offset after the first byte of its input.
template <std::size_t Count>
[[nodiscard]] inline Blake3Rows<Count> blake3_load_stacked_(std::byte const* const* firsts,
                                                            std::size_t offset) noexcept {
    if constexpr (Count == 4) {
        Blake3Row row{};
        __builtin_memcpy(&row, firsts[0] + offset, sizeof(row));
        return row;
    } else {
        Blake3Rows<Count / 2> const low = blake3_load_stacked_<Count / 2>(firsts, offset);
        Blake3Rows<Count / 2> const high = blake3_load_stacked_<Count / 2>(firsts + Count / 8, offset);
        return blake3_concatenate_<Count / 2>(low, high, std::make_index_sequence<Count>{});
    }
}

// The row of the input at index of a vector of stacked rows.
template <std::size_t Count>
[[nodiscard]] inline Blake3Row blake3_row_of_(Blake3Rows<Count> rows, std::size_t index) noexcept {
    std::uint32_t words[Count] = {};
    __builtin_memcpy(&words[0], &rows, sizeof(rows));
    Blake3Row row{};
    __builtin_memcpy(&row, &words[4 * index], sizeof(row));
    return row;
}

// One round of Groups vectors of stacked rows, one group after the other, so
// the groups run side by side.
template <std::size_t Round, std::size_t Count, std::size_t Groups>
[[gnu::always_inline]] inline void
blake3_round_groups_(Blake3Rows<Count> (&first)[Groups], Blake3Rows<Count> (&second)[Groups],
                     Blake3Rows<Count> (&third)[Groups], Blake3Rows<Count> (&fourth)[Groups],
                     Blake3Rows<Count> (&word0)[Groups], Blake3Rows<Count> (&word1)[Groups],
                     Blake3Rows<Count> (&word2)[Groups], Blake3Rows<Count> (&word3)[Groups]) noexcept {
    for (std::size_t group = 0; group < Groups; ++group) {
        blake3_round_rows_<Round, Count>(first[group], second[group], third[group], fourth[group], word0[group],
                                         word1[group], word2[group], word3[group]);
    }
}

template <std::size_t Count, std::size_t Groups, std::size_t... Rounds>
[[gnu::always_inline]] inline void blake3_rounds_groups_(
    Blake3Rows<Count> (&first)[Groups], Blake3Rows<Count> (&second)[Groups], Blake3Rows<Count> (&third)[Groups],
    Blake3Rows<Count> (&fourth)[Groups], Blake3Rows<Count> (&word0)[Groups], Blake3Rows<Count> (&word1)[Groups],
    Blake3Rows<Count> (&word2)[Groups], Blake3Rows<Count> (&word3)[Groups], std::index_sequence<Rounds...>) noexcept {
    (blake3_round_groups_<Rounds, Count, Groups>(first, second, third, fourth, word0, word1, word2, word3), ...);
}

// The chaining values of count inputs, at most Groups times Count / 4, in
// Groups vectors of stacked rows of Count words.  A vector stays at the width
// of a register of the target, and two groups run side by side where one
// register holds too few inputs.  As in the lanes, each input runs for the
// steps of its longest input, and a shorter input waits on the bytes before
// it until its first block.  An input past count repeats input 0.
// Complexity: one compression of the rows for each block of the longest
// input.
template <std::size_t Count, std::size_t Groups>
inline void blake3_run_rows_(Blake3Cv key, Blake3Input const* inputs, std::size_t count, std::uint32_t flags,
                             Blake3Cv* out) noexcept {
    using Rows = Blake3Rows<Count>;
    constexpr std::size_t stacked = Count / 4;
    constexpr std::size_t total = Groups * stacked;
    constexpr auto positions = std::make_index_sequence<Count>{};
    std::size_t steps = 0;
    for (std::size_t input = 0; input < count; ++input) {
        steps = steps < inputs[input].blocks ? inputs[input].blocks : steps;
    }
    std::byte const* firsts[total] = {};
    std::size_t waits[total] = {};
    std::size_t longest_wait = 0;
    std::uint32_t fourth_words[Groups * Count] = {};
    std::uint32_t start_words[Groups * Count] = {};
    std::uint32_t end_words[Groups * Count] = {};
    for (std::size_t input = 0; input < total; ++input) {
        Blake3Input const& source = inputs[input < count ? input : 0];
        std::size_t const wait = steps - source.blocks;
        firsts[input] = source.first - wait * blake3_block_bytes;
        waits[input] = wait;
        longest_wait = longest_wait < wait ? wait : longest_wait;
        fourth_words[4 * input] = static_cast<std::uint32_t>(source.counter);
        fourth_words[4 * input + 1] = static_cast<std::uint32_t>(source.counter >> 32);
        fourth_words[4 * input + 2] = static_cast<std::uint32_t>(blake3_block_bytes);
        fourth_words[4 * input + 3] = flags;
        start_words[4 * input + 3] = source.start_flags;
        end_words[4 * input + 3] = source.end_flags;
    }
    Rows fourth_base[Groups] = {};
    Rows start_rows[Groups] = {};
    Rows end_rows[Groups] = {};
    __builtin_memcpy(&fourth_base[0], fourth_words, sizeof(fourth_base));
    __builtin_memcpy(&start_rows[0], start_words, sizeof(start_rows));
    __builtin_memcpy(&end_rows[0], end_words, sizeof(end_rows));
    Rows const key_low = blake3_repeat_row_<Count>(key.low, positions);
    Rows const key_high = blake3_repeat_row_<Count>(key.high, positions);
    Rows const iv =
        blake3_repeat_row_<Count>(Blake3Row{blake3_iv[0], blake3_iv[1], blake3_iv[2], blake3_iv[3]}, positions);
    Rows first[Groups] = {};
    Rows second[Groups] = {};
    for (std::size_t group = 0; group < Groups; ++group) {
        first[group] = key_low;
        second[group] = key_high;
    }
    for (std::size_t step = 0; step < steps; ++step) {
        Rows third[Groups] = {};
        Rows fourth[Groups] = {};
        for (std::size_t group = 0; group < Groups; ++group) {
            third[group] = iv;
            fourth[group] = fourth_base[group];
        }
        if (step <= longest_wait) {
            // All ones in the block of each input whose first block is at
            // this step.
            std::uint32_t is_first_words[Groups * Count] = {};
            for (std::size_t input = 0; input < total; ++input) {
                std::uint32_t const is_first = waits[input] == step ? ~std::uint32_t{0} : 0;
                for (std::size_t word = 0; word < 4; ++word) {
                    is_first_words[4 * input + word] = is_first;
                }
            }
            Rows is_first[Groups] = {};
            __builtin_memcpy(&is_first[0], is_first_words, sizeof(is_first));
            for (std::size_t group = 0; group < Groups; ++group) {
                fourth[group] |= start_rows[group] & is_first[group];
                if (step != 0) {
                    first[group] = (first[group] & ~is_first[group]) | (key_low & is_first[group]);
                    second[group] = (second[group] & ~is_first[group]) | (key_high & is_first[group]);
                }
            }
        }
        if (step + 1 == steps) {
            for (std::size_t group = 0; group < Groups; ++group) {
                fourth[group] |= end_rows[group];
            }
        }
        std::size_t const offset = step * blake3_block_bytes;
        Rows word0[Groups] = {};
        Rows word1[Groups] = {};
        Rows word2[Groups] = {};
        Rows word3[Groups] = {};
        for (std::size_t group = 0; group < Groups; ++group) {
            std::byte const* const* const group_firsts = firsts + group * stacked;
            word0[group] = blake3_load_stacked_<Count>(group_firsts, offset);
            word1[group] = blake3_load_stacked_<Count>(group_firsts, offset + 16);
            word2[group] = blake3_load_stacked_<Count>(group_firsts, offset + 32);
            word3[group] = blake3_load_stacked_<Count>(group_firsts, offset + 48);
        }
        blake3_rounds_groups_<Count, Groups>(first, second, third, fourth, word0, word1, word2, word3,
                                             std::make_index_sequence<7>{});
        for (std::size_t group = 0; group < Groups; ++group) {
            first[group] = first[group] ^ third[group];
            second[group] = second[group] ^ fourth[group];
        }
    }
    for (std::size_t input = 0; input < count; ++input) {
        out[input] = Blake3Cv{blake3_row_of_<Count>(first[input / stacked], input % stacked),
                              blake3_row_of_<Count>(second[input / stacked], input % stacked)};
    }
}

// The chaining values of count inputs into out: runs of the widest lanes
// while they fill, then one run of the narrowest form that holds the rest.
// One input runs in the rows.  Two inputs run in one vector of 256 bits of
// stacked rows with AVX2 or AVX-512, else in two vectors of 128 bits side by
// side.  Three or four inputs run in two vectors of 256 bits with AVX2 or
// AVX-512, else in four lanes.  A vector of 512 bits of stacked rows is no
// faster than two of 256 bits on a target that splits each instruction of
// 512 bits in two halves, and a vector wider than the registers of the
// target loses each shuffle to scalar code.  Complexity: one compression of
// the lanes for each block of each run.
inline void blake3_run_inputs_(Blake3Cv key, Blake3Input const* inputs, std::size_t count, std::uint32_t flags,
                               Blake3Cv* out) noexcept {
    std::size_t done = 0;
    while (count - done >= blake3_widest_lanes) {
        blake3_run_lanes_<blake3_widest_lanes>(key, inputs + done, blake3_widest_lanes, flags, out + done);
        done += blake3_widest_lanes;
    }
    std::size_t const rest = count - done;
    if (rest == 0) return;
    if (rest == 1) {
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
        return;
    }
    if (rest == 2) {
        if constexpr (blake3_widest_lanes >= 8) {
            blake3_run_rows_<8, 1>(key, inputs + done, rest, flags, out + done);
        } else {
            blake3_run_rows_<4, 2>(key, inputs + done, rest, flags, out + done);
        }
    } else if (rest <= 4) {
        if constexpr (blake3_widest_lanes >= 8) {
            blake3_run_rows_<8, 2>(key, inputs + done, rest, flags, out + done);
        } else {
            blake3_run_lanes_<4>(key, inputs + done, rest, flags, out + done);
        }
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

// One level of parents of the count chaining values of values, in place:
// the values join in pairs from the left, and an odd last value goes up
// alone.  The pair of two neighbor values is the 64 bytes of the block of
// their parent.  The pairs run in lanes.  Each run reads its blocks before it
// writes its values, and a run writes only the values below the pairs of the
// runs after it, so the level needs no copy.  Returns the count of the values
// of the next level.  Complexity: one compression for each pair.
inline std::size_t blake3_join_level_(Blake3Cv key, Blake3Cv* values, std::size_t count) noexcept {
    std::size_t const pairs = count / 2;
    std::byte const* const pair_bytes = static_cast<std::byte const*>(static_cast<void const*>(values));
    Blake3Input inputs[blake3_batch_chunks / 2] = {};
    for (std::size_t index = 0; index < pairs; ++index) {
        inputs[index] = Blake3Input{.first = pair_bytes + index * blake3_block_bytes, .blocks = 1};
    }
    blake3_run_inputs_(key, inputs, pairs, blake3_keyed_hash | blake3_parent, values);
    if (count % 2 != 0) values[pairs] = values[count - 1];
    return pairs + count % 2;
}

// Each level of the parents of the count chaining values of the chunks of
// values, until stop values remain: one for the root of a subtree, two for
// the pair of the root of the message.  The chunks form one subtree, whose
// left subtrees are full, so the levels of pairs from the left give the
// parents of the tree.  Returns the count of the values that remain.  Linear
// in count.
inline std::size_t blake3_join_levels_(Blake3Cv key, Blake3Cv* values, std::size_t count, std::size_t stop) noexcept {
    while (count > stop) {
        count = blake3_join_level_(key, values, count);
    }
    return count;
}

// The block of the root of a message of more than one chunk, at run time,
// into node.  The chunks go in batches of blake3_batch_chunks, each a full
// subtree at a multiple of its size, and a last batch with the chunks that
// remain, the last chunk of the message with them.  That last batch starts
// at the count of chunks before the last with its low bits cleared, so it is
// the last right subtree of the tree.  The chunks of a batch run in lanes,
// and each level of their parents runs in lanes.  The root of each full
// batch joins a stack, as its chunks would.  The root of the last batch then
// joins each value of the stack from the right.  Linear in the count of
// chunks.
[[gnu::noinline]] inline void blake3_tree_root_(Blake3Cv key, std::byte const* first, std::size_t count,
                                                Blake3Node& node) noexcept {
    std::size_t const chunks = (count - 1) / blake3_chunk_bytes + 1;
    std::size_t const last_first = (chunks - 1) / blake3_batch_chunks * blake3_batch_chunks;
    Blake3Input inputs[blake3_batch_chunks] = {};
    Blake3Cv values[blake3_batch_chunks] = {};
    Blake3Cv stack[blake3_max_depth] = {};
    std::size_t depth = 0;
    auto const chunk_input = [first](std::size_t chunk) noexcept {
        return Blake3Input{.first = first + chunk * blake3_chunk_bytes,
                           .blocks = blake3_chunk_bytes / blake3_block_bytes,
                           .counter = chunk,
                           .start_flags = blake3_chunk_start,
                           .end_flags = blake3_chunk_end};
    };
    for (std::size_t batch_first = 0; batch_first < last_first; batch_first += blake3_batch_chunks) {
        for (std::size_t index = 0; index < blake3_batch_chunks; ++index) {
            inputs[index] = chunk_input(batch_first + index);
        }
        blake3_run_inputs_(key, inputs, blake3_batch_chunks, blake3_keyed_hash, values);
        blake3_join_levels_(key, values, blake3_batch_chunks, 1);
        Blake3Cv value = values[0];
        for (std::size_t total = batch_first / blake3_batch_chunks + 1; (total & 1U) == 0; total >>= 1) {
            value = blake3_parent_cv_(key, stack[depth - 1], value);
            --depth;
        }
        stack[depth] = value;
        ++depth;
    }
    // The last batch.  A short last chunk runs its full blocks before its
    // last block in a lane beside the full chunks, and its last block runs
    // after them in the rows, from a copy with zero bytes after its end.
    std::size_t const batch = chunks - last_first;
    for (std::size_t index = 0; index < batch; ++index) {
        inputs[index] = chunk_input(last_first + index);
    }
    std::size_t const last_bytes = count - (chunks - 1) * blake3_chunk_bytes;
    bool const is_short = last_bytes < blake3_chunk_bytes;
    std::size_t runs = batch;
    Blake3Node tail{};
    if (is_short) {
        Blake3Input& last = inputs[batch - 1];
        last.blocks = (last_bytes - 1) / blake3_block_bytes;
        last.end_flags = 0;
        if (last.blocks == 0) --runs;
        std::size_t const tail_bytes = last_bytes - last.blocks * blake3_block_bytes;
        blake3_load_partial_rows_(last.first + last.blocks * blake3_block_bytes, tail_bytes, tail.words);
        tail.counter = chunks - 1;
        tail.bytes = static_cast<std::uint32_t>(tail_bytes);
        tail.flags = blake3_keyed_hash | (last.blocks == 0 ? blake3_chunk_start : 0) | blake3_chunk_end;
    }
    blake3_run_inputs_(key, inputs, runs, blake3_keyed_hash, values);
    if (is_short) {
        Blake3Cv const prefix = runs == batch ? values[batch - 1] : key;
        values[batch - 1] = blake3_compress_cv_(prefix, tail.words, tail.counter, tail.bytes, tail.flags);
    }
    // With no stack, the last batch is the whole tree, and its last pair is
    // the root.  Else the root of the last batch joins the stack.
    std::size_t const left = blake3_join_levels_(key, values, batch, depth == 0 ? 2 : 1);
    Blake3Cv left_value = values[0];
    Blake3Cv right_value = values[left - 1];
    if (depth != 0) {
        for (; depth > 1; --depth) {
            right_value = blake3_parent_cv_(key, stack[depth - 1], right_value);
        }
        left_value = stack[0];
    }
    node.cv = key;
    node.words[0] = left_value.low;
    node.words[1] = left_value.high;
    node.words[2] = right_value.low;
    node.words[3] = right_value.high;
    node.counter = 0;
    node.bytes = blake3_block_bytes;
    node.flags = blake3_keyed_hash | blake3_parent;
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The keyed hash of the bytes of message, at run time.  A message of one
// chunk runs in the rows, and its last block is the root.  A longer message
// runs its chunks in the lanes, the last chunk with them, and its root is the
// parent of the top of the tree.
[[nodiscard]] Hash256 compute_keyed_hash_(KeyedHashKeyBytes const& key, View<std::byte const> message) noexcept {
    std::byte const* const first = first_of_(ViewDoor::key(), message);
    std::size_t const count = message.size();
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
    if (full_chunks != 0) blake3_tree_root_(key_cv, first, count, node);
    Blake3Cv const root = blake3_compress_cv_(node.cv, node.words, 0, node.bytes, node.flags | blake3_root);
    std::uint64_t out[4] = {};
    __builtin_memcpy(&out[0], &root.low, sizeof(root.low));
    __builtin_memcpy(&out[2], &root.high, sizeof(root.high));
    return Hash256{out[0], out[1], out[2], out[3]};
}

}  // namespace foundation::core::detail
