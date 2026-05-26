// ═══════════════════════════════════════════════════════════════════
// prop_integrity_xxhash.cpp — streaming-vs-one-shot path-equivalence
// fuzzer for the cntp XXH64 checksum (cntp/Integrity.h).
//
// Integrity.h ships TWO implementations of the same mathematical
// function: the straight-line one-shot `xxhash64_raw(span, seed)` and
// the buffered streaming `XxHash64State::update()/digest()`.  They
// share the core round primitives (process_stripe / merge_round64 /
// finalize_tail), but the streaming path adds a fiddly buffer-carry
// state machine — accumulate sub-stripe bytes into `memory_`, complete
// a partial stripe via `fill = 32 - memory_size_`, then carry the
// trailing `len mod 32` bytes forward for the next update or for the
// final `digest()` tail.  That carry logic is the classic divergence
// site for hand-rolled streaming hashes, and it was NEARLY untested:
// the one existing streaming assertion in test_cntp_integrity.cpp feeds
// a 96-byte (= 3·32, exact-multiple) payload in a single fixed 7/25/64
// chunking, which leaves `memory_size_ == 0` at digest() and therefore
// never exercises digest()'s `finalize_tail(hash, memory_, memory_size_)`
// with a non-empty tail, nor a sub-stripe (<32) streamed input, nor any
// byte-at-a-time carry.  A regression that mishandled the trailing
// partial bytes would slip past that single test — and corrupt content-
// addressing / Cipher integrity (DetSafe-critical: same bytes MUST hash
// identically whether checksummed in one shot or incrementally).
//
// The reference here is the one-shot path (a DIFFERENT code path: no
// buffer, no carry, a single `do/while` stripe loop).  This is a path-
// equivalence differential targeting the streaming buffer-carry state
// machine — not an independent reimplementation of XXH64.  For each
// random payload we stream it THREE ways and require every digest to
// match the one-shot raw hash bit-for-bit:
//   * Whole     — one update() of the entire payload (update-once ==
//                 one-shot baseline);
//   * ByteWise  — update() one byte at a time (maximal carry stress —
//                 every byte transits the accumulate path and the final
//                 `len mod 32` bytes land in memory_ for digest's tail);
//   * Random    — split into uniform-random chunk sizes drawn from a
//                 spec-seeded local Rng (broad boundary coverage).
//
// ZeroHash admission is tied across paths too: if the one-shot raw hash
// is 0, digest() must return unexpected(ZeroHash); otherwise digest()
// must succeed with value() == the raw hash.  Both paths feed the same
// `admit_integrity_hash`, so this also witnesses admission consistency.
//
// Four length modes — Small (<32, sub-stripe + tail-only), Boundary
// (exact multiples of 32, empty tail), TailHeavy (32·k + r, r∈[1,31] —
// the carried-tail corner), Random ([0, kMaxLen]) — so the boundary the
// existing test missed is exercised densely, not just sampled.  The
// hash seed is 0 half the time (exercises both XxHash64State ctor
// branches: seed==0 vs seed!=0 v1..v4 derivation).
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/cntp/Integrity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace {

namespace ci = crucible::cntp;
using crucible::fuzz::prop::Rng;

inline constexpr std::uint32_t kMaxLen = 256;  // ≥ 8 full stripes + tail

enum class LenMode : std::uint8_t {
    Small = 0,       // [0, 31]      — sub-stripe; seed+prime5 + tail-only
    Boundary = 1,    // {0,32,...,256} — exact stripe multiples, empty tail
    TailHeavy = 2,   // 32·k + r, r∈[1,31] — carried-tail corner
    Random = 3,      // [0, kMaxLen]
};

struct Spec {
    std::array<std::byte, kMaxLen> bytes{};
    std::uint32_t len = 0;
    std::uint64_t hash_seed = 0;   // 0 half the time, else random
    std::uint64_t chunk_seed = 0;  // drives the Random-chunking split
    LenMode mode = LenMode::Random;
    std::uint8_t pad[7]{};
};

[[nodiscard]] std::uint32_t gen_len(Rng& rng, LenMode mode) noexcept {
    switch (mode) {
        case LenMode::Small:
            return rng.next_below(32u);                       // [0, 31]
        case LenMode::Boundary:
            return 32u * rng.next_below((kMaxLen / 32u) + 1u);  // {0,32,...,256}
        case LenMode::TailHeavy: {
            const std::uint32_t full = rng.next_below(8u);    // [0, 7] stripes
            const std::uint32_t tail = 1u + rng.next_below(31u);  // [1, 31]
            return 32u * full + tail;
        }
        case LenMode::Random:
            return rng.next_below(kMaxLen + 1u);              // [0, 256]
        default:
            std::unreachable();
    }
}

// One-shot raw hash over the populated prefix.
[[nodiscard]] std::uint64_t one_shot_raw(const Spec& spec) noexcept {
    return ci::xxhash64_raw(
        std::span<const std::byte>{spec.bytes.data(), spec.len},
        spec.hash_seed);
}

// Stream `spec` through a fresh state with the given chunk plan, then
// digest.  Returns true iff the digest is consistent with `expected_raw`
// (value-equal when nonzero, ZeroHash error when zero).
template <typename ChunkFn>
[[nodiscard]] bool streamed_matches(const Spec& spec,
                                    std::uint64_t expected_raw,
                                    ChunkFn&& feed) noexcept {
    auto state = ci::xxhash64_streaming(spec.hash_seed);
    if (!feed(state)) return false;  // an update() reported an error

    const auto digested = state.digest();
    if (expected_raw == 0) {
        // Both paths feed admit_integrity_hash → both must reject 0.
        return !digested.has_value() &&
               digested.error() == ci::IntegrityError::ZeroHash;
    }
    return digested.has_value() && digested->value() == expected_raw;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("integrity_xxhash", cfg,
        // ── Generator ──
        [](Rng& rng) noexcept -> Spec {
            Spec spec{};
            spec.mode = static_cast<LenMode>(rng.next_below(4u));
            spec.len = gen_len(rng, spec.mode);
            for (std::uint32_t i = 0; i < spec.len; ++i) {
                spec.bytes[i] = static_cast<std::byte>(rng.next32() & 0xFFu);
            }
            spec.hash_seed = (rng.next_below(2u) == 0u) ? 0u : rng.next64();
            spec.chunk_seed = rng.next64();
            return spec;
        },
        // ── Property ──
        [](const Spec& spec) noexcept -> bool {
            const std::byte* base = spec.bytes.data();
            const std::uint64_t expected_raw = one_shot_raw(spec);

            // Strategy 1 — Whole: a single update of the entire payload.
            const bool whole_ok = streamed_matches(spec, expected_raw,
                [&](ci::XxHash64State& st) noexcept -> bool {
                    return st.update(
                        std::span<const std::byte>{base, spec.len}).has_value();
                });
            if (!whole_ok) return false;

            // Strategy 2 — ByteWise: one byte per update.  Maximal carry
            // stress; the trailing len mod 32 bytes land in memory_ for
            // digest()'s finalize_tail — the path the single fixed test
            // never reaches.
            const bool bytewise_ok = streamed_matches(spec, expected_raw,
                [&](ci::XxHash64State& st) noexcept -> bool {
                    for (std::uint32_t i = 0; i < spec.len; ++i) {
                        if (!st.update(
                                std::span<const std::byte>{base + i, 1}).has_value()) {
                            return false;
                        }
                    }
                    return true;
                });
            if (!bytewise_ok) return false;

            // Strategy 3 — Random: uniform-random chunk sizes from a
            // spec-seeded local Rng (deterministic + reproducible).
            const bool random_ok = streamed_matches(spec, expected_raw,
                [&](ci::XxHash64State& st) noexcept -> bool {
                    Rng chunk_rng{spec.chunk_seed, 0};
                    std::uint32_t offset = 0;
                    // Bounded draws guarantee termination even under an
                    // (astronomically unlikely) run of zero-size chunks;
                    // a zero take is legal and exercises update()'s
                    // empty-span early return, so we permit it but flush
                    // the remainder once the draw budget is spent.
                    std::uint32_t draws = 0;
                    constexpr std::uint32_t kMaxDraws = 4u * kMaxLen;
                    while (offset < spec.len && draws < kMaxDraws) {
                        const std::uint32_t remaining = spec.len - offset;
                        const std::uint32_t take = chunk_rng.next_below(remaining + 1u);
                        if (!st.update(std::span<const std::byte>{
                                base + offset, take}).has_value()) {
                            return false;
                        }
                        offset += take;
                        ++draws;
                    }
                    if (offset < spec.len &&
                        !st.update(std::span<const std::byte>{
                            base + offset, spec.len - offset}).has_value()) {
                        return false;
                    }
                    return true;
                });
            return random_ok;
        });
}
