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

// Four words of 32 bits, a row of the state.
typedef std::uint32_t Blake3Row __attribute__((vector_size(16)));
typedef std::uint16_t Blake3Halves __attribute__((vector_size(16)));
typedef std::uint8_t Blake3Bytes __attribute__((vector_size(16)));

// A rotation right of each word of a row.  With AVX-512 each rotation is one
// instruction.  Else a rotation by 16 is one shuffle of the halves, which
// SSE2 does in two steps, a rotation by 8 with SSSE3 or NEON is one shuffle
// of the bytes, and the others are two shifts and an or.
template <int Count>
[[nodiscard]] inline Blake3Row blake3_rotr_row_(Blake3Row row) noexcept {
#if !defined(__AVX512VL__) && (defined(__SSE2__) || defined(__ARM_NEON) || defined(__ARM_NEON__))
    if constexpr (Count == 16) {
        Blake3Halves const halves = __builtin_bit_cast(Blake3Halves, row);
        return __builtin_bit_cast(Blake3Row, __builtin_shufflevector(halves, halves, 1, 0, 3, 2, 5, 4, 7, 6));
    }
#endif
#if !defined(__AVX512VL__) && (defined(__SSSE3__) || defined(__ARM_NEON) || defined(__ARM_NEON__))
    if constexpr (Count == 8) {
        Blake3Bytes const bytes = __builtin_bit_cast(Blake3Bytes, row);
        return __builtin_bit_cast(
            Blake3Row, __builtin_shufflevector(bytes, bytes, 1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12));
    }
#endif
    return (row >> Count) | (row << (32 - Count));
}

// The g function on the four rows.  The word of the message joins first, so
// the sum waits on the second row with one add.
[[gnu::always_inline]] inline void blake3_g_rows_(Blake3Row& first, Blake3Row& second, Blake3Row& third,
                                                  Blake3Row& fourth, Blake3Row mix_x, Blake3Row mix_y) noexcept {
    first = first + mix_x + second;
    fourth = blake3_rotr_row_<16>(fourth ^ first);
    third = third + fourth;
    second = blake3_rotr_row_<12>(second ^ third);
    first = first + mix_y + second;
    fourth = blake3_rotr_row_<8>(fourth ^ first);
    third = third + fourth;
    second = blake3_rotr_row_<7>(second ^ third);
}

// One round of the rows.  The column step mixes the columns of the state.
// The diagonal step turns the rows first, third and fourth by three, one and
// two lanes, so that its columns are the diagonals, and its words of the
// message turn with them.  The second row stays in place: the column step
// writes it last, so no turn waits on it.  After the diagonal step the three
// rows turn back, and the words take the order of the next round.
template <std::size_t Round>
[[gnu::always_inline]] inline void blake3_round_rows_(Blake3Row& first, Blake3Row& second, Blake3Row& third,
                                                      Blake3Row& fourth, Blake3Row& word0, Blake3Row& word1,
                                                      Blake3Row& word2, Blake3Row& word3) noexcept {
    blake3_g_rows_(first, second, third, fourth, __builtin_shufflevector(word0, word1, 0, 2, 4, 6),
                   __builtin_shufflevector(word0, word1, 1, 3, 5, 7));
    first = __builtin_shufflevector(first, first, 3, 0, 1, 2);
    third = __builtin_shufflevector(third, third, 1, 2, 3, 0);
    fourth = __builtin_shufflevector(fourth, fourth, 2, 3, 0, 1);
    blake3_g_rows_(first, second, third, fourth, __builtin_shufflevector(word2, word3, 6, 0, 2, 4),
                   __builtin_shufflevector(word2, word3, 7, 1, 3, 5));
    first = __builtin_shufflevector(first, first, 1, 2, 3, 0);
    third = __builtin_shufflevector(third, third, 3, 0, 1, 2);
    fourth = __builtin_shufflevector(fourth, fourth, 2, 3, 0, 1);
    if constexpr (Round + 1 < 7) {
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
}

template <std::size_t... Rounds>
[[gnu::always_inline]] inline void blake3_rounds_rows_(Blake3Row& first, Blake3Row& second, Blake3Row& third,
                                                       Blake3Row& fourth, Blake3Row& word0, Blake3Row& word1,
                                                       Blake3Row& word2, Blake3Row& word3,
                                                       std::index_sequence<Rounds...>) noexcept {
    (blake3_round_rows_<Rounds>(first, second, third, fourth, word0, word1, word2, word3), ...);
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
    blake3_rounds_rows_(row0, row1, row2, row3, word0, word1, word2, word3, std::make_index_sequence<7>{});
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
// with CHUNK_START and CHUNK_END, the full blocks of a short last chunk
// before its last block have no CHUNK_END, and a parent has one block.
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
