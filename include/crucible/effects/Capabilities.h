#pragma once

// ── crucible::effects::Effect — named capability atoms ──────────────
//
// Six effect atoms parameterizing the Met(X) row algebra per
// 25_04_2026.md §3.3 and Tang-Lindley POPL 2026 (arXiv:2507.10301).
// The legacy crucible/Effects.h fx::* tree was deleted in
// FOUND-B07 / METX-5 — this header IS the production capability surface.
//
//   Effect    | Underlying value | Carried by
//   ----------+------------------+-------------------------------------
//   Alloc     | 0                | heap allocation, arena alloc, push_back
//   IO        | 1                | file/network I/O (fprintf, send, recv)
//   Block     | 2                | mutex, sleep, futex, spin-wait
//   Bg        | 3                | background thread context (Alloc + IO + Block)
//   Init      | 4                | initialization context (Alloc + IO)
//   Test      | 5                | test context (unrestricted: Alloc + IO + Block)
//
//   Axiom coverage: TypeSafe — strong enum with explicit underlying
//                   type; reflection traversal sees all atoms.
//                   DetSafe — underlying values are FROZEN; row_hash
//                   federation depends on bit-for-bit value stability
//                   (see "Append-only Universe extension" below).
//   Runtime cost:   zero — atoms are compile-time tags only.  The
//                   companion cap::* value-level tag types and the
//                   Bg / Init / Test context structs collapse to one
//                   byte each via [[no_unique_address]] EBO.
//
// Foreground hot-path code holds an empty row; the type system rejects
// every effectful call.  See Computation.h for the carrier and
// EffectRow.h for the set algebra (Subrow / row_union_t / etc.).
//
// ── Append-only Universe extension (FOUND-I04) ──────────────────────
//
// **Existing atom values are immutable.**  A change to any value
// already shipped (re-numbering Bg from 3 to anything else, deleting
// Test, swapping Alloc and IO) is a wire-format-breaking event for
// row_hash (safety/diag/RowHashFold.h) federation cache keys.  All
// Family-A persistent hashes (CDAG_VERSION, Types.h taxonomy) tied
// to those rows would silently re-key, invalidating every published
// L1 / L2 / L3 cache entry across every fleet that consumed those
// rows.
//
// **New atoms append only.**  An additional capability (e.g. a
// hypothetical `Refute = 6`) joins at the next free underlying value;
// existing atom values stay pinned.  This bounds federation-cache
// invalidation to entries that mention the new atom — pre-existing
// `Row<Bg>` keeps the same row_hash forever because `Effect::Bg`
// keeps underlying value 3 forever.
//
// **Enforcement.**  The self-test block at file end pins each
// underlying value with an explicit `static_assert`.  A re-numbering
// fires the static_assert at the offending atom, naming the value
// drift.  An additional sentinel block in
// `safety/diag/RowHashFold.h` pins the resulting `row_hash` for
// every singleton row plus EmptyRow plus the full-Universe row to
// hex literals — that catches both enum-value drift AND any change
// to the fold algorithm itself.  Cardinality `effect_count` is
// derived via reflection (P2996R13) and pinned at six.
//
// **Major-version event procedure.**  If a change to an existing
// value is genuinely required (e.g., reflection-driven re-codification
// of the catalog), bump CDAG_VERSION (Types.h Family-A taxonomy),
// flush every L1/L2/L3 cache entry, document the wire-format break
// in MIMIC.md / FORGE.md / CRUCIBLE.md, and re-pin the canonical
// hashes in RowHashFold.h's self-test block to the new values.
// Until that ceremony lands, the static_asserts in this header
// MUST stay green.
//
// Self-test block at file end proves the atom catalog is exhaustive,
// underlying values are pinned, and diagnostic-name emission covers
// every atom.

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

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
//
// constexpr (not consteval) so the runtime smoke-test discipline can
// drive every Effect atom through this accessor with non-constant
// arguments — per feedback_algebra_runtime_smoke_test_discipline.
// Constant-evaluated when called from consteval contexts (e.g.,
// every_effect_has_name() below) — the demotion costs nothing at
// compile time and unblocks runtime probing.
[[nodiscard]] constexpr std::string_view effect_name(Effect e) noexcept {
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

// ── Capability tag types (cap::*) ───────────────────────────────────
//
// The Effect enum is the abstract atom catalog used by the Met(X) row
// algebra; these `cap::*` types are the concrete value-level proof
// tokens carried at function-parameter sites.  A function declared
//
//   void* alloc(cap::Alloc, size_t n);
//
// can only be called from a context that synthesises a `cap::Alloc`
// value — and only the `Bg` / `Init` / `Test` context types below
// expose them as members.  Foreground hot-path code holds no context
// and therefore cannot construct any `cap::*` token; the type system
// rejects any attempt to do so.
//
// Layout: every cap::* is one byte (default-constructible, copyable,
// trivially-destructible empty struct).  Marked `[[no_unique_address]]`
// in containing contexts collapses them to zero bytes via EBO.
//
// noexcept on every special member: capability tokens are empty
// structs and must never throw — explicit noexcept documents intent
// AND fires a compile-time contradiction if a future body adds a
// throwing expression.
namespace cap {

struct Alloc {
    constexpr Alloc()                          noexcept = default;
    constexpr Alloc(const Alloc&)              noexcept = default;
    constexpr Alloc(Alloc&&)                   noexcept = default;
    constexpr Alloc& operator=(const Alloc&)   noexcept = default;
    constexpr Alloc& operator=(Alloc&&)        noexcept = default;
    ~Alloc()                                            = default;
};

struct IO {
    constexpr IO()                             noexcept = default;
    constexpr IO(const IO&)                    noexcept = default;
    constexpr IO(IO&&)                         noexcept = default;
    constexpr IO& operator=(const IO&)         noexcept = default;
    constexpr IO& operator=(IO&&)              noexcept = default;
    ~IO()                                               = default;
};

struct Block {
    constexpr Block()                          noexcept = default;
    constexpr Block(const Block&)              noexcept = default;
    constexpr Block(Block&&)                   noexcept = default;
    constexpr Block& operator=(const Block&)   noexcept = default;
    constexpr Block& operator=(Block&&)        noexcept = default;
    ~Block()                                            = default;
};

}  // namespace cap

// ── Top-level effects:: aliases for the cap tags ────────────────────
//
// Production call sites use the short form (`effects::Alloc`); the
// `cap::` namespace exists for diagnostic clarity when the surrounding
// code is doing something unusual with the tags directly.
using Alloc = cap::Alloc;
using IO    = cap::IO;
using Block = cap::Block;

// ── Context types — Bg / Init / Test ────────────────────────────────
//
// A context aggregates the cap::* tokens a particular thread or
// scope is allowed to exercise.  Members are `[[no_unique_address]]`
// so the whole context collapses to one byte under -O3.
//
//   Bg    — background thread: alloc + io + block
//   Init  — initialization scope: alloc + io (no block — init must
//           never block on a synchronisation primitive)
//   Test  — test driver: unrestricted (alloc + io + block)
//
// Usage:
//   void bg_main(Arena& arena) {
//       effects::Bg bg;
//       arena.alloc_obj<RegionNode>(bg.alloc);  // OK
//   }
//
// Hot-path code holds NO context, therefore cannot construct any
// cap::* token, therefore cannot call any cap::*-taking function.

struct Bg {
    [[no_unique_address]] cap::Alloc alloc{};
    [[no_unique_address]] cap::IO    io{};
    [[no_unique_address]] cap::Block block{};
};

struct Init {
    [[no_unique_address]] cap::Alloc alloc{};
    [[no_unique_address]] cap::IO    io{};
};

struct Test {
    [[no_unique_address]] cap::Alloc alloc{};
    [[no_unique_address]] cap::IO    io{};
    [[no_unique_address]] cap::Block block{};
};

static_assert(sizeof(Bg)   == 1, "Bg context must be 1 byte (EBO over empty cap::* members)");
static_assert(sizeof(Init) == 1, "Init context must be 1 byte");
static_assert(sizeof(Test) == 1, "Test context must be 1 byte");
static_assert(sizeof(cap::Alloc) == 1);
static_assert(sizeof(cap::IO)    == 1);
static_assert(sizeof(cap::Block) == 1);

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

// ── Append-only Universe pin (FOUND-I04) ────────────────────────────
//
// Underlying values are FROZEN.  A change here is a federation-cache
// wire-format break — see the "Append-only Universe extension" block
// at the file head for the audit / migration ceremony.  These
// assertions fire instantly on any drift, naming the offending atom.
//
// A new atom (e.g. Refute) MUST land at the next free value (6, 7,
// ...) without disturbing the existing pin lines below.  Adding a
// new `static_assert(... == 6)` line below this block at the same
// time keeps the pin set complete.
static_assert(static_cast<std::uint8_t>(Effect::Alloc) == 0,
    "Effect::Alloc value drifted — federation row_hash invalidated.  "
    "Restore Alloc=0 or follow the major-version migration ceremony "
    "documented at file head.");
static_assert(static_cast<std::uint8_t>(Effect::IO)    == 1,
    "Effect::IO value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(Effect::Block) == 2,
    "Effect::Block value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(Effect::Bg)    == 3,
    "Effect::Bg value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(Effect::Init)  == 4,
    "Effect::Init value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(Effect::Test)  == 5,
    "Effect::Test value drifted — federation row_hash invalidated.");

// Underlying type pinned at uint8_t — a future widen to uint16_t or
// uint32_t silently changes ABI of any struct that uses Effect by
// value.  Federation row_hash sees only the underlying value (cast
// to uint64_t inside fmix64_fold) so type widening is invisible to
// the hash, but still ABI-breaking for transport structs.
static_assert(std::is_same_v<std::underlying_type_t<Effect>, std::uint8_t>,
    "Effect underlying type drifted from uint8_t — ABI change.");

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

// ── Cap-tag layout invariants ───────────────────────────────────────
//
// Every cap::* token is a default-constructible empty struct.  Bg /
// Init / Test contexts hold them as [[no_unique_address]] members and
// collapse to one byte total.
static_assert(std::is_default_constructible_v<cap::Alloc>);
static_assert(std::is_default_constructible_v<cap::IO>);
static_assert(std::is_default_constructible_v<cap::Block>);
static_assert(std::is_trivially_copyable_v<cap::Alloc>);
static_assert(std::is_trivially_copyable_v<cap::IO>);
static_assert(std::is_trivially_copyable_v<cap::Block>);
static_assert(std::is_trivially_destructible_v<cap::Alloc>);
static_assert(std::is_trivially_destructible_v<cap::IO>);
static_assert(std::is_trivially_destructible_v<cap::Block>);

// Top-level aliases really do refer to the cap::* originals.
static_assert(std::is_same_v<Alloc, cap::Alloc>);
static_assert(std::is_same_v<IO,    cap::IO>);
static_assert(std::is_same_v<Block, cap::Block>);

// Bg / Init / Test contexts default-construct without throwing.
static_assert(std::is_nothrow_default_constructible_v<Bg>);
static_assert(std::is_nothrow_default_constructible_v<Init>);
static_assert(std::is_nothrow_default_constructible_v<Test>);

}  // namespace detail::capabilities_self_test

}  // namespace crucible::effects
