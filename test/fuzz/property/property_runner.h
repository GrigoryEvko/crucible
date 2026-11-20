#pragma once

// ═══════════════════════════════════════════════════════════════════
// property_runner.h — minimal property-based / E2E stress harness
//
// Property-based testing without an external dependency.  Each
// property test runs N iterations with deterministic Philox-derived
// randomness per iteration; a failing iteration prints its index and
// the seed, which reproduce the failing input exactly.
//
// Usage in a property test (`test/fuzz/property/prop_*.cpp`):
//
//   #include "property_runner.h"
//   #include "random_input.h"
//
//   int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
//       using namespace crucible::fuzz::prop;
//       const Config cfg = parse_args(arguments, 2'000'000);
//       return run("recipe_hash_determinism", cfg,
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
// invariant + a reproducible input.  None of that needs a library.
// Hand-rolling keeps the test layer GCC-16-only just like the rest
// of the codebase.
//
// ─── Determinism ────────────────────────────────────────────────────
//
// Each iteration `i` draws from a Philox stream keyed by the run-level
// seed, with the counter built from `i`.  The same (seed, i) always
// produces the same random input, so a failure at iteration N is
// reproduced exactly by re-running with the same seed.  The default
// seed (0xC0FFEE) is fixed; --seed lets developers explore beyond the
// regression set.
//
// ─── Failure reporting ──────────────────────────────────────────────
//
// On the first invariant violation, the runner prints:
//
//   PROPERTY FAILED: recipe_hash_determinism
//     iteration: 4271
//     seed:      0xC0FFEE
//     reproduce: re-run the binary with --seed=0xC0FFEE --iters=4272
//
// then aborts (so sanitizer + core-dump + IDE all latch the failure).
// The runner does not print the input: a generator may return a local
// struct that reflection cannot name, and the seed and the iteration
// already rebuild it.
//
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Philox.h>
#include <fixy/Core.h>
#include <fixy/os/Args.h>
#include <foundation/os/Abort.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::fuzz::prop {

// The body of a property test, which each test defines.  main of
// property_main.cpp gives it the arguments of the process, and its result
// is the exit status of the test.
[[nodiscard]] int run_property(::fixy::os::Args arguments);

// ─── Dense enums ───────────────────────────────────────────────────
//
// A scoped enum whose enumerators hold 0, 1, ..., N - 1 in declaration
// order, with N > 0.  Rng::pick draws an enumerator by its index, so a
// hole or a reordered value would let it produce a value that names no
// enumerator.  The count comes from the enumerators, so a new
// enumerator is drawn without an edit at the draw site.

template <class E>
[[nodiscard]] consteval bool enumerators_are_dense() noexcept {
    std::size_t index = 0;
    // A negative value converts to a count past each index, so it is never
    // equal to the index.
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^E)) {
        if (static_cast<std::size_t>(std::meta::extract<E>(enumerator)) != index) return false;
        ++index;
    }
    return index != 0;
}

template <class E>
concept DenseEnum = ::foundation::reflect::ScopedEnum<E> && enumerators_are_dense<E>();

namespace detail::dense_enum_self_test {

enum class Dense : std::uint8_t {
    First,
    Second,
    Third
};
enum class Holed : std::uint8_t {
    First,
    Third = 2
};
enum class Reordered : std::uint8_t {
    Second = 1,
    First = 0
};
enum class Empty : std::uint8_t {
};
enum Unscoped : std::uint8_t {
    unscoped_first
};

static_assert(DenseEnum<Dense>);
static_assert(!DenseEnum<Holed>);
static_assert(!DenseEnum<Reordered>);
static_assert(!DenseEnum<Empty>);
static_assert(!DenseEnum<Unscoped>);
static_assert(!DenseEnum<std::uint8_t>);

}  // namespace detail::dense_enum_self_test

// ─── Rng wrapper ───────────────────────────────────────────────────
//
// Per-iteration RNG: Philox-backed counter-mode generator.
// Each call to next32() consumes one 32-bit value from the iteration's
// 4×32 random pool, refilling via fixy::Philox4x32::generate when exhausted.
// next64() composes two next32() calls.
//
// Pure: same (seed, iteration) → same sequence.  No global state.
class Rng {
public:
    constexpr Rng(uint64_t seed, uint64_t iteration) noexcept : seed_{seed}, iteration_{iteration} { refill_(); }

    // Uniform [0, 2^32).
    [[nodiscard]] uint32_t next32() noexcept {
        if (cursor_ >= 4) [[unlikely]]
            refill_();
        // refill_ sets the cursor to 0, so the cursor is below 4 here.
        return pool_.at(::fixy::Index<4>::check(cursor_++).expect("the cursor of the pool is below 4"));
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
    [[nodiscard]] float next_unit() noexcept { return Philox::to_uniform(next32()); }

    // One enumerator of E, each with the same probability.
    template <DenseEnum E>
    [[nodiscard]] E pick() noexcept {
        return static_cast<E>(next_below(static_cast<uint32_t>(::foundation::reflect::enum_count<E>)));
    }

private:
    constexpr void refill_() noexcept {
        // Each iteration gets a fresh 4×u32 pool.  block_idx_
        // increments to walk further into the iteration's stream
        // when one pool is exhausted; the (iteration, block_idx)
        // tuple ensures statistical independence across pools
        // within the same iteration.
        const uint64_t pool_ctr = (iteration_ << 32) ^ static_cast<uint64_t>(block_idx_);
        pool_ = ::fixy::Philox4x32::generate(pool_ctr, seed_);
        cursor_ = 0;
        ++block_idx_;
    }

    uint64_t seed_;
    uint64_t iteration_;
    uint32_t block_idx_ = 0;
    ::fixy::Philox4x32::Counter pool_{};
    uint32_t cursor_ = 4;  // forces initial refill
};

// ─── Run-time reads ────────────────────────────────────────────────
//
// The value at the reference, read at run time.  The function is noipa,
// so the caller does not know that the result is the argument.  The
// optimizer then folds no check on the result at compile time, and it
// merges no two calls of one function on two results.

template <class T>
[[gnu::noipa, nodiscard]] T const& read_at_run_time(T const& value) noexcept {
    return value;
}

// ─── Configuration ─────────────────────────────────────────────────

struct Config {
    uint64_t seed = 0xC0FFEEULL;
    uint64_t iterations = 100000;
    bool verbose = false;
};

// The value of the flag argument after its prefix, in the grammar of parse
// of foundation/core/Parse.h: decimal digits, or "0x" and hexadecimal
// digits.  A value that holds no such integer ends the run, so a typo in a
// flag does not run the default.
[[nodiscard]] inline uint64_t flag_value(::fixy::TextView const argument, ::fixy::TextView const prefix) noexcept {
    ::fixy::TextView const value = argument.window(prefix.size(), argument.size() - prefix.size())
                                       .expect("the argument of a flag starts with its prefix");
    return ::fixy::parse<uint64_t>(value).match(
        [](uint64_t const parsed) noexcept { return parsed; },
        [](::fixy::ParseError) noexcept -> uint64_t {
            ::fixy::fatal("property: the value of --seed= or --iters= is no decimal or 0x hexadecimal integer");
        });
}

// Parse --seed=N --iters=N --verbose from the arguments of main, which the
// argument door of fixy/os/Args.h gives.  Defaults preserved for missing
// args.  Unknown args are ignored.  The iteration count is clamped to
// `max_iterations`, the bound past which one property run costs more than
// its coverage returns.  Complexity: linear in the characters of the
// arguments.
[[nodiscard]] inline Config parse_args(::fixy::os::Args const arguments,
                                       uint64_t max_iterations = ::fixy::max_of<uint64_t>) noexcept {
    constexpr ::fixy::TextView kSeed{"--seed="};
    constexpr ::fixy::TextView kIterations{"--iters="};
    Config cfg{};
    for (std::size_t position = 1; position < arguments.size(); ++position) {
        ::fixy::TextView const argument = arguments.get(position).expect("a position below the count").view();
        if (::fixy::starts_with(argument, kSeed)) {
            cfg.seed = flag_value(argument, kSeed);
        } else if (::fixy::starts_with(argument, kIterations)) {
            cfg.iterations = flag_value(argument, kIterations);
        } else if (argument == ::fixy::TextView{"--verbose"}) {
            cfg.verbose = true;
        }
    }
    if (cfg.iterations > max_iterations) cfg.iterations = max_iterations;
    return cfg;
}

// ─── Failure printer ───────────────────────────────────────────────
//
// Prints the failure header, then aborts so the sanitizer state and
// the stack frame are preserved for debugging.

[[noreturn]] inline void report_failure(::fixy::TextView const name, const Config& cfg, uint64_t iteration) noexcept {
    ::fixy::report(::fixy::Sink::Err,
                   "\n═══════════════════════════════════════════════════════════\n"
                   "PROPERTY FAILED: {}\n"
                   "  iteration: {}\n"
                   "  seed:      0x{}\n"
                   "  reproduce: re-run the binary with --seed=0x{} --iters={}\n"
                   "═══════════════════════════════════════════════════════════\n",
                   name, iteration, ::fixy::Hex<16>{cfg.seed}, ::fixy::Hex<16>{cfg.seed}, iteration + 1);
    ::foundation::os::abort_process();
}

// ─── The runner ────────────────────────────────────────────────────
//
// Generic property runner.  `Generator(Rng&) -> Input`.
// `Property(const Input&) -> bool` (true = invariant holds).
// On the first false return, it reports the iteration and the seed.

template <typename Generator, typename Property>
[[nodiscard]] inline int run(::fixy::TextView const name, const Config& cfg, Generator&& gen,
                             Property&& check) noexcept {
    ::fixy::report(::fixy::Sink::Err, "[prop] {}: {} iterations, seed=0x{}\n", name, cfg.iterations,
                   ::fixy::Hex<16>{cfg.seed});

    for (uint64_t i = 0; i < cfg.iterations; ++i) {
        Rng rng{cfg.seed, i};
        auto input = gen(rng);

        if (cfg.verbose && (i % 10000 == 0)) {
            ::fixy::report(::fixy::Sink::Err, "[prop]   iter {}\n", i);
        }

        if (!check(input)) {
            report_failure(name, cfg, i);
        }
    }

    ::fixy::report(::fixy::Sink::Err, "[prop] {}: PASSED {} iterations\n", name, cfg.iterations);
    return 0;
}

}  // namespace crucible::fuzz::prop
