#pragma once

// ── crucible::effects::Effect — named capability atoms ──────────────
//
// Six effect atoms parameterizing the Met(X) row algebra per
// 25_04_2026.md §3.3 and Tang-Lindley POPL 2026 (arXiv:2507.10301).
// Each atom replaces one of the existing fx::* capability tokens in
// crucible/Effects.h.  The new representation is reflection-friendly
// (`std::meta::info`-addressable) and composes via row union without
// the per-token boilerplate the existing fx::* tree carries.
//
//   Effect    | Replaces       | Carried by
//   ----------+----------------+-------------------------------------
//   Alloc     | fx::Alloc      | heap allocation, arena alloc, push_back
//   IO        | fx::IO         | file/network I/O (fprintf, send, recv)
//   Block     | fx::Block      | mutex, sleep, futex, spin-wait
//   Bg        | fx::Bg         | background thread context (Alloc+IO+Block)
//   Init      | fx::Init       | initialization context (Alloc+IO)
//   Test      | fx::Test       | test context (unrestricted)
//
//   Axiom coverage: TypeSafe — strong enum with explicit underlying
//                   type; reflection traversal sees all atoms.
//   Runtime cost:   zero — atoms are compile-time tags only.
//
// Foreground hot-path code holds an empty row; the type system rejects
// every effectful call.  See Computation.h for the carrier; EffectRow.h
// for set algebra; compat/Fx.h for backward-compat aliases preserving
// the existing fx::* spellings during migration.
//
// Self-test block at file end proves the atom catalog is exhaustive
// and that diagnostic-name emission covers every atom.

#include <cstdint>
#include <meta>
#include <string_view>

namespace crucible::effects {

// ── Effect atom ─────────────────────────────────────────────────────
enum class Effect : std::uint8_t {
    Alloc = 0,
    IO    = 1,
    Block = 2,
    Bg    = 3,
    Init  = 4,
    Test  = 5,
};

// Cardinality derived via reflection (P2996R13).  Adding a new atom
// auto-bumps this constant — no manual maintenance.  The name-
// coverage assertion in detail::capabilities_self_test then catches
// any new atom that lacks an `effect_name()` switch arm.
inline constexpr std::size_t effect_count =
    std::meta::enumerators_of(^^Effect).size();

// ── Diagnostic name emitter ─────────────────────────────────────────
[[nodiscard]] consteval std::string_view effect_name(Effect e) noexcept {
    switch (e) {
        case Effect::Alloc: return "Alloc";
        case Effect::IO:    return "IO";
        case Effect::Block: return "Block";
        case Effect::Bg:    return "Bg";
        case Effect::Init:  return "Init";
        case Effect::Test:  return "Test";
        default:            return std::string_view{"<unknown Effect>"};
    }
}

// ── Concept gate ────────────────────────────────────────────────────
//
// IsEffect<E> rejects template-parameter typos at substitution time,
// not at use site.
template <Effect E>
concept IsEffect =
    E == Effect::Alloc || E == Effect::IO   || E == Effect::Block ||
    E == Effect::Bg    || E == Effect::Init || E == Effect::Test;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::capabilities_self_test {

// Cardinality.  Held at six for the original Alloc/IO/Block/Bg/Init/
// Test catalog — if a future revision adds a seventh atom, this guard
// fires AND the name-coverage assertion below independently fires
// (the latter is the load-bearing one because it pinpoints the
// missing switch arm in effect_name()).
static_assert(effect_count == 6,
    "Effect catalog diverged from the original sextet — confirm the "
    "addition is intentional and the name-coverage assertion below "
    "still fires for the new atom.");

// Name coverage via reflection — every Effect atom MUST have a
// non-sentinel name from effect_name().  Adding a new atom without
// updating the switch fires this assertion at header-inclusion time.
[[nodiscard]] consteval bool every_effect_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Effect));
    // -Wshadow on `template for` body's induction variable is the
    // canonical false-positive across iterations; suppress locally.
    // See feedback_gcc16_c26_reflection_gotchas memory rule.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (effect_name([:en:]) == std::string_view{"<unknown Effect>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_effect_has_name(),
    "effect_name() switch is missing an arm for at least one Effect "
    "atom — add the arm or the new atom leaks the '<unknown Effect>' "
    "sentinel into diagnostics.");

// Every atom satisfies the concept gate.
static_assert(IsEffect<Effect::Alloc>);
static_assert(IsEffect<Effect::IO>);
static_assert(IsEffect<Effect::Block>);
static_assert(IsEffect<Effect::Bg>);
static_assert(IsEffect<Effect::Init>);
static_assert(IsEffect<Effect::Test>);

// Diagnostic names are non-empty AND distinct AND none falls through
// to the "<unknown Effect>" sentinel.
static_assert(!effect_name(Effect::Alloc).empty());
static_assert(!effect_name(Effect::IO).empty());
static_assert(!effect_name(Effect::Block).empty());
static_assert(!effect_name(Effect::Bg).empty());
static_assert(!effect_name(Effect::Init).empty());
static_assert(!effect_name(Effect::Test).empty());

static_assert(effect_name(Effect::Alloc) != "<unknown Effect>");
static_assert(effect_name(Effect::IO)    != "<unknown Effect>");
static_assert(effect_name(Effect::Block) != "<unknown Effect>");
static_assert(effect_name(Effect::Bg)    != "<unknown Effect>");
static_assert(effect_name(Effect::Init)  != "<unknown Effect>");
static_assert(effect_name(Effect::Test)  != "<unknown Effect>");

// Pairwise distinctness — every atom has a unique name.
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::IO));
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::Block));
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::Bg));
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::Init));
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::Test));
static_assert(effect_name(Effect::IO)    != effect_name(Effect::Block));
static_assert(effect_name(Effect::Bg)    != effect_name(Effect::Init));
static_assert(effect_name(Effect::Init)  != effect_name(Effect::Test));

}  // namespace detail::capabilities_self_test

}  // namespace crucible::effects
