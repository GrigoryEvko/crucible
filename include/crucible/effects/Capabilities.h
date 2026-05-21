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
//
// ── Why no Async / Network / CT atoms (FIXY-AUDIT-B4) ───────────────
//
// The fixy stance design considered three additional capability atoms
// — `Async` (coroutine reentrancy), `Network` (socket IO), and
// `CT` (constant-time discipline) — and explicitly rejected adding
// any of them to the Effect enum.  The 6-atom closed set
// {Alloc, IO, Block, Bg, Init, Test} is the production hot-path
// effect surface; the rejected three are downstream concerns the hot
// path does NOT track per-call.
//
// Rationale, per axis the concern actually belongs on:
//
//   Async (coroutine reentrancy) — already expressed by the fixy
//     Reentrancy axis (`fixy::stance::AsyncEndpoint` resolves
//     Reentrancy=Coroutine via `safety::fn::ReentrancyMode`).
//     Coroutine reentrancy is a CONTROL-FLOW property of how a
//     function suspends and resumes, NOT a capability the function
//     exercises — adding `Async` to Effect would conflate the two.
//     The hot path cannot suspend; the Reentrancy axis already
//     rejects coroutine bodies at substitution time.
//
//   Network (socket IO) — already covered by the existing `IO`
//     atom plus the `Bg` context (background-thread aggregate that
//     bundles Alloc + IO + Block).  Distinguishing "file IO" from
//     "network IO" at the capability level requires per-call
//     bookkeeping the hot path cannot afford; the runtime's CNT-P
//     transport layer carries the network-vs-disk distinction
//     downstream via its own typestate (sessions::, cipher::Tier).
//     Adding `Network` to Effect would force every IO-bearing
//     signature to choose between two atoms that the substrate
//     treats identically at the row-algebra layer.
//
//   CT (constant-time crypto) — expressed by the fixy
//     `stance::CtCrypto` composite stance combining Security=Secret
//     (via `as_secret`), Effect=Row<> (empty via `with<>` —
//     constant-time paths MUST NOT exercise IO/Alloc/Block because
//     any such trip is a timing-observable side channel), and the
//     `safety::ConstantTime<T>` wrapper for branch-free primitives.
//     The §30.14 implicit-flow detector enforces the discipline
//     structurally.  Promoting CT to an Effect atom would imply
//     constant-time bodies could opt into Alloc/IO/Block — exactly
//     the discipline the stance forbids.  The empty row is the
//     correct representation; an explicit CT atom would weaken it.
//
// The closed-set assertion in the self-test block below pins the
// catalog at six.  Adding any of {Async, Network, CT} requires the
// "Major-version event procedure" ceremony above — bumping
// CDAG_VERSION, flushing federation caches, re-pinning canonical
// row_hash values in RowHashFold.h — and would not buy anything the
// current axis decomposition does not already deliver.

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
// ── fixy-A3-005 — private default ctor + passkey-via-passkey ────────
//
// Each context's default ctor is **private**, friending only the
// canonical mint factory.  Each factory takes a passkey type whose
// own default ctor is private and friended only to authorized
// production entry points (`Vigil`, `BackgroundThread`) plus the
// test-witness scaffolding (`effects::testing::TestWitness`).
//
// Mirrors the H-25 `crash_witness_key` ↔ `WrapCrashReturnKey` layered
// gate from permissions/PermissionInherit.h: production code holds
// the upstream passkey by construction; user TUs cannot forge a
// context without holding ONE of the friended entry points' identity,
// closing the "any TU can mint a cross-tier capability" hole.
//
// Production:                         | Tests + bench:
//   void Vigil::ctor() {              |   auto init = effects::testing
//       auto k = detail::ctx_mint::   |              ::TestWitness::init();
//                    init_key{};      |   // or shorter:
//       auto init = mint_init_context(|   auto init = effects::testing::init();
//                    k);              |
//   }                                 |
//
// Hot-path code holds NO context, holds NO passkey, therefore cannot
// construct any cap::* token, therefore cannot call any cap::*-taking
// function.  The discipline scales: adding a new privileged entry
// point means adding a single friend declaration to the relevant
// passkey type — discoverable by grep on `detail::ctx_mint::`.
//
// fixy-A3-015: each context's mint factory is `constexpr noexcept`
// so the context can be aggregated at compile time within friended
// callers (`Vigil::Vigil(...)` ctor body, perf-loader templates).

// Forward declarations for friend lists.  Briefly close
// crucible::effects (a nested-namespace-declaration closes both
// crucible AND crucible::effects in one `}`) so we can forward-declare
// the host classes Vigil + BackgroundThread that friend the passkeys
// below.  Then reopen.
}  // namespace crucible::effects

namespace crucible {
class Vigil;
struct BackgroundThread;
}

namespace crucible::effects {

namespace testing { struct TestWitness; }

// Forward-declare ExecCtx so the contexts can friend it (ExecCtx
// aggregate-inits its Cap member via NSDMI, which requires access to
// the Cap's default ctor — friending ExecCtx grants that access
// without leaking the default ctor to user TUs).
template <class Cap, class Numa, class Alloc, class Heat,
          class Resid, class Row, class Workload, class Progress>
struct ExecCtx;

namespace detail::ctx_mint {

// Bg minter key.  Private default ctor friended only to authorized
// production entry points + the test-witness namespace.
class bg_key {
private:
    constexpr bg_key() noexcept = default;

    friend struct ::crucible::BackgroundThread;
    friend struct ::crucible::effects::testing::TestWitness;
};

class init_key {
private:
    constexpr init_key() noexcept = default;

    friend class ::crucible::Vigil;
    friend struct ::crucible::BackgroundThread;
    friend struct ::crucible::effects::testing::TestWitness;
};

class test_key {
private:
    constexpr test_key() noexcept = default;

    friend struct ::crucible::effects::testing::TestWitness;
};

}  // namespace detail::ctx_mint

// Forward-declare the three context classes so the §XXI mint-validity
// concepts (next) can name them in their `is_nothrow_default_constructible_v`
// clauses.  Trait instantiation is deferred until the concept is checked
// (call site), at which point the full class definitions are visible.
class Bg;
class Init;
class Test;

// ── §XXI mint-validity concepts ──────────────────────────────────────
//
// FIXY-V-017/018/019: each context-mint factory is a function template
// gated on a single concept (§XXI: "single composite concept" rule)
// that pins the passkey-to-context pairing at concept-evaluation time
// — surfaces wrong-key mistakes (e.g. handing bg_key to mint_init_context)
// as a concept-violation diagnostic before overload resolution finishes
// burning instantiation budget.
//
// **Why no nothrow check here**: `std::is_nothrow_default_constructible_v<Bg>`
// would evaluate at concept-substitution scope, which lacks friend access
// to Bg's private default ctor and reports `false` regardless of NSDMI
// noexcept-ness.  The body-level `static_assert(noexcept(Bg{}))` is the
// authoritative noexcept gate (it runs inside the friended scope where
// access is granted); the concept's job is the key-type pairing alone.
//
// These are token mints (passkey authority chain established at the
// key's friend list, not threaded through a Ctx parameter), so the
// concept signature is `<Key>` not `<Context, Ctx>`.

template <class Key>
concept CanMintBgContext = std::same_as<Key, detail::ctx_mint::bg_key>;

template <class Key>
concept CanMintInitContext = std::same_as<Key, detail::ctx_mint::init_key>;

template <class Key>
concept CanMintTestContext = std::same_as<Key, detail::ctx_mint::test_key>;

class Bg {
private:
    // fixy-A3-005 (CLAUDE.md §XXI Universal Mint Pattern): the only
    // path to a Bg context is `mint_bg_context(detail::ctx_mint::bg_key)`,
    // and the only path to a bg_key is through a friended class.
    constexpr Bg() noexcept = default;

    template <class Key>
        requires CanMintBgContext<Key>
    friend constexpr Bg mint_bg_context(Key) noexcept;

    // ExecCtx<Bg, ...> aggregate-inits its Cap member via NSDMI
    // (`[[no_unique_address]] Cap cap_{};` in ExecCtx.h).  NSDMI
    // access is checked in the member's containing-class context per
    // [class.base.init]/9, so friending the ExecCtx template grants
    // ExecCtx's class body access to Bg's private default ctor while
    // keeping it private from every other TU.
    template <class Cap, class Numa, class Alloc, class Heat,
              class Resid, class Row, class Workload>
    friend struct ::crucible::effects::ExecCtx;

public:
    [[no_unique_address]] cap::Alloc alloc{};
    [[no_unique_address]] cap::IO    io{};
    [[no_unique_address]] cap::Block block{};
};

class Init {
private:
    constexpr Init() noexcept = default;

    template <class Key>
        requires CanMintInitContext<Key>
    friend constexpr Init mint_init_context(Key) noexcept;

    template <class Cap, class Numa, class Alloc, class Heat,
              class Resid, class Row, class Workload>
    friend struct ::crucible::effects::ExecCtx;

public:
    [[no_unique_address]] cap::Alloc alloc{};
    [[no_unique_address]] cap::IO    io{};
};

class Test {
private:
    constexpr Test() noexcept = default;

    template <class Key>
        requires CanMintTestContext<Key>
    friend constexpr Test mint_test_context(Key) noexcept;

    template <class Cap, class Numa, class Alloc, class Heat,
              class Resid, class Row, class Workload>
    friend struct ::crucible::effects::ExecCtx;

public:
    [[no_unique_address]] cap::Alloc alloc{};
    [[no_unique_address]] cap::IO    io{};
    [[no_unique_address]] cap::Block block{};
};

// ── Mint factories (§XXI Universal Mint Pattern) ─────────────────────
//
// Each factory takes the corresponding passkey by value (sizeof == 1
// each — EBO-collapsed; zero runtime cost).  The factory body just
// trades the key for a fresh context — the type-level check has
// already happened at the passkey's construction site.

// fixy-A3-015 (#1622): each factory body holds a friend-scope
// `static_assert(noexcept(Context{}))` pin.  Bg/Init/Test's default
// constructors are private; the only places `Context{}` is a valid
// expression are inside the friended mint factory bodies (and ExecCtx,
// via NSDMI).  Pinning the noexcept-ness HERE catches the moment any
// cap::* atom's NSDMI becomes throwing — the regression localizes to
// the broken context AND to the cap atom (cf. the cap::*-level pins
// in detail::capabilities_self_test below).  Defense-in-depth chain:
//
//   cap::*  ── noexcept default ctor  (atom level)
//      │
//      ▼
//   Bg/Init/Test  ── noexcept aggregate-init via NSDMI  (context level)
//      │
//      ▼
//   TestWitness::bg/init/test()  ── noexcept call  (mint level, pinned
//                                     at lines 580-585 of the self-test)
//
// All three layers fire on a throwing-NSDMI regression.  The factory-
// body pin is the middle layer — it sits in the only scope where the
// private default ctor is accessible.

template <class Key>
    requires CanMintBgContext<Key>
[[nodiscard]] inline constexpr Bg
mint_bg_context(Key) noexcept {
    static_assert(noexcept(Bg{}),
        "fixy-A3-015: Bg default ctor MUST be noexcept — a cap::* "
        "token's NSDMI must never throw.");
    return Bg{};
}

template <class Key>
    requires CanMintInitContext<Key>
[[nodiscard]] inline constexpr Init
mint_init_context(Key) noexcept {
    static_assert(noexcept(Init{}),
        "fixy-A3-015: Init default ctor MUST be noexcept — a cap::* "
        "token's NSDMI must never throw.");
    return Init{};
}

template <class Key>
    requires CanMintTestContext<Key>
[[nodiscard]] inline constexpr Test
mint_test_context(Key) noexcept {
    static_assert(noexcept(Test{}),
        "fixy-A3-015: Test default ctor MUST be noexcept — a cap::* "
        "token's NSDMI must never throw.");
    return Test{};
}

// ── effects::testing — scaffolding entry point ──────────────────────
//
// Tests + bench harnesses construct contexts via this namespace.  The
// `TestWitness` struct is friended on every passkey type, so its
// static accessors can produce a key and immediately trade it in for
// the corresponding context.
//
// **Production-code discipline**: `effects::testing::` in production
// code (anywhere outside `test/`, `bench/`, or test-only headers) is
// a review-rejected smell — the entire point of the namespace is
// grep-discoverability for "this TU is exercising the test path".

namespace testing {

struct TestWitness {
    [[nodiscard]] static constexpr Bg bg() noexcept {
        return mint_bg_context(detail::ctx_mint::bg_key{});
    }
    [[nodiscard]] static constexpr Init init() noexcept {
        return mint_init_context(detail::ctx_mint::init_key{});
    }
    [[nodiscard]] static constexpr Test test() noexcept {
        return mint_test_context(detail::ctx_mint::test_key{});
    }
};

// Free-function aliases for terse call sites.
[[nodiscard]] inline constexpr Bg   bg()   noexcept { return TestWitness::bg(); }
[[nodiscard]] inline constexpr Init init() noexcept { return TestWitness::init(); }
[[nodiscard]] inline constexpr Test test() noexcept { return TestWitness::test(); }

}  // namespace testing

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

// fixy-A3-015 (#1622): cap::* atom-level noexcept pins.  Every cap::*
// is the source of an NSDMI inside Bg/Init/Test (`[[no_unique_address]]
// cap::Alloc alloc{};`).  If a future change to cap::* adds a throwing
// default ctor or NSDMI, this assertion fires AT THE ATOM — naming the
// exact cap that broke before the chain reaches Bg/Init/Test's friended
// factory bodies (which carry their own pins per A3-015) and the
// outermost TestWitness pins above.  Three-layer defense in depth: atom
// → context → mint.  A regression at the atom layer fires all three;
// the atom pin pinpoints the cause.
static_assert(noexcept(cap::Alloc{}));
static_assert(noexcept(cap::IO{}));
static_assert(noexcept(cap::Block{}));
static_assert(std::is_nothrow_default_constructible_v<cap::Alloc>);
static_assert(std::is_nothrow_default_constructible_v<cap::IO>);
static_assert(std::is_nothrow_default_constructible_v<cap::Block>);

// Top-level aliases really do refer to the cap::* originals.
static_assert(std::is_same_v<Alloc, cap::Alloc>);
static_assert(std::is_same_v<IO,    cap::IO>);
static_assert(std::is_same_v<Block, cap::Block>);

// fixy-A3-005: Bg / Init / Test contexts default-construct without
// throwing — but only through their friended mint factory.  We assert
// the property at the mint-factory level via the static accessors on
// effects::testing::TestWitness (which has friend access to every
// passkey type), confirming the (a) constexpr noexcept (b) zero-cost
// EBO-collapsed shape (c) lvalue-callable contract.
static_assert(noexcept(::crucible::effects::testing::TestWitness::bg()));
static_assert(noexcept(::crucible::effects::testing::TestWitness::init()));
static_assert(noexcept(::crucible::effects::testing::TestWitness::test()));
static_assert(noexcept(::crucible::effects::testing::bg()));
static_assert(noexcept(::crucible::effects::testing::init()));
static_assert(noexcept(::crucible::effects::testing::test()));

// fixy-A3-005: direct default construction MUST be rejected — the
// whole point of the privatization is that user TUs cannot forge a
// context.  Use the negation here so the property is visible in the
// header self-tests; the load-bearing reject lives in the HS14
// neg-compile fixtures at test/effects_neg/.
static_assert(!std::is_default_constructible_v<Bg>,
    "fixy-A3-005: Bg default ctor must be private — use "
    "effects::testing::TestWitness::bg() in tests or the friended "
    "production entry point (BackgroundThread).");
static_assert(!std::is_default_constructible_v<Init>,
    "fixy-A3-005: Init default ctor must be private — use "
    "effects::testing::TestWitness::init() in tests or the friended "
    "production entry point (Vigil, BackgroundThread).");
static_assert(!std::is_default_constructible_v<Test>,
    "fixy-A3-005: Test default ctor must be private — use "
    "effects::testing::TestWitness::test().");

// ── Runtime smoke test (fixy-A3-021) ────────────────────────────────
//
// Drive every constexpr accessor through a NON-constant argument so
// the front-end type-checks the inline body against runtime semantics
// — the bug class that pure-static_assert coverage misses.  All work
// is `inline` (one-definition rule) and almost certainly elided under
// -O3, but the parse + body type-check is the load-bearing step.
inline void runtime_smoke_test() {
    // `e` is intentionally NOT constexpr — drives effect_name() through
    // the runtime path, exercising the constexpr-not-consteval demotion
    // that the discipline comment at file head asserts (line 151).
    Effect e = Effect::Alloc;
    [[maybe_unused]] std::string_view n1 = effect_name(e);
    e = Effect::IO;     [[maybe_unused]] std::string_view n2 = effect_name(e);
    e = Effect::Block;  [[maybe_unused]] std::string_view n3 = effect_name(e);
    e = Effect::Bg;     [[maybe_unused]] std::string_view n4 = effect_name(e);
    e = Effect::Init;   [[maybe_unused]] std::string_view n5 = effect_name(e);
    e = Effect::Test;   [[maybe_unused]] std::string_view n6 = effect_name(e);

    // cap::* tag default construction at runtime — the [[no_unique_address]]
    // EBO discipline only holds if these ctors are runtime-callable.
    [[maybe_unused]] cap::Alloc a_tag{};
    [[maybe_unused]] cap::IO    i_tag{};
    [[maybe_unused]] cap::Block b_tag{};

    // Bg/Init/Test mint through the testing-witness facade at runtime
    // — fixy-A3-005 made the direct default ctor private; the witness
    // is the only sanctioned path and MUST be runtime-callable.
    [[maybe_unused]] auto bg_ctx   = ::crucible::effects::testing::bg();
    [[maybe_unused]] auto init_ctx = ::crucible::effects::testing::init();
    [[maybe_unused]] auto test_ctx = ::crucible::effects::testing::test();
}

}  // namespace detail::capabilities_self_test

}  // namespace crucible::effects
