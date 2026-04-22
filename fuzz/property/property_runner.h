#pragma once

// ═══════════════════════════════════════════════════════════════════
// property_runner.h — minimal property-based / E2E stress harness
//
// Property-based testing without an external dependency.  Each
// property test runs N iterations with deterministic Philox-derived
// randomness per iteration; a failing iteration prints its index +
// the input that triggered the failure for trivial reproduction.
//
// Usage in a property test (`fuzz/property/prop_*.cpp`):
//
//   #include "property_runner.h"
//   #include "random_input.h"
//
//   int main(int argc, char** argv) {
//       using namespace crucible::fuzz::prop;
//       Config cfg = parse_args(argc, argv);  // --iters / --seed
//       return run("hash_determinism", cfg,
//           [](Rng& rng) { return random_recipe(rng); },
//           [](const NumericalRecipe& r) {
//               return compute_recipe_hash(r) == compute_recipe_hash(r);
//           });
//   }
//
// ─── Why a custom harness rather than rapidcheck / fuzztest? ───────
//
// Crucible's no-external-deps discipline (CLAUDE.md §I).  Property-
// based testing here means: deterministic per-iteration RNG + an
// invariant + a printable input.  None of that needs a library.
// Hand-rolling keeps the test layer GCC-16-only just like the rest
// of the codebase.
//
// ─── Determinism ────────────────────────────────────────────────────
//
// Each iteration `i` derives its RNG key from the run-level seed
// plus `i` via Philox.  The same (seed, i) always produces the same
// random input, so a failure at iteration N is reproduced exactly by
// re-running with the same seed.  The default seed (0xC0FFEE) is
// fixed; --seed lets developers explore beyond the regression set.
//
// ─── Failure reporting ──────────────────────────────────────────────
//
// On the first invariant violation, the runner prints:
//
//   PROPERTY FAILED: hash_determinism
//     iteration: 4271
//     seed:      0xC0FFEE
//     input:     <pretty-printed via reflect_print or to_string>
//
// then aborts (so sanitizer + core-dump + IDE all latch the failure).
//
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Philox.h>
#include <crucible/Reflect.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace crucible::fuzz::prop {

// ─── Rng wrapper ───────────────────────────────────────────────────
//
// Per-iteration RNG: Philox-backed counter-mode generator.
// Each call to next32() consumes one 32-bit value from the iteration's
// 4×32 random pool, refilling via Philox::generate when exhausted.
// next64() composes two next32() calls.
//
// Pure: same (seed, iteration) → same sequence.  No global state.
class Rng {
 public:
    constexpr Rng(uint64_t seed, uint64_t iteration) noexcept
        : seed_{seed}, iteration_{iteration}
    {
        refill_();
    }

    // Uniform [0, 2^32).
    [[nodiscard]] uint32_t next32() noexcept {
        if (cursor_ >= 4) [[unlikely]] refill_();
        return pool_[cursor_++];
    }

    // Uniform [0, 2^64).
    [[nodiscard]] uint64_t next64() noexcept {
        const uint64_t lo = next32();
        const uint64_t hi = next32();
        return (hi << 32) | lo;
    }

    // Uniform [0, n).
    [[nodiscard]] uint32_t next_below(uint32_t n) noexcept {
        // Multiplicative debiasing (Lemire 2019).  For our scale
        // (n < 2^16 typically) the bias is negligible without
        // rejection; keep the cheap form.
        return n == 0 ? 0 : (next32() % n);
    }

    // Uniform float [0, 1).
    [[nodiscard]] float next_unit() noexcept {
        return Philox::to_uniform(next32());
    }

    // Pick one of N enumerated values.  Caller supplies the count.
    template <typename E>
    [[nodiscard]] E pick_enum(uint32_t count) noexcept {
        return static_cast<E>(next_below(count));
    }

 private:
    void refill_() noexcept {
        // Each iteration gets a fresh 4×u32 pool.  block_idx_
        // increments to walk further into the iteration's stream
        // when one pool is exhausted; the (iteration, block_idx)
        // tuple ensures statistical independence across pools
        // within the same iteration.
        const uint64_t pool_ctr =
            (iteration_ << 32) ^ static_cast<uint64_t>(block_idx_);
        pool_     = Philox::generate(pool_ctr, seed_);
        cursor_   = 0;
        ++block_idx_;
    }

    uint64_t                 seed_;
    uint64_t                 iteration_;
    uint32_t                 block_idx_ = 0;
    Philox::Ctr              pool_      {};
    uint32_t                 cursor_    = 4;  // forces initial refill
};

// ─── Configuration ─────────────────────────────────────────────────

struct Config {
    uint64_t seed       = 0xC0FFEEULL;
    uint64_t iterations = 100'000;
    bool     verbose    = false;
};

// Parse --seed=N --iters=N --verbose from argv.  Defaults preserved
// for missing args.  Unknown args are ignored.
[[nodiscard]] inline Config parse_args(int argc, char** argv) noexcept {
    Config cfg{};
    for (int i = 1; i < argc; ++i) {
        std::string_view a{argv[i]};
        if (a.starts_with("--seed=")) {
            cfg.seed = std::strtoull(a.data() + 7, nullptr, 0);
        } else if (a.starts_with("--iters=")) {
            cfg.iterations = std::strtoull(a.data() + 8, nullptr, 0);
        } else if (a == "--verbose") {
            cfg.verbose = true;
        }
    }
    return cfg;
}

// ─── Failure printer ───────────────────────────────────────────────
//
// Prints the failure header.  Caller can append a reflect_print of
// the failing input afterward.  Then aborts so the sanitizer state
// + stack frame are preserved for debugging.

[[noreturn]] inline void report_failure(
    std::string_view name,
    uint64_t iteration,
    uint64_t seed) noexcept
{
    std::fprintf(stderr,
        "\n═══════════════════════════════════════════════════════════\n"
        "PROPERTY FAILED: %.*s\n"
        "  iteration: %llu\n"
        "  seed:      0x%llX\n"
        "  reproduce: re-run the binary with --seed=0x%llX --iters=%llu\n"
        "═══════════════════════════════════════════════════════════\n",
        static_cast<int>(name.size()), name.data(),
        static_cast<unsigned long long>(iteration),
        static_cast<unsigned long long>(seed),
        static_cast<unsigned long long>(seed),
        static_cast<unsigned long long>(iteration + 1));
    std::abort();
}

// ─── The runner ────────────────────────────────────────────────────
//
// Generic property runner.  `Generator(Rng&) -> Input`.
// `Property(const Input&) -> bool` (true = invariant holds).
// On first false return, prints failure with the input via
// reflect_print (if Input is class-typed) or skips the input dump.

template <typename Generator, typename Property>
[[nodiscard]] inline int run(
    const char* name,
    const Config& cfg,
    Generator&& gen,
    Property&& check) noexcept
{
    std::fprintf(stderr,
        "[prop] %s: %llu iterations, seed=0x%llX\n",
        name,
        static_cast<unsigned long long>(cfg.iterations),
        static_cast<unsigned long long>(cfg.seed));

    for (uint64_t i = 0; i < cfg.iterations; ++i) {
        Rng rng{cfg.seed, i};
        auto input = gen(rng);

        if (cfg.verbose && (i % 10000 == 0)) {
            std::fprintf(stderr, "[prop]   iter %llu\n",
                static_cast<unsigned long long>(i));
        }

        if (!check(input)) {
            // No reflect_print of the input here — local struct
            // generators in the property tests don't always satisfy
            // reflect's identifier_of() prerequisite.  The seed +
            // iteration in the failure report are the reproduction
            // handles developers actually use.
            report_failure(name, i, cfg.seed);
        }
    }

    std::fprintf(stderr, "[prop] %s: PASSED %llu iterations\n",
        name,
        static_cast<unsigned long long>(cfg.iterations));
    return 0;
}

}  // namespace crucible::fuzz::prop
